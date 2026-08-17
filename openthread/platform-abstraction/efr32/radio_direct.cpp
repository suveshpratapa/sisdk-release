/*******************************************************************************
 * @file
 * @brief EFR32 platform implementation of Thread Direct radio APIs.
 *
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#include "radio_direct.h"

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#include <string.h>
#include <openthread/link.h>
#include <openthread/platform/alarm-micro.h>
#include <openthread/platform/radio.h>
#include <openthread/platform/thread_direct.h>
#include "common/code_utils.hpp"
#include "common/debug.hpp"
#include "utils/code_utils.h"
#include "utils/mac_frame.h"

#include "platform-efr32.h"
#include "radio_instance.h"
#include "radio_security.h"

// Per-instance SLW (Scheduled Listen Window) state, mirroring the CSL pattern in radio_csl.cpp.
typedef struct
{
    uint32_t sampleTime;     // Next expected SLW frame arrival time in us.
    uint32_t slotDurationUs; // SLW slot duration in us.
    uint16_t period; // SLW period in units of the advertised Slot Duration (0 = clear schedule / rx-on-when-idle).
} slw_state_t;

static slw_state_t sSlwState[RADIO_INTERFACE_COUNT];

static slw_state_t *getSlwState(otInstance *aInstance)
{
    slw_state_t *state;

#if OPENTHREAD_CONFIG_MULTIPLE_INSTANCE_ENABLE
    instanceIndex_t index = sli_ot_radio_instance_get_index(aInstance);
    OT_ASSERT(index < RADIO_INTERFACE_COUNT);
    state = &sSlwState[index];
#else
    OT_UNUSED_VARIABLE(aInstance);
    state = &sSlwState[0];
#endif

    return state;
}

void otPlatRadioSetWakeKey(otInstance *aInstance, uint8_t aKeyIndex, const otMacKeyMaterial *aWakeKey)
{
    sli_ot_radio_security_set_wake_key(aInstance, aKeyIndex, aWakeKey);
}

// Called from the time-critical RAIL data-request callback within the
// 192 us IEEE 802.15.4 ACK turnaround window.
uint8_t sli_ot_radio_direct_generate_enh_ack_ie_data(otInstance   *aInstance,
                                                     otRadioFrame *aReceivedFrame,
                                                     uint8_t      *aIeData,
                                                     uint8_t       aAvailable)
{
    otMacFrameThreadDirectSca sca;

    memset(&sca, 0, sizeof(sca));

    if ((aInstance != nullptr) && sli_ot_radio_direct_slw_is_present(aInstance))
    {
        uint32_t now = otPlatAlarmMicroGetNow();

        sca.mHasSlw        = true;
        sca.mSlwPeriod     = sli_ot_radio_direct_slw_get_period(aInstance);
        sca.mClockAccuracy = otPlatRadioGetThreadDirectSlwAccuracy(aInstance);
        sca.mUncertainty   = otPlatRadioGetThreadDirectSlwUncertainty(aInstance);
        sli_ot_radio_direct_slw_get_phase_and_ram_offset(aInstance, now, &sca.mSlwPhase, &sca.mRamOffsetUs);
    }

    return otMacFrameGenerateThreadDirectEnhAckIe(aReceivedFrame, aIeData, aAvailable, &sca);
}

otError otPlatRadioSetThreadDirectSlwSchedule(otInstance *aInstance, uint16_t aSlwPeriod, uint32_t aSlotDurationUs)
{
    slw_state_t *state    = getSlwState(aInstance);
    state->period         = aSlwPeriod;
    state->slotDurationUs = aSlotDurationUs;

    return OT_ERROR_NONE;
}

void otPlatRadioUpdateThreadDirectSlwSampleTime(otInstance *aInstance, uint32_t aSlwSampleTime)
{
    slw_state_t *state = getSlwState(aInstance);
    state->sampleTime  = aSlwSampleTime;
}

bool sli_ot_radio_direct_slw_is_present(otInstance *aInstance)
{
    return getSlwState(aInstance)->period != 0;
}

uint16_t sli_ot_radio_direct_slw_get_period(otInstance *aInstance)
{
    return getSlwState(aInstance)->period;
}

uint32_t sli_ot_radio_direct_slw_get_sample_time(otInstance *aInstance)
{
    return getSlwState(aInstance)->sampleTime;
}

bool sli_ot_radio_direct_slw_get_phase_and_ram_offset(otInstance *aInstance,
                                                      uint32_t    aMacHeaderTxTime,
                                                      uint16_t   *aPhaseSlots,
                                                      int16_t    *aRamOffsetUs)
{
    slw_state_t *state       = getSlwState(aInstance);
    uint16_t     phase       = 0;
    int16_t      ramOffsetUs = 0;
    bool         success     = false;

    if (state->period != 0)
    {
        success = otMacFrameCalculateSlwPhaseAndRamOffset(state->sampleTime,
                                                          aMacHeaderTxTime,
                                                          state->period,
                                                          state->slotDurationUs,
                                                          &phase,
                                                          &ramOffsetUs);
    }

    if (aPhaseSlots != nullptr)
    {
        *aPhaseSlots = phase;
    }

    if (aRamOffsetUs != nullptr)
    {
        *aRamOffsetUs = ramOffsetUs;
    }
    return success;
}

void sli_ot_radio_direct_update_enh_ack_ie(otInstance *aInstance, otRadioFrame *aEnhAckFrame, uint32_t aAckShrDoneTime)
{
    uint16_t phase;
    int16_t  ramOffsetUs;

    otEXPECT(aInstance != nullptr && aEnhAckFrame != nullptr);
    otEXPECT(sli_ot_radio_direct_slw_is_present(aInstance));
    otEXPECT(otMacFrameHasThreadDirectScaLtv(aEnhAckFrame));
    otEXPECT(sli_ot_radio_direct_slw_get_phase_and_ram_offset(aInstance, aAckShrDoneTime, &phase, &ramOffsetUs));

    otMacFrameSetThreadDirectScaLtv(aEnhAckFrame, sli_ot_radio_direct_slw_get_period(aInstance), phase, ramOffsetUs);

exit:
    return;
}

// Must be strong: the stack's radio_platform.cpp stubs are weak and return 255.
// Delegates to CSL; both use the same oscillator.
uint8_t otPlatRadioGetThreadDirectSlwAccuracy(otInstance *aInstance)
{
    return otPlatRadioGetCslAccuracy(aInstance);
}

uint8_t otPlatRadioGetThreadDirectSlwUncertainty(otInstance *aInstance)
{
    return otPlatRadioGetCslUncertainty(aInstance);
}

OT_TOOL_WEAK otError otPlatRadioGetThreadDirectRamParams(otInstance *aInstance, otThreadDirectRamParams *aParams)
{
    OT_UNUSED_VARIABLE(aInstance);
    OT_UNUSED_VARIABLE(aParams);
    return OT_ERROR_NOT_IMPLEMENTED;
}

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
