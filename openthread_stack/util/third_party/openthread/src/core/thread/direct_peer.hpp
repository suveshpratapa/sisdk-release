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
 *   This file includes definitions for a Thread Direct `DirectPeer`.
 */

#ifndef OT_CORE_THREAD_DIRECT_PEER_HPP_
#define OT_CORE_THREAD_DIRECT_PEER_HPP_

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#include "thread/neighbor.hpp"

namespace ot {

/**
 * Represents a Thread Direct peer and its link state established during the
 * TD handshake.
 */
class DirectPeer : public CslNeighbor
{
public:
    /**
     * Maximum number of re-transmitted TD link teardown frames.
     */
    static constexpr uint8_t kMaxRetransmitLinkTearDowns = 4;

    /**
     * Initializes the `DirectPeer` object.
     *
     * @param[in] aInstance  The OpenThread instance.
     */
    void Init(Instance &aInstance)
    {
        Mac::ScaParams sca = Mac::ScaParams();

        Neighbor::Init(aInstance);
        mTearDownCount            = 0;
        mWakeKeyUsed              = false;
        mCoexEnabled              = false;
        mHasScaSchedule           = false;
        mWakeKeyIndex             = 0;
        mSlwPeriodSlots           = 0;
        mSlwPhaseSlots            = 0;
        mSupervisionIntervalMs    = 0;
        mServicesBitmap           = 0;
        mLastWakeFrameCounter     = 0;
        mSupervisionProbeAttempts = 0;
        mSupervisionProbePending  = false;
        sca.mSlotDuration         = Mac::ScaSlotDuration::k625Usec;
        sca.mRamAvailable         = false;
        SetSca(sca);
        mSlwAccuracy.Init();
        mLastScaRxTimestamp          = 0;
        mLastActivityTime            = TimerMilli::GetNow();
        mLastActivityRadioUs         = 0;
        mLastSupervisionProbeTime    = TimerMilli::GetNow();
        mLastSupervisionProbeRadioUs = 0;
    }

    /**
     * Clears the peer entry.
     */
    void Clear(void);

    /**
     * Gets the link-local IPv6 address of the peer.
     *
     * @returns The link-local IPv6 address of the peer.
     */
    void GetLinkLocalIp6Address(Ip6::Address &aIp6Address) const { aIp6Address.SetToLinkLocalAddress(GetExtAddress()); }

    /**
     * Gets the peer-advertised SCA state cached for this peer.
     *
     * @returns The cached SCA parameters.
     */
    const Mac::ScaParams &GetSca(void) const { return mSca; }

    /**
     * Sets the peer-advertised SCA state cached for this peer.
     *
     * Updates the normalized scheduling fields derived from the raw SCA data.
     *
     * @param[in] aSca  The SCA parameters to cache.
     */
    void SetSca(const Mac::ScaParams &aSca);

    /**
     * Updates the cached peer SCA state and the RX timestamp anchor together.
     *
     * @param[in] aSca          The SCA parameters to cache.
     * @param[in] aRxTimestamp  The MAC-header-start timestamp of the frame carrying @p aSca, in microseconds.
     */
    void UpdateSca(const Mac::ScaParams &aSca, uint64_t aRxTimestamp);

    /**
     * Indicates whether the peer advertises an SLW schedule.
     *
     * @retval TRUE   The peer advertises an SLW schedule.
     * @retval FALSE  The peer is rx-on-when-idle.
     */
    bool HasSlwSchedule(void) const { return mSlwPeriodUs != 0; }

    /**
     * Gets the peer-advertised SLW Slot Duration in microseconds.
     *
     * @returns The Slot Duration in microseconds.
     */
    uint64_t GetSlwSlotDurationUs(void) const { return mSlwSlotDurationUs; }

    /**
     * Gets the peer-advertised SLW period in microseconds.
     *
     * @returns The SLW period in microseconds.
     */
    uint64_t GetSlwPeriodUs(void) const { return mSlwPeriodUs; }

    /**
     * Gets the peer-advertised SLW phase in microseconds.
     *
     * @returns The SLW phase in microseconds.
     */
    uint64_t GetSlwPhaseUs(void) const { return mSlwPhaseUs; }

    /**
     * Gets the peer-advertised Thread Direct clock accuracy and uncertainty.
     *
     * @returns The peer-advertised accuracy values.
     */
    const Mac::CslAccuracy &GetSlwAccuracy(void) const { return mSlwAccuracy; }

    /**
     * Sets the peer-advertised Thread Direct clock accuracy and uncertainty.
     *
     * @param[in] aAccuracy  The peer-advertised accuracy values.
     */
    void SetSlwAccuracy(const Mac::CslAccuracy &aAccuracy) { mSlwAccuracy = aAccuracy; }

    /**
     * Updates the cached peer Thread Direct clock accuracy and uncertainty.
     *
     * @param[in] aAccuracy  The peer-advertised accuracy values.
     */
    void UpdateSlwAccuracy(const Mac::CslAccuracy &aAccuracy) { SetSlwAccuracy(aAccuracy); }

    /**
     * Gets the timestamp of the last received frame that refreshed the peer SCA state.
     *
     * @returns The MAC-header-start timestamp in microseconds.
     */
    uint64_t GetLastScaRxTimestamp(void) const { return mLastScaRxTimestamp; }

    /**
     * Sets the timestamp of the last received frame that refreshed the peer SCA state.
     *
     * @param[in] aRxTimestamp  The MAC-header-start timestamp in microseconds.
     */
    void SetLastScaRxTimestamp(uint64_t aRxTimestamp) { mLastScaRxTimestamp = aRxTimestamp; }

    /**
     * Calculates the next peer receive-window start time using the cached SCA data.
     *
     * @param[in]  aRadioNow         The current radio time in microseconds.
     * @param[in]  aAheadUs          Minimum lead time required before transmission.
     * @param[in]  aEarliestUs       Optional idle bound in radio time. Zero means no extra bound.
     * @param[out] aWindowStartTime  The next receive-window start time in microseconds.
     *
     * @retval TRUE   The next receive-window start time was calculated successfully.
     * @retval FALSE  The peer does not advertise an SLW schedule or no anchor timestamp is available.
     */
    bool GetNextSlwWindowStart(uint64_t  aRadioNow,
                               uint32_t  aAheadUs,
                               uint64_t  aEarliestUs,
                               uint64_t &aWindowStartTime) const;

    /**
     * Maps @p aRadioTime onto the start of the SLW window that contains it.
     *
     * If the peer does not advertise an SLW schedule, returns @p aRadioTime unchanged.
     *
     * @param[in] aRadioTime  A radio timestamp in microseconds.
     *
     * @returns The containing window start, or @p aRadioTime when no SLW is advertised.
     */
    uint64_t AlignToSlwWindowStart(uint64_t aRadioTime) const;

    /**
     * Maps @p aRadioTime onto the nearest SLW window start.
     *
     * If the peer does not advertise an SLW schedule, returns @p aRadioTime unchanged.
     *
     * @param[in] aRadioTime  A radio timestamp in microseconds.
     *
     * @returns The nearest window start, or @p aRadioTime when no SLW is advertised.
     */
    uint64_t SnapToNearestSlwWindowStart(uint64_t aRadioTime) const;

    /**
     * Increments the count of re-transmitted link teardown frames.
     */
    void IncrementTearDownCount(void) { mTearDownCount++; }

    /**
     * Resets the teardown re-transmit count to zero.
     */
    void ResetTearDownCount(void) { mTearDownCount = 0; }

    /**
     * Returns the current teardown re-transmit count.
     */
    uint8_t GetTearDownCount(void) const { return mTearDownCount; }

    uint8_t  GetWakeKeyIndex(void) const { return mWakeKeyIndex; }
    void     SetWakeKeyIndex(uint8_t aIndex) { mWakeKeyIndex = aIndex; }
    bool     IsWakeKeyUsed(void) const { return mWakeKeyUsed; }
    void     SetWakeKeyUsed(bool aUsed) { mWakeKeyUsed = aUsed; }
    uint16_t GetSlwPeriodSlots(void) const { return mSlwPeriodSlots; }
    void     SetSlwPeriodSlots(uint16_t aPeriod) { mSlwPeriodSlots = aPeriod; }
    uint16_t GetSlwPhaseSlots(void) const { return mSlwPhaseSlots; }
    void     SetSlwPhaseSlots(uint16_t aPhase) { mSlwPhaseSlots = aPhase; }
    uint32_t GetSupervisionIntervalMs(void) const { return mSupervisionIntervalMs; }
    void     SetSupervisionIntervalMs(uint32_t aIntervalMs) { mSupervisionIntervalMs = aIntervalMs; }
    uint8_t  GetServicesBitmap(void) const { return mServicesBitmap; }
    void     SetServicesBitmap(uint8_t aBitmap) { mServicesBitmap = aBitmap; }
    bool     HasScaSchedule(void) const { return mHasScaSchedule; }
    void     SetHasScaSchedule(bool aHas) { mHasScaSchedule = aHas; }
    bool     IsCoexEnabled(void) const { return mCoexEnabled; }
    void     SetCoexEnabled(bool aEnabled) { mCoexEnabled = aEnabled; }
    uint32_t GetLastWakeFrameCounter(void) const { return mLastWakeFrameCounter; }
    void     SetLastWakeFrameCounter(uint32_t aCounter) { mLastWakeFrameCounter = aCounter; }

    /**
     * Gets the time of the last successful TX or RX exchange with this peer.
     *
     * @returns The time of the last successful exchange.
     */
    TimeMilli GetLastActivityTime(void) const { return mLastActivityTime; }

    /**
     * Sets the time of the last successful TX or RX exchange with this peer.
     *
     * @param[in] aTime  The time of the exchange.
     */
    void SetLastActivityTime(TimeMilli aTime) { mLastActivityTime = aTime; }

    /**
     * Gets the radio time of the last successful TX or RX exchange with this peer.
     *
     * @returns The radio time of the last exchange, in microseconds. Zero if unknown.
     */
    uint64_t GetLastActivityRadioUs(void) const { return mLastActivityRadioUs; }

    /**
     * Records a successful TX or RX exchange.
     *
     * @param[in] aRadioUs  Radio time of the exchange, in microseconds.
     */
    void RecordActivity(uint64_t aRadioUs)
    {
        mLastActivityTime    = TimerMilli::GetNow();
        mLastActivityRadioUs = aRadioUs;
    }

    /**
     * Gets the count of consecutive un-acked link supervision probes sent to this peer.
     *
     * @returns The consecutive un-acked probe count.
     */
    uint8_t GetSupervisionProbeAttempts(void) const { return mSupervisionProbeAttempts; }

    /**
     * Increments the count of consecutive un-acked link supervision probes.
     */
    void IncrementSupervisionProbeAttempts(void) { mSupervisionProbeAttempts++; }

    /**
     * Resets the count of consecutive un-acked link supervision probes to zero.
     */
    void ResetSupervisionProbeAttempts(void) { mSupervisionProbeAttempts = 0; }

    /**
     * Indicates whether a link supervision probe to this peer is in flight.
     *
     * @retval TRUE   A supervision probe is in flight.
     * @retval FALSE  No supervision probe is in flight.
     */
    bool IsSupervisionProbePending(void) const { return mSupervisionProbePending; }

    /**
     * Sets whether a link supervision probe to this peer is in flight.
     *
     * @param[in] aPending  Whether a supervision probe is in flight.
     */
    void SetSupervisionProbePending(bool aPending) { mSupervisionProbePending = aPending; }

    /**
     * Gets the time the most recent link supervision probe to this peer completed.
     *
     * @returns The time of the most recent probe completion.
     */
    TimeMilli GetLastSupervisionProbeTime(void) const { return mLastSupervisionProbeTime; }

    /**
     * Sets the time the most recent link supervision probe to this peer completed.
     *
     * @param[in] aTime  The time the probe completed.
     */
    void SetLastSupervisionProbeTime(TimeMilli aTime) { mLastSupervisionProbeTime = aTime; }

    /**
     * Gets the radio time of the most recent link supervision probe to this peer.
     *
     * @returns The probe's TX window start in microseconds. Zero if unknown.
     */
    uint64_t GetLastSupervisionProbeRadioUs(void) const { return mLastSupervisionProbeRadioUs; }

    /**
     * Sets the radio time of the most recent link supervision probe to this peer.
     *
     * @param[in] aRadioUs  The probe's TX window start in microseconds.
     */
    void SetLastSupervisionProbeRadioUs(uint64_t aRadioUs) { mLastSupervisionProbeRadioUs = aRadioUs; }

private:
    uint16_t
        mSlwPeriodSlots; ///< SLW period in units of advertised Slot Duration (0 = clear schedule / rx-on-when-idle).
    uint16_t mSlwPhaseSlots;                ///< SLW phase in slot-duration units.
    uint32_t mLastWakeFrameCounter;         ///< Last accepted wake frame counter (replay protection).
    uint32_t mSupervisionIntervalMs;        ///< Supervision interval from TD Link Command, in milliseconds.
    uint8_t  mServicesBitmap;               ///< Services bitmap from TD Link Command (bit 0 = peer has SRP server).
    uint8_t  mWakeKeyIndex;                 ///< Key index that secured this link (129 or 130-192).
    uint8_t  mTearDownCount : 3;            ///< Retransmitted teardown frame count.
    uint8_t  mSupervisionProbeAttempts : 3; ///< Consecutive un-acked link supervision probe attempts.
    bool     mWakeKeyUsed : 1;              ///< True if a wake key secured the link.
    bool     mCoexEnabled : 1;              ///< True if the peer reported CoEx constraints (RAM Duration > 1).
    bool     mHasScaSchedule : 1;           ///< True after a valid SCA LTV has been received and applied.
    bool     mSupervisionProbePending : 1;  ///< True while a link supervision probe TX is in flight.

    Mac::ScaParams   mSca;
    Mac::CslAccuracy mSlwAccuracy;
    uint64_t         mSlwSlotDurationUs;
    uint64_t         mSlwPeriodUs;
    uint64_t         mSlwPhaseUs;
    uint64_t         mLastScaRxTimestamp;
    TimeMilli        mLastActivityTime;            ///< Time of the last successful TX or RX exchange with this peer.
    TimeMilli        mLastSupervisionProbeTime;    ///< Time the most recent supervision probe to this peer completed.
    uint64_t         mLastActivityRadioUs;         ///< Radio time of the last successful exchange, in microseconds.
    uint64_t         mLastSupervisionProbeRadioUs; ///< Radio time of the last probe TX window, in microseconds.
};

} // namespace ot

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#endif // OT_CORE_THREAD_DIRECT_PEER_HPP_
