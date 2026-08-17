/*
 *  Copyright (c) 2026, The OpenThread Authors.
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
 *   This file implements Thread Direct transmission scheduling.
 */

#include "thread_direct_tx_scheduler.hpp"

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#include "instance/instance.hpp"
#include "thread/mesh_forwarder.hpp"

namespace ot {

RegisterLogModule("TDTxScheduler");

ThreadDirectTxScheduler::ThreadDirectTxScheduler(Instance &aInstance)
    : InstanceLocator(aInstance)
    , mFrameRequestAheadUs(0)
    , mPendingMessage(nullptr)
    , mPendingCommand(kCommandNone)
    , mPendingDest()
    , mPendingSchedule()
    , mFrameCounter(0)
    , mFrameLength(0)
    , mTxAttempts(0)
    , mDataSequence(0)
    , mKeyId(0)
    , mHasRetxFrameInfo(false)
    , mHasPendingSchedule(false)
{
    UpdateFrameRequestAhead();
}

void ThreadDirectTxScheduler::UpdateFrameRequestAhead(void)
{
    mFrameRequestAheadUs = Mac::kDirectRequestAhead + Get<Mac::Mac>().CalculateRadioBusTransferTime(kMaxFrameSize);
}

Error ThreadDirectTxScheduler::ScheduleTransmission(const Mac::ThreadDirectTxSchedule &aSchedule)
{
    uint64_t radioNow   = Get<Radio>().GetNow();
    uint64_t submitTime = aSchedule.mWindowStart - aSchedule.mRequestAheadUs;

    if (submitTime > (radioNow + kFramePreparationGuardInterval))
    {
        submitTime -= kFramePreparationGuardInterval;
    }
    else
    {
        // If there is not enough slack to apply the full guard, wake MAC immediately.
        submitTime = radioNow;
    }

    mPendingSchedule    = aSchedule;
    mHasPendingSchedule = true;

    if (mPendingCommand != kCommandNone)
    {
        RequestMacCommand(mPendingCommand, (submitTime > radioNow) ? static_cast<uint32_t>(submitTime - radioNow) : 0);
    }
    else
    {
        Get<Mac::Mac>().RequestThreadDirectFrameTransmission(
            (submitTime > radioNow) ? static_cast<uint32_t>(submitTime - radioNow) : 0);
    }

    return kErrorNone;
}

void ThreadDirectTxScheduler::RequestMacCommand(Command aCommand, uint32_t aDelayUs)
{
    switch (aCommand)
    {
    case kCommandTdLinkCmd:
        Get<Mac::Mac>().RequestDelayedTdLinkCmdTransmission(aDelayUs);
        break;

    case kCommandSupervision:
        Get<Mac::Mac>().RequestDelayedTdSupervisionTransmission(aDelayUs);
        break;

    case kCommandTeardown:
        Get<Mac::Mac>().RequestDelayedTdTeardownTransmission(aDelayUs);
        break;

    case kCommandNone:
        break;
    }
}

void ThreadDirectTxScheduler::RequestImmediateMacCommand(Command aCommand)
{
    switch (aCommand)
    {
    case kCommandTdLinkCmd:
        Get<Mac::Mac>().RequestTdLinkCmdTransmission();
        break;

    case kCommandSupervision:
        Get<Mac::Mac>().RequestTdSupervisionTransmission();
        break;

    case kCommandTeardown:
        Get<Mac::Mac>().RequestTeardownTransmission();
        break;

    case kCommandNone:
        break;
    }
}

Error ThreadDirectTxScheduler::ScheduleMacCommand(Command             aCommand,
                                                  const Mac::Address &aDestAddress,
                                                  uint64_t            aEarliestUs)
{
    Error                       error = kErrorNone;
    Mac::ThreadDirectTxSchedule schedule;

    VerifyOrExit(aCommand != kCommandNone, error = kErrorInvalidArgs);
    VerifyOrExit(!IsPending(), error = kErrorBusy);

    mPendingCommand     = aCommand;
    mPendingDest        = aDestAddress;
    mPendingSchedule    = Mac::ThreadDirectTxSchedule();
    mHasPendingSchedule = false;
    mPendingMessage     = nullptr;
    mFrameCounter       = 0;
    mFrameLength        = 0;
    mTxAttempts         = 0;
    mDataSequence       = 0;
    mKeyId              = 0;
    mHasRetxFrameInfo   = false;

    error = Get<Mac::Mac>().CalculateThreadDirectTxSchedule(aDestAddress, kMaxFrameSize, schedule, aEarliestUs);

    if (error == kErrorNone)
    {
        error = ScheduleTransmission(schedule);
        ExitNow();
    }

    // Peer has no SLW (rx-on-when-idle): transmit immediately.
    if (error == kErrorInvalidState)
    {
        mHasPendingSchedule = false;
        RequestImmediateMacCommand(aCommand);
        error = kErrorNone;
        ExitNow();
    }

    Clear();

exit:
    return error;
}

void ThreadDirectTxScheduler::ApplyPendingSchedule(Mac::TxFrame &aFrame) const
{
    VerifyOrExit(mHasPendingSchedule);
    Get<Mac::Mac>().ApplyThreadDirectTxSchedule(aFrame, mPendingSchedule);

exit:
    return;
}

Error ThreadDirectTxScheduler::TrySchedule(Message &aMessage, const Mac::Address &aDestAddress)
{
    Error                       error = kErrorNone;
    Mac::ThreadDirectTxSchedule schedule;
    uint16_t                    frameLength;

    // `TrySchedule()` serves two cases:
    // 1. Initial scheduling of a new TD transmission (`mTxAttempts == 0`).
    // 2. Re-arming a retry for the same pending TD transmission (`mTxAttempts > 0`).
    if (mTxAttempts == 0)
    {
        VerifyOrExit(!IsPending(), error = kErrorBusy);

        mPendingMessage     = &aMessage;
        mPendingCommand     = kCommandNone;
        mPendingDest        = aDestAddress;
        mPendingSchedule    = Mac::ThreadDirectTxSchedule();
        mHasPendingSchedule = false;
        mFrameCounter       = 0;
        mFrameLength        = 0;
        mTxAttempts         = 0;
        mDataSequence       = 0;
        mKeyId              = 0;
        mHasRetxFrameInfo   = false;
        frameLength         = kMaxFrameSize;
    }
    else
    {
        VerifyOrExit(IsPending(), error = kErrorInvalidState);
        VerifyOrExit(IsPendingMessage(aMessage), error = kErrorInvalidState);
        VerifyOrExit(mPendingDest == aDestAddress, error = kErrorInvalidState);
        VerifyOrExit(mFrameLength != 0, error = kErrorInvalidState);
        frameLength = mFrameLength;
    }

    SuccessOrExit(error = Get<Mac::Mac>().CalculateThreadDirectTxSchedule(aDestAddress, frameLength, schedule));
    error = ScheduleTransmission(schedule);

exit:
    return error;
}

Mac::TxFrame *ThreadDirectTxScheduler::HandleFrameRequest(Mac::TxFrames &aTxFrames)
{
    Mac::TxFrame               *frame = nullptr;
    DirectPeer                 *peer  = nullptr;
    Mac::ThreadDirectTxSchedule schedule;
    uint64_t                    minWindowStart;
    uint64_t                    txDelay;

    VerifyOrExit(mPendingMessage != nullptr);
    VerifyOrExit(Get<MeshForwarder>().HasSelectedDirectTransmission());
    VerifyOrExit(&Get<MeshForwarder>().GetSelectedDirectTransmission() == mPendingMessage);
    VerifyOrExit(Get<MeshForwarder>().GetSelectedDirectDestination() == mPendingDest);

    frame = Get<MeshForwarder>().PrepareSelectedDirectFrame(aTxFrames);
    VerifyOrExit(frame != nullptr);

    peer = Get<DirectPeerTable>().FindPeer(mPendingDest, DirectPeer::kInStateValid);
    VerifyOrExit(peer != nullptr, frame = nullptr);
    VerifyOrExit(peer->HasSlwSchedule(), frame = nullptr);

    // Reuse the window reserved earlier in `TrySchedule()`.
    // Only fall forward if that specific window is no longer feasible for the
    // exact frame length.
    schedule = mPendingSchedule;
    schedule.mRequestAheadUs =
        Mac::kDirectRequestAhead + Get<Mac::Mac>().CalculateRadioBusTransferTime(frame->GetLength());
    minWindowStart = Get<Radio>().GetNow() + schedule.mRequestAheadUs;

    // Skip the frame to transmit if the window start is before the minimum window start.
    // Instead, try for the next retry attempt.
    if (schedule.mWindowStart < minWindowStart)
    {
        mFrameLength = frame->GetLength();
        ExitNow(frame = nullptr);
    }

    txDelay = schedule.mWindowStart - peer->GetLastScaRxTimestamp();
    VerifyOrExit(txDelay <= NumericLimits<uint32_t>::kMax, frame = nullptr);
    schedule.mTxDelay         = static_cast<uint32_t>(txDelay);
    schedule.mTxDelayBaseTime = static_cast<uint32_t>(peer->GetLastScaRxTimestamp());

    Get<Mac::Mac>().ApplyThreadDirectTxSchedule(*frame, schedule);
    mPendingSchedule = schedule;
    mFrameLength     = frame->GetLength();

    if (mTxAttempts > 0)
    {
        frame->SetIsARetransmission(true);
        frame->SetSequence(mDataSequence);

        if (frame->GetSecurityEnabled() && mHasRetxFrameInfo)
        {
            frame->SetFrameCounter(mFrameCounter);
            frame->SetKeyId(mKeyId);
        }
    }
    else
    {
        frame->SetIsARetransmission(false);
    }

    frame->SetCsmaCaEnabled(true);

exit:
    return frame;
}

bool ThreadDirectTxScheduler::HandleSentFrame(const Mac::TxFrame &aFrame, Error aError)
{
    bool complete = true;

    switch (aError)
    {
    case kErrorNone:
        Clear();
        break;

    case kErrorNoAck:
    case kErrorChannelAccessFailure:
    case kErrorAbort:
        VerifyOrExit(mPendingMessage != nullptr);

        if (!aFrame.IsEmpty())
        {
            mDataSequence = aFrame.GetSequence();
            mFrameLength  = aFrame.GetLength();

            if (aFrame.GetSecurityEnabled() && aFrame.IsHeaderUpdated())
            {
                IgnoreError(aFrame.GetFrameCounter(mFrameCounter));
                IgnoreError(aFrame.GetKeyId(mKeyId));
                mHasRetxFrameInfo = true;
            }
        }

        mTxAttempts++;

        if (mTxAttempts >= kMaxTxAttempts)
        {
            Clear();
            break;
        }

        SuccessOrExit(TrySchedule(*mPendingMessage, mPendingDest));
        complete = false;
        break;

    default:
        OT_ASSERT(false);
        OT_UNREACHABLE_CODE(break);
    }

exit:
    if (complete && (aError != kErrorNone))
    {
        Clear();
    }

    return complete;
}

void ThreadDirectTxScheduler::Update(void)
{
    if (!IsPending())
    {
        ExitNow();
    }

    if (!Get<MeshForwarder>().HasSelectedDirectTransmission() ||
        !IsPendingMessage(Get<MeshForwarder>().GetSelectedDirectTransmission()))
    {
        Clear();
    }

exit:
    return;
}

void ThreadDirectTxScheduler::ClearIfMacCommand(void)
{
    if (mPendingCommand != kCommandNone)
    {
        Clear();
        Get<MeshForwarder>().mScheduleTransmissionTask.Post();
    }
}

void ThreadDirectTxScheduler::ClearIfCommand(Command aCommand)
{
    if (mPendingCommand == aCommand)
    {
        ClearIfMacCommand();
    }
}

void ThreadDirectTxScheduler::Clear(void)
{
    mPendingMessage     = nullptr;
    mPendingCommand     = kCommandNone;
    mPendingDest        = Mac::Address();
    mPendingSchedule    = Mac::ThreadDirectTxSchedule();
    mHasPendingSchedule = false;
    mFrameCounter       = 0;
    mFrameLength        = 0;
    mTxAttempts         = 0;
    mDataSequence       = 0;
    mKeyId              = 0;
    mHasRetxFrameInfo   = false;
}

} // namespace ot

#endif
