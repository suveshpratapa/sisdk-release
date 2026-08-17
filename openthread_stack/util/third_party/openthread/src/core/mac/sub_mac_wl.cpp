/*
 *  Copyright (c) 2024, The OpenThread Authors.
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
 *   This file implements the Thread Direct Wake Listener (WL) and local SLW receive scheduling subset
 *   of IEEE 802.15.4 MAC primitives.
 */

#include "sub_mac.hpp"

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE

#include "common/num_utils.hpp"
#include "instance/instance.hpp"
#include "thread/direct_peer_table.hpp"

namespace ot {
namespace Mac {

RegisterLogModule("SubMac");

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
void SubMac::ThreadDirectSlwInit(void)
{
    mIsThreadDirectSlwSampling     = false;
    mIsThreadDirectSlwEnabled      = false;
    mThreadDirectSlwChannel        = 0;
    mThreadDirectSlwPeriod         = 0;
    mThreadDirectSlwSlotDurationUs = 0;
    mThreadDirectSlwLastSync.SetValue(0);
    mThreadDirectSlwSampleTimeRadio = 0;
    mThreadDirectSlwSampleTimeLocal.SetValue(0);
    mThreadDirectSlwTimer.Stop();
}

void SubMac::UpdateThreadDirectSlw(bool     aEnable,
                                   uint32_t aPeriodUs,
                                   uint16_t aPeriodSlots,
                                   uint32_t aSlotDurationUs,
                                   uint8_t  aChannel,
                                   uint32_t aSampleTimeRadio)
{
    mThreadDirectSlwSlotDurationUs = aEnable ? aSlotDurationUs : 0;

    bool diffEnable  = mIsThreadDirectSlwEnabled != aEnable;
    bool diffPeriod  = mThreadDirectSlwPeriod != aPeriodUs;
    bool diffChannel = mThreadDirectSlwChannel != aChannel;
    bool didChange   = diffEnable || diffPeriod || diffChannel;

    VerifyOrExit(didChange);

    mThreadDirectSlwChannel = aChannel;

    VerifyOrExit(diffEnable || diffPeriod);

    mIsThreadDirectSlwEnabled  = aEnable;
    mThreadDirectSlwPeriod     = aPeriodUs;
    mIsThreadDirectSlwSampling = false;
    mThreadDirectSlwLastSync   = TimeMicro(GetLocalTime());
    IgnoreError(Get<Radio>().SetThreadDirectSlwSchedule(aEnable ? aPeriodSlots : 0U, aSlotDurationUs));

    mThreadDirectSlwTimer.Stop();

    if (aEnable)
    {
        StartThreadDirectSlwAtSampleTime(aSampleTimeRadio);
    }
    else if (!RadioSupportsReceiveTiming())
    {
        UpdateRadioSampleState();
    }

exit:
    return;
}

void SubMac::StartThreadDirectSlwAtSampleTime(uint32_t aSampleTimeRadio)
{
    uint32_t  nowRadio = static_cast<uint32_t>(Get<Radio>().GetNow());
    TimeMicro nowLocal = TimerMicro::GetNow();

    VerifyOrExit(mIsThreadDirectSlwEnabled && (mThreadDirectSlwPeriod > 0));

    if (aSampleTimeRadio == 0)
    {
        aSampleTimeRadio = nowRadio;
    }

    while (static_cast<int32_t>(aSampleTimeRadio - nowRadio) <= 0)
    {
        aSampleTimeRadio += mThreadDirectSlwPeriod;
    }

    mThreadDirectSlwSampleTimeRadio = aSampleTimeRadio;
    mThreadDirectSlwSampleTimeLocal = nowLocal + (aSampleTimeRadio - nowRadio);
    HandleThreadDirectSlwTimer();

exit:
    return;
}

uint16_t SubMac::ComputeSlwPhaseSlots(uint32_t aSlotDurationUs) const
{
    return ComputeSlwPhaseSlotsAt(static_cast<uint32_t>(Get<Radio>().GetNow()), aSlotDurationUs);
}

uint16_t SubMac::ComputeSlwPhaseSlotsAt(uint32_t aRefTimeUs, uint32_t aSlotDurationUs) const
{
    uint16_t phaseSlots  = 0;
    int16_t  ramOffsetUs = 0;

    OT_UNUSED_VARIABLE(aSlotDurationUs);
    (void)ComputeSlwPhaseAndRamOffsetAt(aRefTimeUs, phaseSlots, ramOffsetUs);

    return phaseSlots;
}

bool SubMac::ComputeSlwPhaseAndRamOffsetAt(uint32_t aRefTimeUs, uint16_t &aPhaseSlots, int16_t &aRamOffsetUs) const
{
    bool     success = false;
    uint32_t periodUs;
    uint32_t slotUs;
    uint32_t periodSlots;
    uint32_t deltaUs;
    uint32_t phaseSlots;
    int32_t  ramOffsetUs;

    VerifyOrExit(mIsThreadDirectSlwEnabled && (mThreadDirectSlwPeriod > 0) && (mThreadDirectSlwSlotDurationUs > 0));

    slotUs      = mThreadDirectSlwSlotDurationUs;
    periodUs    = mThreadDirectSlwPeriod;
    periodSlots = periodUs / slotUs;
    VerifyOrExit(periodSlots > 0);

    deltaUs = ((mThreadDirectSlwSampleTimeRadio % periodUs) - (aRefTimeUs % periodUs) + periodUs) % periodUs;

    phaseSlots  = (deltaUs + (slotUs / 2)) / slotUs;
    ramOffsetUs = static_cast<int32_t>(deltaUs) - static_cast<int32_t>(phaseSlots * slotUs);

    VerifyOrExit(phaseSlots <= periodSlots);
    VerifyOrExit((ramOffsetUs >= -1024) && (ramOffsetUs <= 1023));

    aPhaseSlots  = static_cast<uint16_t>(phaseSlots);
    aRamOffsetUs = static_cast<int16_t>(ramOffsetUs);
    success      = true;

exit:
    return success;
}

void SubMac::HandleThreadDirectSlwTimer(Timer &aTimer) { aTimer.Get<SubMac>().HandleThreadDirectSlwTimer(); }

void SubMac::HandleThreadDirectSlwTimer(void)
{
    uint32_t timeAhead, timeAfter;

    GetThreadDirectSlwWindowEdges(timeAhead, timeAfter);

    // The handler works in different ways when the radio supports receive-timing and doesn't.
    if (RadioSupportsReceiveTiming())
    {
        HandleThreadDirectSlwReceiveAt(timeAhead, timeAfter);
    }
    else
    {
        HandleThreadDirectSlwReceiveOrSleep(timeAhead, timeAfter);
    }
}

void SubMac::HandleThreadDirectSlwReceiveAt(uint32_t aTimeAhead, uint32_t aTimeAfter)
{
    /*
     * When the radio supports receive-timing:
     *   The handler runs once per local Thread Direct SLW period. It reuses the same
     *   sample-point semantics as CSL: mThreadDirectSlwSampleTime{Local,Radio} represent
     *   the expected sample time, while the actual receive window starts earlier and spans
     *   the ahead/after margins computed from local/peer accuracy and uncertainty.
     *
     *   Timer fires                                         Timer fires
     *       ^                                                    ^
     *       x-|------------|-------------------------------------x-|------------|---------------------------------------|
     *            sample                   sleep                        sample                    sleep
     */
    uint32_t  periodUs = mThreadDirectSlwPeriod;
    uint32_t  winStart;
    uint32_t  winDuration;
    uint32_t  nextCycleDrift;
    TimeMicro nextTimerFireTime;

    nextCycleDrift    = GetThreadDirectNextCycleDrift();
    nextTimerFireTime = mThreadDirectSlwSampleTimeLocal + periodUs - aTimeAhead - nextCycleDrift;
    aTimeAhead -= kCslReceiveTimeAhead;
    winStart    = mThreadDirectSlwSampleTimeRadio - aTimeAhead;
    winDuration = aTimeAhead + aTimeAfter;

    HandlePeriodicReceiveAt(mThreadDirectSlwTimer, nextTimerFireTime, mThreadDirectSlwSampleTimeLocal,
                            mThreadDirectSlwSampleTimeRadio, periodUs, mThreadDirectSlwChannel, winStart, winDuration);

    Get<Radio>().UpdateThreadDirectSlwSampleTime(mThreadDirectSlwSampleTimeRadio);

    LogThreadDirectSlwWindow(winStart, winDuration);
}

void SubMac::HandleThreadDirectSlwReceiveOrSleep(uint32_t aTimeAhead, uint32_t aTimeAfter)
{
    /*
     * When the radio doesn't support receive-timing:
     *   The handler runs twice per Thread Direct SLW period: once to enter the receive
     *   window and once to return to sleep. The radio state transition itself is handled
     *   through UpdateRadioSampleState(), matching the CSL receive/sleep behavior.
     *
     *   Timer fires  Timer fires                            Timer fires  Timer fires
     *       ^            ^                                       ^            ^
     *       |------------|---------------------------------------|------------|---------------------------------------|
     *          sample                   sleep                        sample                    sleep
     */
    TimeMicro sleepFireTime;
    TimeMicro sampleFireTime;
    bool      isSampling;

    sleepFireTime  = mThreadDirectSlwSampleTimeLocal + aTimeAfter;
    sampleFireTime = mThreadDirectSlwSampleTimeLocal - aTimeAhead - GetThreadDirectNextCycleDrift();
    isSampling     = mIsThreadDirectSlwSampling;

    if (!HandlePeriodicReceiveOrSleep(mThreadDirectSlwTimer, isSampling, sleepFireTime, sampleFireTime,
                                      mThreadDirectSlwSampleTimeLocal, mThreadDirectSlwSampleTimeRadio,
                                      mThreadDirectSlwPeriod))
    {
        if (mState == kStateRadioSample)
        {
            LogDebg("TD SLW sleep %lu", ToUlong(mThreadDirectSlwTimer.GetNow().GetValue()));
        }
    }
    else
    {
        uint32_t winStart;
        uint32_t winDuration;

        winStart    = TimerMicro::GetNow().GetValue();
        winDuration = aTimeAhead + aTimeAfter;

        Get<Radio>().UpdateThreadDirectSlwSampleTime(mThreadDirectSlwSampleTimeRadio);

        LogThreadDirectSlwWindow(winStart, winDuration);
    }

    mIsThreadDirectSlwSampling = isSampling;

    UpdateRadioSampleState();
}

void SubMac::GetThreadDirectSlwWindowEdges(uint32_t &aAhead, uint32_t &aAfter) const
{
    /*
     * Thread Direct local sample timing diagram
     *    |<---------------------------------Sample--------------------------------->|<--------Sleep--------->|
     *    |                                                                          |                        |
     *    |<--Ahead-->|<--UnCert-->|<--Drift-->|<--Drift-->|<--UnCert-->|<--MinWin-->|                        |
     *    |           |            |           |           |            |            |                        |
     * ---|-----------|------------|-----------|-----------|------------|------------|----------//------------|---
     * -timeAhead                           SlwSample                            +timeAfter             -timeAhead
     */
    CslAccuracy peerAccuracy;
    uint32_t    elapsedUs;

    peerAccuracy.Init();

    // Using the worst active peer accuracy keeps the shared TD receive window conservative
    // until per-peer receive-window specialization is introduced.
    Get<DirectPeerTable>().GetWorstCaseThreadDirectPeerSlwAccuracy(peerAccuracy);

    elapsedUs = GetLocalTime() - mThreadDirectSlwLastSync.GetValue();

    CalculatePeriodicSampleWindowEdges(mThreadDirectSlwPeriod, elapsedUs, Get<Radio>().GetThreadDirectSlwAccuracy(),
                                       Get<Radio>().GetThreadDirectSlwUncertainty() * 10,
                                       peerAccuracy.GetClockAccuracy(), peerAccuracy.GetUncertaintyInMicrosec(), aAhead,
                                       aAfter);

    // SCA phase is advertised in slot units. Truncation makes reconstructed TX
    // early; nearest-slot rounding can go either way. Cover one local slot on
    // both edges. `kCslReceiveTimeAhead` is subtracted from `aAhead` before
    // ReceiveAt, so this term has to live in the window, not in warmup.
    aAhead = Min(mThreadDirectSlwPeriod / 2, aAhead + mThreadDirectSlwSlotDurationUs);
    aAfter = Min(mThreadDirectSlwPeriod / 2, aAfter + mThreadDirectSlwSlotDurationUs);
}

uint32_t SubMac::GetThreadDirectNextCycleDrift(void) const
{
    CslAccuracy peerAccuracy;

    peerAccuracy.Init();
    Get<DirectPeerTable>().GetWorstCaseThreadDirectPeerSlwAccuracy(peerAccuracy);

    return CalculatePeriodicSamplePeriodDrift(mThreadDirectSlwPeriod, Get<Radio>().GetThreadDirectSlwAccuracy(),
                                              peerAccuracy.GetClockAccuracy());
}

#if OT_SHOULD_LOG_AT(OT_LOG_LEVEL_DEBG)
void SubMac::LogThreadDirectSlwWindow(uint32_t aWinStart, uint32_t aWinDuration) const
{
    LogDebg("TD SLW window start %lu, duration %lu", ToUlong(aWinStart), ToUlong(aWinDuration));
}
#else
void SubMac::LogThreadDirectSlwWindow(uint32_t, uint32_t) const {}
#endif

void SubMac::UpdateThreadDirectSlwSyncTimestamp(const RxFrame &aFrame)
{
    VerifyOrExit(mIsThreadDirectSlwEnabled && (mThreadDirectSlwPeriod > 0));

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE && OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_LOCAL_TIME_SYNC
    mThreadDirectSlwLastSync = TimerMicro::GetNow();
#else
    mThreadDirectSlwLastSync = TimeMicro(static_cast<uint32_t>(aFrame.mInfo.mRxInfo.mTimestamp));
#endif

exit:
    return;
}
#endif

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
void SubMac::WlInit(void)
{
    mIsWlSampling         = false;
    mIsWlEnabled          = false;
    mWakeupListenInterval = 0;
    mWlTimer.Stop();
}

void SubMac::UpdateWakeupListening(bool aEnable, uint32_t aInterval, uint32_t aDuration, uint8_t aChannel)
{
    mWakeupListenInterval = aInterval;
    mWakeupListenDuration = aDuration;
    mWakeupChannel        = aChannel;
    mIsWlSampling         = false;
    mIsWlEnabled          = aEnable;

    mWlTimer.Stop();

    if (aEnable)
    {
        mWlSampleTime      = TimerMicro::GetNow() + kCslReceiveTimeAhead - mWakeupListenInterval;
        mWlSampleTimeRadio = Get<Radio>().GetNow() + kCslReceiveTimeAhead - mWakeupListenInterval;

        HandleWlTimer();
    }
    else if (!RadioSupportsReceiveTiming())
    {
        UpdateRadioSampleState();
    }
}

void SubMac::HandleWlTimer(Timer &aTimer) { aTimer.Get<SubMac>().HandleWlTimer(); }

void SubMac::HandleWlTimer(void)
{
    if (RadioSupportsReceiveTiming())
    {
        HandleWlReceiveAt();
    }
    else
    {
        HandleWlReceiveOrSleep();
    }
}

void SubMac::HandleWlReceiveAt(void)
{
    uint32_t nowRadio = static_cast<uint32_t>(Get<Radio>().GetNow());

    mWlSampleTime += mWakeupListenInterval;
    mWlSampleTimeRadio += mWakeupListenInterval;

    // A late callback (say an RTOS scheduling delay on the OpenThread task) can leave a single
    // interval step at or behind the radio's current time.
    // Keep advancing until the sample point is strictly ahead of the radio clock, matching the
    // resync loop already used for the post-link SLW schedule (StartThreadDirectSlwAtSampleTime).
    while (static_cast<int32_t>(mWlSampleTimeRadio - nowRadio) <= 0)
    {
        mWlSampleTime += mWakeupListenInterval;
        mWlSampleTimeRadio += mWakeupListenInterval;
    }

    mWlTimer.FireAt(mWlSampleTime + mWakeupListenDuration + kWlReceiveTimeAfter);

    if (mState != kStateDisabled)
    {
        IgnoreError(
            Get<Radio>().ReceiveAt(mWakeupChannel, static_cast<uint32_t>(mWlSampleTimeRadio), mWakeupListenDuration));
    }
}

void SubMac::HandleWlReceiveOrSleep(void)
{
    TimeMilli fireTime;

    mIsWlSampling = !mIsWlSampling;

    if (mIsWlSampling)
    {
        fireTime = mWlSampleTime + mWakeupListenDuration + kMinReceiveOnAfter;
    }
    else
    {
        mWlSampleTime += mWakeupListenInterval;
        fireTime = mWlSampleTime - kMinReceiveOnAhead;
    }

    mWlTimer.FireAt(fireTime);

    if (mState == kStateRadioSample)
    {
        UpdateRadioSampleState();
    }
    else if (mIsWlSampling && mState == kStateReceive)
    {
        IgnoreError(Get<Radio>().Receive(mWakeupChannel));
    }
}
#endif
} // namespace Mac
} // namespace ot

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
