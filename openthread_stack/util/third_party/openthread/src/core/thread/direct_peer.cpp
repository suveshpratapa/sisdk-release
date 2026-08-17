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
 *   This file implements the Thread Direct `DirectPeer`.
 */

#include "direct_peer.hpp"

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#include "common/code_utils.hpp"
#include "instance/instance.hpp"

namespace ot {

namespace {

uint32_t ScaSlotDurationToUs(Mac::ScaSlotDuration aSlotDuration)
{
    switch (aSlotDuration)
    {
    case Mac::ScaSlotDuration::k625Usec:
        return 625;
    case Mac::ScaSlotDuration::k1250Usec:
        return 1250;
    case Mac::ScaSlotDuration::k625Msec:
        return 625000;
    case Mac::ScaSlotDuration::k1250Msec:
        return 1250000;
    }

    OT_UNREACHABLE_CODE(return 625;);
}

} // namespace

void DirectPeer::SetSca(const Mac::ScaParams &aSca)
{
    mSca               = aSca;
    mSlwSlotDurationUs = ScaSlotDurationToUs(aSca.mSlotDuration);
    mSlwPeriodUs       = mSlwSlotDurationUs * aSca.mSlwPeriodSlots;
    mSlwPhaseUs        = mSlwSlotDurationUs * aSca.mSlwPhaseSlots;
    mSlwPeriodSlots    = aSca.mSlwPeriodSlots;
    mSlwPhaseSlots     = aSca.mSlwPhaseSlots;

    if (aSca.mHasClockAccuracy)
    {
        mSlwAccuracy.SetClockAccuracy(aSca.mClockAccuracy);
        mSlwAccuracy.SetUncertainty(aSca.mUncertainty);
    }
}

void DirectPeer::UpdateSca(const Mac::ScaParams &aSca, uint64_t aRxTimestamp)
{
    SetSca(aSca);
    SetLastScaRxTimestamp(aRxTimestamp);
}

bool DirectPeer::GetNextSlwWindowStart(uint64_t  aRadioNow,
                                       uint32_t  aAheadUs,
                                       uint64_t  aEarliestUs,
                                       uint64_t &aWindowStartTime) const
{
    bool     hasWindow = false;
    uint64_t firstWindowStart;
    uint64_t minWindowStart;

    VerifyOrExit(HasSlwSchedule() && (mLastScaRxTimestamp != 0));

    firstWindowStart = mLastScaRxTimestamp;

    if (mSca.mRamOffsetUs >= 0)
    {
        firstWindowStart += static_cast<uint64_t>(mSca.mRamOffsetUs);
    }
    else
    {
        uint64_t ramOffsetUs = static_cast<uint64_t>(-mSca.mRamOffsetUs);

        firstWindowStart = (firstWindowStart > ramOffsetUs) ? (firstWindowStart - ramOffsetUs) : 0;
    }

    firstWindowStart += mSlwPhaseUs;
    aWindowStartTime = firstWindowStart;
    minWindowStart   = aRadioNow + aAheadUs;

    if (aWindowStartTime < minWindowStart)
    {
        uint64_t elapsedUs = minWindowStart - aWindowStartTime;
        uint64_t numPeriods;

        numPeriods = elapsedUs / mSlwPeriodUs;
        aWindowStartTime += numPeriods * mSlwPeriodUs;

        if (aWindowStartTime < minWindowStart)
        {
            aWindowStartTime += mSlwPeriodUs;
        }
    }

    if (aEarliestUs != 0)
    {
        if (aWindowStartTime < aEarliestUs)
        {
            uint64_t extraPeriods;

            extraPeriods = (aEarliestUs - aWindowStartTime) / mSlwPeriodUs;
            aWindowStartTime += extraPeriods * mSlwPeriodUs;

            if (aWindowStartTime < aEarliestUs)
            {
                aWindowStartTime += mSlwPeriodUs;
            }
        }
    }

    hasWindow = true;

exit:
    return hasWindow;
}

uint64_t DirectPeer::AlignToSlwWindowStart(uint64_t aRadioTime) const
{
    uint64_t nextWindowStart;

    VerifyOrExit(GetNextSlwWindowStart(aRadioTime, /* aAheadUs */ 0, /* aEarliestUs */ 0, nextWindowStart));

    if ((nextWindowStart > aRadioTime) && (nextWindowStart >= mSlwPeriodUs))
    {
        uint64_t prevWindowStart = nextWindowStart - mSlwPeriodUs;

        if (aRadioTime >= prevWindowStart)
        {
            aRadioTime = prevWindowStart;
        }
        else if (prevWindowStart >= mSlwPeriodUs)
        {
            aRadioTime = prevWindowStart - mSlwPeriodUs;
        }
        else
        {
            aRadioTime = nextWindowStart;
        }
    }
    else
    {
        aRadioTime = nextWindowStart;
    }

exit:
    return aRadioTime;
}

uint64_t DirectPeer::SnapToNearestSlwWindowStart(uint64_t aRadioTime) const
{
    uint64_t nextWindowStart;
    uint64_t nearest = aRadioTime;

    VerifyOrExit(GetNextSlwWindowStart(aRadioTime, /* aAheadUs */ 0, /* aEarliestUs */ 0, nextWindowStart));

    nearest = nextWindowStart;

    if ((nextWindowStart > aRadioTime) && (nextWindowStart >= mSlwPeriodUs))
    {
        uint64_t prevWindowStart = nextWindowStart - mSlwPeriodUs;

        if (aRadioTime >= prevWindowStart)
        {
            if ((aRadioTime - prevWindowStart) <= (nextWindowStart - aRadioTime))
            {
                nearest = prevWindowStart;
            }
        }
        else if (prevWindowStart >= mSlwPeriodUs)
        {
            uint64_t olderWindowStart = prevWindowStart - mSlwPeriodUs;

            if ((prevWindowStart - aRadioTime) <= (aRadioTime - olderWindowStart))
            {
                nearest = prevWindowStart;
            }
            else
            {
                nearest = olderWindowStart;
            }
        }
    }

exit:
    return nearest;
}

void DirectPeer::Clear(void)
{
    Instance &instance = GetInstance();

    ClearAllBytes(*this);
    Init(instance);
}

} // namespace ot

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
