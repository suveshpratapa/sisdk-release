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

#include "instance/instance.hpp"
#include "thread/direct_peer_table.hpp"

namespace ot {
namespace Mac {

RegisterLogModule("SubMac");

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
void SubMac::ThreadDirectSlwInit(void)
{
    mIsThreadDirectSlwSampling = false;
    mIsThreadDirectSlwEnabled  = false;
    mThreadDirectSlwChannel    = 0;
    mThreadDirectSlwPeriod     = 0;
    mThreadDirectSlwLastSync.SetValue(0);
    mThreadDirectSlwSampleTimeRadio = 0;
    mThreadDirectSlwSampleTimeLocal.SetValue(0);
    mThreadDirectSlwTimer.Stop();
}

void SubMac::RestartThreadDirectSlwTimerAfterSyncUpdate(void)
{
    // Only applies for the case where radio supports receive timing.
    if (RadioSupportsReceiveTiming() && mThreadDirectSlwTimer.IsRunning())
    {
        uint32_t periodUs = mThreadDirectSlwPeriod;

        mThreadDirectSlwTimer.Stop();

        // Rewind sample times by one period. HandleThreadDirectSlwTimer() will
        // add this period back, effectively re-evaluating the current TD SLW
        // period's schedule using the updated sync anchor.
        mThreadDirectSlwSampleTimeRadio -= periodUs;
        mThreadDirectSlwSampleTimeLocal -= periodUs;

        HandleThreadDirectSlwTimer();
    }
}

void SubMac::UpdateThreadDirectSlw(bool aEnable, uint32_t aPeriodUs, uint8_t aChannel)
{
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
    IgnoreError(Get<Radio>().SetThreadDirectSlwSchedule(aEnable ? aPeriodUs : 0, 0));

    mThreadDirectSlwTimer.Stop();

    if (aEnable)
    {
        mThreadDirectSlwSampleTimeLocal = TimerMicro::GetNow();
        mThreadDirectSlwSampleTimeRadio = static_cast<uint32_t>(Get<Radio>().GetNow());
        HandleThreadDirectSlwTimer();
    }
    else if (!RadioSupportsReceiveTiming())
    {
        UpdateRadioSampleState();
    }

exit:
    return;
}

void SubMac::UpdateThreadDirectSlwSyncTimestamp(const RxFrame &aFrame)
{
    VerifyOrExit(mIsThreadDirectSlwEnabled && (mThreadDirectSlwPeriod > 0));

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE && OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_LOCAL_TIME_SYNC
    mThreadDirectSlwLastSync = TimerMicro::GetNow();
#else
    mThreadDirectSlwLastSync = TimeMicro(static_cast<uint32_t>(aFrame.mInfo.mRxInfo.mTimestamp));
#endif

    RestartThreadDirectSlwTimerAfterSyncUpdate();

exit:
    return;
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
    mWlSampleTime += mWakeupListenInterval;
    mWlSampleTimeRadio += mWakeupListenInterval;
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
