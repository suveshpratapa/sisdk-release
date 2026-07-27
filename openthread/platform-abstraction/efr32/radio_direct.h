/*******************************************************************************
 * @file
 * @brief Thread Direct platform abstraction — EFR32 internal declarations.
 *
 * Private interface between radio.cpp (RAIL state machine) and
 * radio_direct.cpp (Thread Direct platform API implementations).
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

#ifndef RADIO_DIRECT_H
#define RADIO_DIRECT_H

#include <openthread-core-config.h>

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#include <stdint.h>
#include <openthread/instance.h>
#include <openthread/platform/radio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum byte length of the Thread Direct Enh-ACK Thread Header IE payload
 * (Challenge LTV echo plus optional SCA LTV).
 */
#define SLI_OT_RADIO_DIRECT_ENH_ACK_IE_MAX_SIZE 64U

/**
 * Generates the Thread Header IE for the Enh-ACK to a TD Link Command by
 * parsing the Challenge LTV directly from the already-received bytes of
 * @p aReceivedFrame and building the verbatim echo IE inline.
 *
 * The RAIL spin-poll in radio.cpp guarantees that all Challenge LTV bytes
 * are present in @p aReceivedFrame->mPsdu before this function is called.
 *
 * Called from the time-critical generateAckIeData() path in radio.cpp during
 * the RAIL SL_RAIL_EVENT_IEEE802154_DATA_REQUEST_COMMAND handler.
 *
 * @param[in]  aInstance       OT instance (unused; may be nullptr).
 * @param[in]  aReceivedFrame  Incoming MAC frame with sufficient bytes buffered.
 * @param[out] aIeData         Destination IE buffer; @p aAvailable bytes of space.
 * @param[in]  aAvailable      Bytes available at @p aIeData.
 *
 * @return  Number of bytes written; 0 when no Thread Header IE with a
 *          Challenge LTV is found in @p aReceivedFrame.
 */
uint8_t sli_ot_radio_direct_generate_enh_ack_ie_data(otInstance   *aInstance,
                                                     otRadioFrame *aReceivedFrame,
                                                     uint8_t      *aIeData,
                                                     uint8_t       aAvailable);

/**
 * Returns whether the local SLW schedule is active (period != 0).
 *
 * Called by radio.cpp before building each outgoing frame to decide whether
 * to populate the sRadioContext SLW fields for otMacFrameProcessTxSfd.
 */
bool sli_ot_radio_direct_slw_is_present(otInstance *aInstance);

/**
 * Returns the SLW period in slot-duration units, as set by
 * otPlatRadioSetThreadDirectSlwSchedule.
 */
uint16_t sli_ot_radio_direct_slw_get_period(otInstance *aInstance);

/**
 * Returns the next SLW sample time in us, as updated by otPlatRadioUpdateThreadDirectSlwSampleTime.
 */
uint32_t sli_ot_radio_direct_slw_get_sample_time(otInstance *aInstance);

/**
 * Computes the outgoing SCA LTV SLW phase and RAM offset for a frame whose MAC
 * header starts at @p aMacHeaderTxTime (us).
 *
 * The stored next SLW sample time is first normalized forward within the
 * configured SLW period so it represents the next sample point relative to the
 * outgoing frame anchor. The resulting time delta is then split into a slot
 * phase and a signed residual as RAM offset.
 *
 * Called when stamping the SCA LTV before transmission.
 *
 * @param[in]  aInstance         OT instance.
 * @param[in]  aMacHeaderTxTime  Radio timestamp (us) of the frame's MAC header start.
 * @param[out] aPhaseSlots       Computed SLW phase in slot-duration units, or NULL.
 * @param[out] aRamOffsetUs      Computed RAM offset in us ranged [-1024, 1023], or NULL.
 *
 * @retval true   Phase and RAM offset were computed successfully.
 * @retval false  SLW is disabled or the computed values could not be encoded.
 */
bool sli_ot_radio_direct_slw_get_phase_and_ram_offset(otInstance *aInstance,
                                                      uint32_t    aMacHeaderTxTime,
                                                      uint16_t   *aPhaseSlots,
                                                      int16_t    *aRamOffsetUs);

#ifdef __cplusplus
}
#endif

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#endif // RADIO_DIRECT_H
