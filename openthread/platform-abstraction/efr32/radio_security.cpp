/*
 *  Copyright (c) 2025, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   This file implements the radio security for the EFR32 platform.
 */
#include "radio_security.h"
#include "security_manager.h"

#include <openthread-core-config.h>
#include <openthread/platform/radio.h>
#include <openthread/platform/time.h>

#include "common/code_utils.hpp"
#include "common/debug.hpp"
#include "common/logging.hpp"
#include "utils/code_utils.h"
#include "utils/mac_frame.h"

#include "platform-efr32.h"
#include "radio_instance.h"

extern "C" {
#include "sl_core.h"
#include "sl_packet_utils.h"
}

#if (OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2)

// Security key management
enum class MacKeyType
{
    PREV,
    CURRENT,
    NEXT,
    COUNT
};

struct securityMaterial
{
    uint8_t           ackKeyId;
    uint8_t           keyId;
    volatile uint32_t macFrameCounter;
    volatile uint32_t ackFrameCounter;
    otMacKeyMaterial  keys[static_cast<int>(MacKeyType::COUNT)];
    // KSU slot index for each stored key. 0xFF means "not stored in KSU".
    uint8_t ksuSlot[static_cast<int>(MacKeyType::COUNT)];
};

// Per-instance security material
static securityMaterial sMacKeys[RADIO_INTERFACE_COUNT];

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
// Default wake key (index 129): derived from the Network Key, one static slot per interface.
static otMacKeyMaterial sDefaultWakeKey[RADIO_INTERFACE_COUNT];

// Guest wake keys (indices 130-192): one static slot per peer per interface.
// Slot is empty when mKeyIndex == 0.
typedef struct
{
    uint8_t          mKeyIndex;
    otMacKeyMaterial mKey;
} sli_ot_wake_guest_key_t;

static sli_ot_wake_guest_key_t sGuestWakeKeys[RADIO_INTERFACE_COUNT][OPENTHREAD_CONFIG_THREAD_DIRECT_MAX_DIRECT_PEERS];
#endif

// External declarations
extern otExtAddress sExtAddress[RADIO_EXT_ADDR_COUNT];

extern "C" {

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
static void sli_ot_radio_security_store_wake_key_material(otMacKeyMaterial *aDest, const otMacKeyMaterial *aWakeKey);
#endif

void sli_ot_radio_security_init(void)
{
    // Initialize security material for all instances
    memset(sMacKeys, 0, sizeof(sMacKeys));
    // Mark KSU slots as "not present"
    for (size_t i = 0; i < RADIO_INTERFACE_COUNT; ++i)
    {
        for (int k = 0; k < static_cast<int>(MacKeyType::COUNT); ++k)
        {
            sMacKeys[i].ksuSlot[k] = 0xFF;
        }
    }

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    memset(sDefaultWakeKey, 0, sizeof(sDefaultWakeKey));
    memset(sGuestWakeKeys, 0, sizeof(sGuestWakeKeys));
#endif
}

void sli_ot_radio_security_deinit(void)
{
#if (OPENTHREAD_CONFIG_CRYPTO_LIB == OPENTHREAD_CONFIG_CRYPTO_LIB_PSA)
#ifdef LPWAES
#if defined(KSU_PRESENT)
    // Unregister all KSU keys before clearing memory
    for (size_t i = 0; i < RADIO_INTERFACE_COUNT; ++i)
    {
        for (int k = 0; k < static_cast<int>(MacKeyType::COUNT); ++k)
        {
            psa_key_id_t key_ref = sMacKeys[i].keys[k].mKeyMaterial.mKeyRef;
            if (key_ref != 0)
            {
                sl_sec_man_unregister_ksu_key(key_ref);
            }
        }
    }
#endif // KSU_PRESENT
#endif // LPWAES
#endif // PSA crypto lib

    // Clear security material for all instances
    memset(sMacKeys, 0, sizeof(sMacKeys));

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    memset(sDefaultWakeKey, 0, sizeof(sDefaultWakeKey));
    memset(sGuestWakeKeys, 0, sizeof(sGuestWakeKeys));
#endif
}

static otError sli_ot_radio_security_finish_transmit(otRadioFrame      *aFrame,
                                                     instanceIndex_t    aIndex,
                                                     otMacKeyMaterial  *aKeyMaterial,
                                                     uint8_t            aKeyId,
                                                     volatile uint32_t *aFrameCounter)
{
    aFrame->mInfo.mTxInfo.mAesKey = aKeyMaterial;

    if (!aFrame->mInfo.mTxInfo.mIsHeaderUpdated)
    {
        uint32_t frameCounter;
        CORE_DECLARE_IRQ_STATE;

        CORE_ENTER_ATOMIC();
        frameCounter = (*aFrameCounter)++;

        // Store ack frame counter and ack key ID for receive frame.
        // Only update for MAC keys (key IDs 1-128); wake-key ACKs must not
        // overwrite the MAC ACK context read back by the receive path.
        if (otMacFrameIsAck(aFrame) && aKeyId < OT_MAC_FRAME_WAKE_KEY_INDEX)
        {
            sMacKeys[aIndex].ackKeyId        = aKeyId;
            sMacKeys[aIndex].ackFrameCounter = frameCounter;
        }

        CORE_EXIT_ATOMIC();

        otMacFrameSetKeyId(aFrame, aKeyId);
        otMacFrameSetFrameCounter(aFrame, frameCounter);
    }

    efr32PlatProcessTransmitAesCcm(aFrame, &sExtAddress[aIndex]);

    return OT_ERROR_NONE;
}

static otError sli_ot_radio_security_resolve_mac_transmit_key(otRadioFrame      *aFrame,
                                                              instanceIndex_t    aIndex,
                                                              uint8_t           *aKeyId,
                                                              otMacKeyMaterial **aKeyMaterial)
{
    otError error = OT_ERROR_NONE;
    uint8_t keyToUse;

    if (otMacFrameIsAck(aFrame))
    {
        *aKeyId = otMacFrameGetKeyId(aFrame);

        otEXPECT_ACTION(*aKeyId != 0, error = OT_ERROR_FAILED);

        if (*aKeyId == sMacKeys[aIndex].keyId - 1)
        {
            keyToUse = static_cast<uint8_t>(MacKeyType::PREV);
        }
        else if (*aKeyId == sMacKeys[aIndex].keyId)
        {
            keyToUse = static_cast<uint8_t>(MacKeyType::CURRENT);
        }
        else if (*aKeyId == sMacKeys[aIndex].keyId + 1)
        {
            keyToUse = static_cast<uint8_t>(MacKeyType::NEXT);
        }
        else
        {
            ExitNow(error = OT_ERROR_SECURITY);
        }
    }
    else
    {
        *aKeyId  = sMacKeys[aIndex].keyId;
        keyToUse = static_cast<uint8_t>(MacKeyType::CURRENT);
    }

    *aKeyMaterial = &sMacKeys[aIndex].keys[keyToUse];

exit:
    return error;
}

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

static sli_ot_wake_guest_key_t *sli_ot_radio_security_find_guest_wake_key_entry(instanceIndex_t aIndex,
                                                                                uint8_t         aKeyIndex)
{
    for (sli_ot_wake_guest_key_t &entry : sGuestWakeKeys[aIndex])
    {
        if (entry.mKeyIndex == aKeyIndex)
        {
            return &entry;
        }
    }

    return nullptr;
}

static otMacKeyMaterial *sli_ot_radio_security_lookup_wake_key_material(instanceIndex_t aIndex, uint8_t aKeyIndex)
{
    sli_ot_wake_guest_key_t *entry = nullptr;

    if (aKeyIndex == OT_MAC_FRAME_WAKE_KEY_INDEX)
    {
        return &sDefaultWakeKey[aIndex];
    }

    if (aKeyIndex < OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MIN || aKeyIndex > OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MAX)
    {
        return nullptr;
    }

    entry = sli_ot_radio_security_find_guest_wake_key_entry(aIndex, aKeyIndex);

    return (entry != nullptr) ? &entry->mKey : nullptr;
}

static bool sli_ot_radio_security_wake_key_is_registered(const otMacKeyMaterial *aWakeKey)
{
    static const uint8_t kZero[OT_MAC_KEY_SIZE] = {0};

    return memcmp(aWakeKey->mKeyMaterial.mKey.m8, kZero, OT_MAC_KEY_SIZE) != 0;
}

static otError sli_ot_radio_security_resolve_wake_transmit_key(instanceIndex_t    aIndex,
                                                               uint8_t            aKeyId,
                                                               otMacKeyMaterial **aKeyMaterial)
{
    otError           error   = OT_ERROR_NONE;
    otMacKeyMaterial *wakeKey = sli_ot_radio_security_lookup_wake_key_material(aIndex, aKeyId);

    otEXPECT_ACTION(wakeKey != nullptr && sli_ot_radio_security_wake_key_is_registered(wakeKey),
                    error = OT_ERROR_SECURITY);

    *aKeyMaterial = wakeKey;

exit:
    return error;
}

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

otError sli_ot_radio_security_process_transmit(otRadioFrame *aFrame, otInstance *aInstance)
{
    otError            error = OT_ERROR_NONE;
    uint8_t            keyId;
    otMacKeyMaterial  *keyMaterial   = nullptr;
    volatile uint32_t *frameCounter  = nullptr;
    instanceIndex_t    instanceIndex = sli_ot_radio_instance_get_index(aInstance);

    otEXPECT(otMacFrameIsSecurityEnabled(aFrame) && otMacFrameIsKeyIdMode1(aFrame)
             && !aFrame->mInfo.mTxInfo.mIsSecurityProcessed);

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    keyId = otMacFrameGetKeyId(aFrame);

    if (keyId >= OT_MAC_FRAME_WAKE_KEY_INDEX)
    {
        error = sli_ot_radio_security_resolve_wake_transmit_key(instanceIndex, keyId, &keyMaterial);

        if (error != OT_ERROR_NONE && aFrame->mInfo.mTxInfo.mAesKey != nullptr)
        {
            sli_ot_radio_security_store_wake_key_material(&sDefaultWakeKey[instanceIndex],
                                                          aFrame->mInfo.mTxInfo.mAesKey);
            keyMaterial = &sDefaultWakeKey[instanceIndex];
            error       = OT_ERROR_NONE;
        }

        SuccessOrExit(error);
        frameCounter = &sMacKeys[instanceIndex].macFrameCounter;
    }
    else
#endif
    {
        SuccessOrExit(error =
                          sli_ot_radio_security_resolve_mac_transmit_key(aFrame, instanceIndex, &keyId, &keyMaterial));

        frameCounter = &sMacKeys[instanceIndex].macFrameCounter;
    }

    error = sli_ot_radio_security_finish_transmit(aFrame, instanceIndex, keyMaterial, keyId, frameCounter);

exit:
    return error;
}

#ifdef LPWAES
#if defined(KSU_PRESENT)
static void sli_ot_radio_security_copy_key_to_ksu(instanceIndex_t index)
{
    // Declare all variables at the top of the function to prevent jumping over initialization
    int          prevIdx = static_cast<int>(MacKeyType::PREV);
    int          currIdx = static_cast<int>(MacKeyType::CURRENT);
    int          nextIdx = static_cast<int>(MacKeyType::NEXT);
    psa_key_id_t idPrev  = 0;
    psa_key_id_t idCurr  = 0;
    psa_key_id_t idNext  = 0;
    // Unregister old KSU keys before they are replaced
    for (int k = 0; k < static_cast<int>(MacKeyType::COUNT); ++k)
    {
        psa_key_id_t old_key_ref = sMacKeys[index].keys[k].mKeyMaterial.mKeyRef;
        if (old_key_ref != 0)
        {
            sl_sec_man_unregister_ksu_key(old_key_ref);
        }
    }
    // Copy previous, current and next keys to KSU using the generalized security manager API
    for (int k = 0; k < static_cast<int>(MacKeyType::COUNT); ++k)
    {
        psa_key_id_t source_key_id = sMacKeys[index].keys[k].mKeyMaterial.mKeyRef;
        psa_key_id_t ksu_key_id    = 0;
        uint8_t      ksu_slot      = 0xFF;

        psa_status_t status = sl_sec_man_copy_key_to_ksu(source_key_id, &ksu_key_id, &ksu_slot);

        if (status == PSA_SUCCESS && ksu_key_id != 0)
        {
            // Update the key reference to the new KSU key
            sMacKeys[index].keys[k].mKeyMaterial.mKeyRef = ksu_key_id;
            sMacKeys[index].ksuSlot[k]                   = ksu_slot;
        }
        else
        {
            // Failed to copy to KSU - this is unexpected, assert in debug builds
            OT_ASSERT(status == PSA_SUCCESS);
        }
    }

    // Get PSA key ids and verify they are unique.
    idPrev = sMacKeys[index].keys[prevIdx].mKeyMaterial.mKeyRef;
    idCurr = sMacKeys[index].keys[currIdx].mKeyMaterial.mKeyRef;
    idNext = sMacKeys[index].keys[nextIdx].mKeyMaterial.mKeyRef;
    // If any two PSA key ids are equal, that's unexpected — fail loudly in
    // debug builds so the issue can be investigated.
    if (idPrev == idCurr || idPrev == idNext || idCurr == idNext)
    {
        // Duplicate PSA key id detected when copying keys to KSU. This is
        // unexpected — assert so the issue can be investigated.
        OT_ASSERT(false);
    }
}
#endif // KSU_PRESENT
#endif // LPWAES
void sli_ot_radio_security_set_mac_key(otInstance             *aInstance,
                                       uint8_t                 aKeyIdMode,
                                       uint8_t                 aKeyId,
                                       const otMacKeyMaterial *aPrevKey,
                                       const otMacKeyMaterial *aCurrKey,
                                       const otMacKeyMaterial *aNextKey,
                                       otRadioKeyType          aKeyType)
{
    OT_UNUSED_VARIABLE(aKeyIdMode);
    OT_UNUSED_VARIABLE(aKeyType);

    instanceIndex_t index = sli_ot_radio_instance_get_index(aInstance);

    otEXPECT(sl_ot_rtos_task_can_access_pal());
    OT_ASSERT(aPrevKey != nullptr && aCurrKey != nullptr && aNextKey != nullptr);

    // MAC frame counters are reset before updating keys. This order
    // safeguards against issues that can arise when the radio
    // platform handles TX security and counter assignment.  The
    // radio platform might prepare an enhanced ACK to a received
    // frame from an parallel (e.g., ISR) context, which consumes
    // a MAC frame counter value.
    //
    // If the MAC key is updated before the frame counter is cleared,
    // the radio could receive and send an enhanced ACK between these
    // two actions, possibly using the new MAC key with a larger
    // (current) frame counter value. This could then prevent the
    // receiver from accepting subsequent transmissions after the
    // frame counter reset for a long time.
    //
    // While resetting counters first might briefly cause an enhanced
    // ACK to be sent with the old key and a zero counter (which might
    // be rejected by the receiver), this is a transient issue that
    // quickly resolves itself.
    sli_ot_radio_security_set_mac_frame_counter(aInstance, 0);

    sMacKeys[index].keyId = aKeyId;
    memcpy(&sMacKeys[index].keys[static_cast<int>(MacKeyType::PREV)], aPrevKey, sizeof(otMacKeyMaterial));
    memcpy(&sMacKeys[index].keys[static_cast<int>(MacKeyType::CURRENT)], aCurrKey, sizeof(otMacKeyMaterial));
    memcpy(&sMacKeys[index].keys[static_cast<int>(MacKeyType::NEXT)], aNextKey, sizeof(otMacKeyMaterial));
    // Reset recorded KSU slot markers for the new keys.
    for (int k = 0; k < static_cast<int>(MacKeyType::COUNT); ++k)
    {
        sMacKeys[index].ksuSlot[k] = 0xFF;
    }

#if (OPENTHREAD_CONFIG_CRYPTO_LIB == OPENTHREAD_CONFIG_CRYPTO_LIB_PSA)
    // Under LPWAES: copy current key into KSU (if available) using psa_copy_key, export only prev/next.
    // Otherwise export all three keys as before.
#if defined(LPWAES) && defined(KSU_PRESENT)
    sli_ot_radio_security_copy_key_to_ksu(index);
#else  // !LPWAES || !KSU_PRESENT
    size_t  aKeyLen;
    otError error;

    error = otPlatCryptoExportKey(sMacKeys[index].keys[static_cast<int>(MacKeyType::PREV)].mKeyMaterial.mKeyRef,
                                  sMacKeys[index].keys[static_cast<int>(MacKeyType::PREV)].mKeyMaterial.mKey.m8,
                                  sizeof(sMacKeys[index].keys[static_cast<int>(MacKeyType::PREV)]),
                                  &aKeyLen);
    OT_ASSERT(error == OT_ERROR_NONE);

    error = otPlatCryptoExportKey(sMacKeys[index].keys[static_cast<int>(MacKeyType::CURRENT)].mKeyMaterial.mKeyRef,
                                  sMacKeys[index].keys[static_cast<int>(MacKeyType::CURRENT)].mKeyMaterial.mKey.m8,
                                  sizeof(sMacKeys[index].keys[static_cast<int>(MacKeyType::CURRENT)]),
                                  &aKeyLen);
    OT_ASSERT(error == OT_ERROR_NONE);

    error = otPlatCryptoExportKey(sMacKeys[index].keys[static_cast<int>(MacKeyType::NEXT)].mKeyMaterial.mKeyRef,
                                  sMacKeys[index].keys[static_cast<int>(MacKeyType::NEXT)].mKeyMaterial.mKey.m8,
                                  sizeof(sMacKeys[index].keys[static_cast<int>(MacKeyType::NEXT)]),
                                  &aKeyLen);
    OT_ASSERT(error == OT_ERROR_NONE);
#endif // LPWAES
#endif // PSA crypto lib

exit:
    return;
}

void sli_ot_radio_security_set_mac_frame_counter(otInstance *aInstance, uint32_t aMacFrameCounter)
{
    instanceIndex_t index = sli_ot_radio_instance_get_index(aInstance);

    otEXPECT(sl_ot_rtos_task_can_access_pal());

    CORE_DECLARE_IRQ_STATE;
    CORE_ENTER_ATOMIC();

    sMacKeys[index].macFrameCounter = aMacFrameCounter;

    CORE_EXIT_ATOMIC();

exit:
    return;
}

void sli_ot_radio_security_set_mac_frame_counter_if_larger(otInstance *aInstance, uint32_t aMacFrameCounter)
{
    instanceIndex_t index = sli_ot_radio_instance_get_index(aInstance);
    otEXPECT(sl_ot_rtos_task_can_access_pal());

    CORE_DECLARE_IRQ_STATE;
    CORE_ENTER_ATOMIC();

    if (aMacFrameCounter > sMacKeys[index].macFrameCounter)
    {
        sMacKeys[index].macFrameCounter = aMacFrameCounter;
    }

    CORE_EXIT_ATOMIC();

exit:
    return;
}

uint8_t sli_ot_radio_security_get_ack_key_id(otInstance *aInstance)
{
    instanceIndex_t index = sli_ot_radio_instance_get_index(aInstance);
    return sMacKeys[index].ackKeyId;
}

uint32_t sli_ot_radio_security_get_ack_frame_counter(otInstance *aInstance)
{
    instanceIndex_t index = sli_ot_radio_instance_get_index(aInstance);
    return sMacKeys[index].ackFrameCounter;
}

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

// Exports PSA key bytes into the material's literal key field so the platform
// AES-CCM path can access raw bytes. No-op on non-PSA builds.
static void sli_wake_key_export_psa(otMacKeyMaterial *aDest)
{
#if (OPENTHREAD_CONFIG_CRYPTO_LIB == OPENTHREAD_CONFIG_CRYPTO_LIB_PSA)
    size_t  keyLen;
    otError error = otPlatCryptoExportKey(aDest->mKeyMaterial.mKeyRef,
                                          aDest->mKeyMaterial.mKey.m8,
                                          sizeof(aDest->mKeyMaterial.mKey.m8),
                                          &keyLen);
    OT_ASSERT(error == OT_ERROR_NONE);
#else
    OT_UNUSED_VARIABLE(aDest);
#endif
}

static void sli_ot_radio_security_store_wake_key_material(otMacKeyMaterial *aDest, const otMacKeyMaterial *aWakeKey)
{
    if (aWakeKey != nullptr)
    {
        memcpy(aDest, aWakeKey, sizeof(otMacKeyMaterial));
        sli_wake_key_export_psa(aDest);
    }
    else
    {
        memset(aDest, 0, sizeof(otMacKeyMaterial));
    }
}

static void sli_ot_radio_security_set_default_wake_key(instanceIndex_t aIndex, const otMacKeyMaterial *aWakeKey)
{
    sli_ot_radio_security_store_wake_key_material(&sDefaultWakeKey[aIndex], aWakeKey);
}

static void sli_ot_radio_security_set_guest_wake_key(instanceIndex_t         aIndex,
                                                     uint8_t                 aKeyIndex,
                                                     const otMacKeyMaterial *aWakeKey)
{
    sli_ot_wake_guest_key_t *entry = sli_ot_radio_security_find_guest_wake_key_entry(aIndex, aKeyIndex);

    if (entry != nullptr)
    {
        if (aWakeKey != nullptr)
        {
            sli_ot_radio_security_store_wake_key_material(&entry->mKey, aWakeKey);
        }
        else
        {
            memset(entry, 0, sizeof(*entry));
        }

        return;
    }

    if (aWakeKey == nullptr)
    {
        return;
    }

    for (sli_ot_wake_guest_key_t &slot : sGuestWakeKeys[aIndex])
    {
        if (slot.mKeyIndex != 0)
        {
            continue;
        }

        slot.mKeyIndex = aKeyIndex;
        sli_ot_radio_security_store_wake_key_material(&slot.mKey, aWakeKey);
        return;
    }
}

void sli_ot_radio_security_set_wake_key(otInstance *aInstance, uint8_t aKeyIndex, const otMacKeyMaterial *aWakeKey)
{
    instanceIndex_t index = sli_ot_radio_instance_get_index(aInstance);

    otEXPECT(sl_ot_rtos_task_can_access_pal());
    otEXPECT_ACTION(aKeyIndex >= OT_MAC_FRAME_WAKE_KEY_INDEX && aKeyIndex <= OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MAX,
                    /* no-op */);

    if (aKeyIndex == OT_MAC_FRAME_WAKE_KEY_INDEX)
    {
        sli_ot_radio_security_set_default_wake_key(index, aWakeKey);
    }
    else
    {
        sli_ot_radio_security_set_guest_wake_key(index, aKeyIndex, aWakeKey);
    }

exit:
    return;
}
#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

} // extern

#endif // (OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2)
