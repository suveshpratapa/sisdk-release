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
 *   This file includes definitions for Thread Direct transmission scheduling.
 */

#ifndef OT_CORE_THREAD_THREAD_DIRECT_TX_SCHEDULER_HPP_
#define OT_CORE_THREAD_THREAD_DIRECT_TX_SCHEDULER_HPP_

#include "openthread-core-config.h"

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#include "common/locator.hpp"
#include "common/message.hpp"
#include "common/non_copyable.hpp"
#include "mac/mac.hpp"

namespace ot {

class MeshForwarder;

class ThreadDirectTxScheduler : public InstanceLocator, private NonCopyable
{
    friend class Mac::Mac;
    friend class MeshForwarder;

public:
    enum Command : uint8_t
    {
        kCommandNone = 0,
        kCommandTdLinkCmd,
        kCommandSupervision,
        kCommandTeardown,
    };

    explicit ThreadDirectTxScheduler(Instance &aInstance);

    Error TrySchedule(Message &aMessage, const Mac::Address &aDestAddress);
    Error ScheduleMacCommand(Command aCommand, const Mac::Address &aDestAddress, uint64_t aEarliestUs = 0);
    void  Update(void);
    void  Clear(void);
    void  ClearIfMacCommand(void);
    void  ClearIfCommand(Command aCommand);

    bool     IsPending(void) const { return (mPendingMessage != nullptr) || (mPendingCommand != kCommandNone); }
    bool     HasPendingSchedule(void) const { return mHasPendingSchedule; }
    uint64_t GetPendingWindowStart(void) const { return mPendingSchedule.mWindowStart; }
    void     ApplyPendingSchedule(Mac::TxFrame &aFrame) const;

private:
    static constexpr uint16_t kMaxFrameSize                  = 150;
    static constexpr uint32_t kFramePreparationGuardInterval = 500;
    static constexpr uint8_t  kMaxTxAttempts                 = 8;

    void  UpdateFrameRequestAhead(void);
    Error ScheduleTransmission(const Mac::ThreadDirectTxSchedule &aSchedule);
    void  RequestMacCommand(Command aCommand, uint32_t aDelayUs);
    void  RequestImmediateMacCommand(Command aCommand);

    Mac::TxFrame *HandleFrameRequest(Mac::TxFrames &aTxFrames);
    bool          HandleSentFrame(const Mac::TxFrame &aFrame, Error aError);

    bool IsPendingMessage(const Message &aMessage) const { return mPendingMessage == &aMessage; }

    uint32_t                    mFrameRequestAheadUs;
    Message                    *mPendingMessage;
    Command                     mPendingCommand;
    Mac::Address                mPendingDest;
    Mac::ThreadDirectTxSchedule mPendingSchedule;
    uint32_t                    mFrameCounter;
    uint16_t                    mFrameLength;
    uint8_t                     mTxAttempts;
    uint8_t                     mDataSequence;
    uint8_t                     mKeyId;
    bool                        mHasRetxFrameInfo;
    bool                        mHasPendingSchedule;
};

} // namespace ot

#endif

#endif // OT_CORE_THREAD_THREAD_DIRECT_TX_SCHEDULER_HPP_
