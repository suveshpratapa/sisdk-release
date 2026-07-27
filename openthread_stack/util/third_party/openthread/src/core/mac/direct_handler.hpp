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
 *   This file includes definitions for the Thread Direct handler.
 *
 *   Implements the Wake Initiator (WI) and Wake Listener (WL) link handshake
 *   state machines.  The WL state machine drives the TD Link Command TX path;
 *   the WI state machine processes incoming TD Link Commands and arms the
 *   Enh-ACK Thread Header IE.
 */

#ifndef OT_CORE_MAC_DIRECT_HANDLER_HPP_
#define OT_CORE_MAC_DIRECT_HANDLER_HPP_

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#include "common/error.hpp"
#include "common/locator.hpp"
#include "common/non_copyable.hpp"
#include "common/timer.hpp"
#include "mac/mac_frame.hpp"
#include "mac/mac_header_ltv.hpp"
#include "mac/mac_links.hpp"
#include "mac/mac_types.hpp"

namespace ot {

class DirectPeer;

/**
 * Manages the Thread Direct link handshake for both the Wake Initiator and
 * Wake Listener roles, and the local SCA state (SLW schedule and RAM
 * parameters) advertised in outgoing SCA LTVs.
 */
class DirectHandler : public InstanceLocator, private NonCopyable
{
public:
    explicit DirectHandler(Instance &aInstance);

    /**
     * Sets the SLW (Scheduled Listen Window) period advertised by this device.
     *
     * Phase is stack-computed at frame-build time and is not configurable here.
     *
     * @param[in] aPeriodSlots  SLW period in units of advertised Slot
     *                          Duration (0 = clear schedule / rx-on-when-idle).
     *
     * @retval kErrorNone  Schedule stored.
     */
    Error SetSlwSchedule(uint16_t aPeriodSlots);

    /**
     * Gets the SLW (Scheduled Listen Window) period configured on this device.
     *
     * @param[out] aPeriodSlots  Set to the SLW period in units of advertised
     *                           Slot Duration (0 = clear schedule / rx-on-when-idle).
     */
    void GetSlwSchedule(uint16_t &aPeriodSlots) const;

    /**
     * Sets the RAM (Radio Availability Mask) parameters advertised by this device.
     *
     * @param[in] aParams  RAM parameters to store.
     *
     * @retval kErrorNone         Parameters stored.
     * @retval kErrorInvalidArgs  @p aParams.mRamOffsetUs is outside [-1024, 1023].
     */
    Error SetRamMask(const Mac::ScaParams &aParams);

    /**
     * Gets the RAM (Radio Availability Mask) parameters configured on this device.
     *
     * @param[out] aParams  Set to the RAM parameters.
     */
    void GetRamMask(Mac::ScaParams &aParams) const;

    /**
     * Gets the local SCA state (SLW schedule and RAM parameters).
     *
     * Computes the SLW phase from the running SubMac SLW schedule at the moment of the
     * call.  Phase is 0 when SLW is not yet active (e.g. before link establishment).
     *
     * @returns A const reference to the local SCA parameters with an up-to-date phase.
     */
    const Mac::ScaParams &GetLocalSca(void);

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    /**
     * Sends a post-link SCA parameter update to @p aPeerAddr.
     *
     * Transmits a TD Link Command carrying the current local SCA LTV (with a dynamically
     * computed phase) to the peer.  The peer uses the updated phase to resynchronise its
     * transmit schedule to this device's actual SLW windows.
     *
     * The peer must already be in `kInStateValid` (link established).
     *
     * @param[in] aPeerAddr  Extended address of the linked peer.
     *
     * @retval kErrorNone      Update queued.
     * @retval kErrorNotFound  No established link to @p aPeerAddr.
     */
    Error SendScaUpdate(const Mac::ExtAddress &aPeerAddr);

    /**
     * Tears down the TD link with @p aExtAddress.
     *
     * Removes the `DirectPeer` entry, fires `OT_THREAD_DIRECT_EVENT_UNLINKED`, and schedules
     * a best-effort teardown frame (empty SCA LTV).
     *
     * @param[in] aExtAddress  Extended address of the peer to unlink.
     *
     * @retval kErrorNone      Teardown initiated.
     * @retval kErrorNotFound  No active link to @p aExtAddress.
     */
    Error Unlink(const Mac::ExtAddress &aExtAddress);

    /**
     * Builds the Thread Direct teardown frame in @p aTxFrames.
     *
     * Called by `Mac::BeginTransmit` for `kOperationTransmitTdTeardown`.
     *
     * @param[in,out] aTxFrames  MAC TX frame set.
     *
     * @returns Pointer to the prepared `TxFrame`, or `nullptr` on error.
     */
    Mac::TxFrame *PrepareTeardownFrame(Mac::TxFrames &aTxFrames);

    /**
     * Maximum consecutive un-acked link supervision probes before a peer is unlinked.
     */
    static constexpr uint8_t kMaxSupervisionFailures = 7;

    /**
     * Builds the Thread Direct link supervision probe in @p aTxFrames.
     *
     * Called by `Mac::BeginTransmit` for `kOperationTransmitTdSupervision`.
     *
     * @param[in,out] aTxFrames  MAC TX frame set.
     *
     * @returns Pointer to the prepared `TxFrame`, or `nullptr` on error.
     */
    Mac::TxFrame *PrepareSupervisionFrame(Mac::TxFrames &aTxFrames);

    /**
     * Called by `Mac::HandleTransmitDone` after a link supervision probe TX completes.
     *
     * On ACK, resets the peer's probe attempt count and idle clock. On failure, retries
     * at the peer's next SLW window, or unlinks the peer once `kMaxSupervisionFailures`
     * consecutive attempts have failed.
     *
     * @param[in] aFrame  The transmitted supervision probe frame.
     * @param[in] aError  TX result (`kErrorNone` on success).
     */
    void HandleSupervisionTxDone(Mac::TxFrame &aFrame, Error aError);
#endif

    /**
     * Indicates whether a local SLW schedule is configured.
     *
     * @retval TRUE   Local SLW scheduling is configured.
     * @retval FALSE  Local SLW scheduling is disabled.
     */
    bool HasSlwSchedule(void) const { return mSlwPeriodUs != 0; }

    /**
     * Gets the configured local SLW Slot Duration in microseconds.
     *
     * @returns The configured Slot Duration in microseconds.
     */
    uint32_t GetSlwSlotDurationUs(void) const { return mSlwSlotDurationUs; }

    /**
     * Gets the configured local SLW period in microseconds.
     *
     * @returns The configured SLW period in microseconds.
     */
    uint64_t GetSlwPeriodUs(void) const { return mSlwPeriodUs; }

    /**
     * Returns the local Thread Direct link supervision interval, in milliseconds.
     *
     * This is the interval this device advertises to a peer in the TD Link Command. The
     * interval that actually governs a given link is the minimum of the two peers' advertised
     * values; see `DirectPeer::GetSupervisionIntervalMs()` for the peer-advertised side.
     *
     * @returns Current local supervision interval, in milliseconds.
     */
    uint32_t GetSlwTimeout(void) const { return mSlwTimeout; }

    /**
     * Sets the local Thread Direct link supervision interval, in milliseconds.
     *
     * @param[in] aTimeout  Interval in milliseconds; 0 imposes no local requirement, deferring
     *                      entirely to the peer's advertised interval.
     *
     * @retval kErrorNone         Interval stored.
     * @retval kErrorInvalidArgs  @p aTimeout exceeds kMaxSlwTimeout.
     */
    Error SetSlwTimeout(uint32_t aTimeout);

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    /**
     * Called by `Mac::HandleWakeupFrame` after a TD Wake Command is received.
     *
     * @param[in] aWakeupInfo  Decoded fields from the received Wake Command.
     */
    void HandleWakeReceived(const Mac::WakeupInfo &aWakeupInfo);
#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    /**
     * Called by `Mac::BeginTransmit` for `kOperationTransmitTdLinkCmd`.
     *
     * Handles both the WL path (WL sends its TD Link Command with Challenge LTV and SCA LTV)
     * and the WI path (WI sends its own TD Link Command with SCA LTV only, scheduled at the
     * WL's SLW window).
     *
     * @param[in,out] aTxFrames  The MAC TX frame set.
     *
     * @returns A pointer to the prepared `TxFrame`, or `nullptr` on error.
     */
    Mac::TxFrame *PrepareTdLinkCmdFrame(Mac::TxFrames &aTxFrames);

    /**
     * Called by `Mac::HandleTransmitDone` after the TD Link Command TX completes.
     *
     * Handles both the WL path (verifies echoed Challenge in Enh-ACK) and the WI path
     * (verifies Enh-ACK was received, then fires `OT_THREAD_DIRECT_EVENT_LINKED`).
     *
     * @param[in] aFrame     The transmitted TD Link Command frame.
     * @param[in] aAckFrame  The received Enh-ACK frame, or `nullptr` if none.
     * @param[in] aError     TX result (`kErrorNone` on success).
     */
    void HandleTdLinkCmdTxDone(Mac::TxFrame &aFrame, Mac::RxFrame *aAckFrame, Error aError);

    /**
     * Called by `Mac::HandleTransmitDone` after the TD Teardown TX completes.
     *
     * Handles the teardown frame TX completion.
     *
     * @param[in] aFrame     The transmitted TD Teardown frame.
     * @param[in] aError     TX result (`kErrorNone` on success).
     */
    void HandleTdTeardownTxDone(Mac::TxFrame &aFrame, Error aError);
#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
    /**
     * Called by `Mac::HandleMacCommand` when a TD Link Command is received.
     *
     * Arms the Enh-ACK IE immediately (platform has ~192 us to inject it into
     * the hardware ACK).  An empty SCA LTV in the payload is treated as teardown.
     *
     * @param[in] aFrame  The received TD Link Command frame (already decrypted).
     */
    void HandleTdLinkCommand(const Mac::RxFrame &aFrame);
#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE && !OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
    /**
     * Called by `Mac::HandleMacCommand` for TD Direct frames (MAC Cmd 0x54 /
     * Thread Cmd 0x02) in WL-only builds.
     *
     * Handles teardown frames and incoming WI TD Link Commands (which carry the WI's
     * SCA LTV so the WL can update the peer's schedule).
     *
     * @param[in] aFrame  The received frame (already decrypted).
     */
    void HandleTdDirectFrame(const Mac::RxFrame &aFrame);
#endif

private:
    static constexpr uint32_t kDefaultSlwTimeout = OPENTHREAD_CONFIG_THREAD_DIRECT_SLW_TIMEOUT;
    static constexpr uint32_t kMaxSlwTimeout     = OPENTHREAD_CONFIG_THREAD_DIRECT_SLW_MAX_TIMEOUT;

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    void HandleTeardownRxd(const Mac::ExtAddress &aPeerAddr);

    // Minimum supervision probe retry delay. A peer with no SLW schedule (or one
    // shorter than 1 ms) would otherwise compute a retry delay of zero, causing all
    // `kMaxSupervisionFailures` attempts to fire back-to-back instead of being paced.
    static constexpr uint32_t kMinSupervisionRetryDelayMs = 10;

    uint32_t  GetEffectiveSupervisionIntervalMs(const DirectPeer &aPeer) const;
    uint32_t  GetSupervisionRetryDelayMs(const DirectPeer &aPeer) const;
    TimeMilli GetSupervisionDeadline(const DirectPeer &aPeer, uint32_t aIntervalMs) const;
    void      RequestSupervisionProbe(DirectPeer &aPeer);
    void      DetermineNextSupervisionFireTime(void);
    void      HandleSupervisionTimer(void);

    using SupervisionTimer = TimerMilliIn<DirectHandler, &DirectHandler::HandleSupervisionTimer>;
#endif

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
    enum WiState : uint8_t
    {
        kWiIdle,             ///< No WI-initiated TD Link Command in progress.
        kWiSendingTdLinkCmd, ///< WI's own TD Link Command TX pending or sent; waiting for Enh-ACK.
    };

    WiState         mWiState;
    Mac::ExtAddress mWiPendingPeerAddr;
#endif

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    static constexpr uint32_t kWlPeerTdLinkCmdTimeoutPeriods =
        OPENTHREAD_CONFIG_THREAD_DIRECT_WL_PEER_TD_LINK_CMD_TIMEOUT_PERIODS;

    // Minimum peer TD Link Command wait, in milliseconds. A WL with no SLW schedule
    // (SLW Period 0, i.e. rx-on-when-idle) would otherwise compute a near-zero timeout.
    static constexpr uint32_t kMinWlPeerTdLinkCmdTimeoutMs = 10;

    enum WlState : uint8_t
    {
        kWlIdle,                 ///< No handshake in progress.
        kWlAttachDelay,          ///< Waiting for rendezvous time before sending TD Link Command.
        kWlWaitingEnhAck,        ///< TD Link Command TX pending or sent; waiting for Enh-ACK.
        kWlWaitingPeerTdLinkCmd, ///< Challenge was echoed; waiting for the peer's TD Link Command.
    };

    void HandleWlStateTimer(void);
    void StartWlPeerTdLinkCmdTimeout(void);
    void ResumeWakeListening(void);
    bool TryAddWlPeer(const Mac::ExtAddress &aPeerAddr, const Mac::ScaParams &aPeerSca, uint64_t aRxTimestamp);

    using WlStateTimer = TimerMilliIn<DirectHandler, &DirectHandler::HandleWlStateTimer>;

    WlState           mWlState;
    Mac::WakeupInfo   mWakeupInfo;
    Mac::ChallengeLtv mWlChallenge;
    WlStateTimer      mWlStateTimer;
#endif
    void UpdateDerivedSlwTiming(void);

    Mac::ScaParams mLocalSca;
    uint32_t       mSlwSlotDurationUs;
    uint64_t       mSlwPeriodUs;
    uint32_t       mSlwTimeout;
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    bool            mScaUpdatePending;
    bool            mScaUpdateInFlight;
    Mac::ExtAddress mScaUpdatePeerAddr;
    Mac::ExtAddress mTeardownAddr;
    uint8_t         mTeardownKeyIndex;
    bool            mTeardownPending;
    // SCA snapshot taken in Unlink() before the peer entry is cleared, used to
    // schedule the teardown frame to the peer's SLW window in PrepareTeardownFrame.
    uint64_t mTeardownLastScaRxTs;
    uint64_t mTeardownSlwPeriodUs;
    uint64_t mTeardownSlwPhaseUs;

    SupervisionTimer mSupervisionTimer;
    Mac::ExtAddress  mSupervisionAddr;
    uint8_t          mSupervisionKeyIndex;
    bool             mSupervisionPending;
#endif
};

} // namespace ot

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#endif // OT_CORE_MAC_DIRECT_HANDLER_HPP_
