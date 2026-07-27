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
 *   This file implements the Thread Direct handler: WI and WL link handshake
 *   state machines and local SCA state management.
 */

#include "direct_handler.hpp"

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#include <string.h>

#include "common/code_utils.hpp"
#include "common/frame_builder.hpp"
#include "common/log.hpp"
#include "common/random.hpp"
#include "common/time.hpp"
#include "config/thread_direct.h"
#include "instance/instance.hpp"
#include "mac/mac.hpp"
#include "mac/mac_links.hpp"
#include "mac/sub_mac.hpp"
#include "radio/radio.hpp"
#include "thread/direct_peer.hpp"
#include "thread/direct_peer_table.hpp"
#include "thread/key_manager.hpp"

namespace ot {

RegisterLogModule("DirectHandler");

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

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
bool CalculateMidpointSlwSampleTime(const Mac::ScaParams &aPeerSca,
                                    const Mac::ScaParams &aLocalSca,
                                    uint32_t              aRxTimestamp,
                                    uint32_t              aRadioNow,
                                    uint32_t             &aSampleTimeRadio)
{
    uint32_t slotDurationUs;
    uint32_t periodUs;
    int64_t  nextPeerSample;

    VerifyOrExit(aPeerSca.mHasSlw && aLocalSca.mHasSlw);
    VerifyOrExit(aPeerSca.mSlwPeriodSlots == aLocalSca.mSlwPeriodSlots);
    VerifyOrExit(aPeerSca.mSlotDuration == aLocalSca.mSlotDuration);

    slotDurationUs = ScaSlotDurationToUs(aPeerSca.mSlotDuration);
    periodUs       = aPeerSca.mSlwPeriodSlots * slotDurationUs;

    VerifyOrExit(periodUs > 0);

    nextPeerSample = static_cast<int64_t>(aRxTimestamp) + aPeerSca.mRamOffsetUs +
                     (static_cast<uint32_t>(aPeerSca.mSlwPhaseSlots) * slotDurationUs);

    aSampleTimeRadio = static_cast<uint32_t>(nextPeerSample) + (periodUs / 2);

    while (static_cast<int32_t>(aSampleTimeRadio - aRadioNow) <= 0)
    {
        aSampleTimeRadio += periodUs;
    }

    return true;

exit:
    return false;
}
#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE

constexpr uint8_t kSupervisionIntervalUnitMs = 100; ///< Unit of the TD Link Command Supervision Interval field.

uint8_t EncodeSupervisionIntervalMs(uint32_t aIntervalMs)
{
    return static_cast<uint8_t>(aIntervalMs / kSupervisionIntervalUnitMs);
}

uint16_t ParseTdLinkCommandSupervisionIntervalMs(const Mac::RxFrame &aFrame)
{
    static constexpr uint8_t kLinkParamMaskIndex       = 2;
    static constexpr uint8_t kSupervisionIntervalIndex = 3;

    const uint8_t *payload    = aFrame.GetPayload();
    uint16_t       intervalMs = 0;

    VerifyOrExit((payload != nullptr) && (aFrame.GetPayloadLength() > kSupervisionIntervalIndex));
    VerifyOrExit(payload[kLinkParamMaskIndex] & Mac::Frame::kLinkParamMaskSupervisionInterval);

    intervalMs = static_cast<uint16_t>(payload[kSupervisionIntervalIndex]) * kSupervisionIntervalUnitMs;

exit:
    return intervalMs;
}

} // namespace

DirectHandler::DirectHandler(Instance &aInstance)
    : InstanceLocator(aInstance)
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
    , mWiState(kWiIdle)
#endif
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    , mWlState(kWlIdle)
    , mWlStateTimer(aInstance)
#endif
    , mSlwSlotDurationUs(0)
    , mSlwPeriodUs(0)
    , mSlwTimeout(kDefaultSlwTimeout)
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    , mScaUpdatePending(false)
    , mScaUpdateInFlight(false)
    , mTeardownKeyIndex(Mac::Frame::kWakeKeyIndex)
    , mTeardownPending(false)
    , mTeardownLastScaRxTs(0)
    , mTeardownSlwPeriodUs(0)
    , mTeardownSlwPhaseUs(0)
    , mSupervisionTimer(aInstance)
    , mSupervisionKeyIndex(Mac::Frame::kWakeKeyIndex)
    , mSupervisionPending(false)
#endif
{
    memset(&mLocalSca, 0, sizeof(mLocalSca));
    mLocalSca.mSlotDuration = Mac::ScaSlotDuration::k625Usec;
    mLocalSca.mRamAvailable = false;
    UpdateDerivedSlwTiming();
}

Error DirectHandler::SetSlwSchedule(uint16_t aPeriodSlots)
{
    Error error = kErrorNone;

    if (aPeriodSlots != 0)
    {
        VerifyOrExit(aPeriodSlots >= OPENTHREAD_CONFIG_THREAD_DIRECT_SLW_MIN_DURATION_SLOTS, error = kErrorInvalidArgs);
    }

    mLocalSca.mSlwPeriodSlots = aPeriodSlots;
    mLocalSca.mSlwPhaseSlots  = 0; // Phase is stack-computed at frame-build time.
    mLocalSca.mHasSlw         = (aPeriodSlots != 0);
    UpdateDerivedSlwTiming();

exit:
    return error;
}

void DirectHandler::GetSlwSchedule(uint16_t &aPeriodSlots) const { aPeriodSlots = mLocalSca.mSlwPeriodSlots; }

const Mac::ScaParams &DirectHandler::GetLocalSca(void)
{
    if (mLocalSca.mHasSlw)
    {
        // Advance the reference time by the nominal frame TX latency (CSMA + scheduling
        // overhead for non-scheduled TX, e.g. Message 2). This gives the peer a phase
        // value that is accurate for when the frame is actually transmitted rather than
        // when it was built, reducing the window-estimate error on the receiving side.
        // For frames sent via ApplyThreadDirectTxScheduling (e.g. Message 4), the phase
        // is overwritten with the exact scheduled-TX-time value after this call.
        static constexpr uint32_t kFrameTxLatencyUs = 5500u;
        uint32_t                  txRefTime         = static_cast<uint32_t>(Get<Radio>().GetNow()) + kFrameTxLatencyUs;

        mLocalSca.mSlwPhaseSlots = Get<Mac::SubMac>().ComputeSlwPhaseSlotsAt(txRefTime, mSlwSlotDurationUs);
    }

    return mLocalSca;
}

Error DirectHandler::SetRamMask(const Mac::ScaParams &aParams)
{
    Error error = kErrorNone;

    VerifyOrExit(aParams.mRamOffsetUs >= Mac::ScaParams::kRamOffsetUsMin &&
                     aParams.mRamOffsetUs <= Mac::ScaParams::kRamOffsetUsMax,
                 error = kErrorInvalidArgs);

    mLocalSca.mRamAvailable = aParams.mRamAvailable;
    mLocalSca.mRamDuration  = aParams.mRamDuration;
    mLocalSca.mRamOffsetUs  = aParams.mRamOffsetUs;
    mLocalSca.mSlotDuration = aParams.mSlotDuration;
    memcpy(mLocalSca.mRamBits, aParams.mRamBits, sizeof(mLocalSca.mRamBits));
    UpdateDerivedSlwTiming();

exit:
    return error;
}

void DirectHandler::GetRamMask(Mac::ScaParams &aParams) const
{
    aParams.mRamAvailable = mLocalSca.mRamAvailable;
    aParams.mRamDuration  = mLocalSca.mRamDuration;
    aParams.mRamOffsetUs  = mLocalSca.mRamOffsetUs;
    aParams.mSlotDuration = mLocalSca.mSlotDuration;
    memcpy(aParams.mRamBits, mLocalSca.mRamBits, sizeof(aParams.mRamBits));
}

Error DirectHandler::SetSlwTimeout(uint32_t aTimeout)
{
    Error error = kErrorNone;

    VerifyOrExit(aTimeout <= kMaxSlwTimeout, error = kErrorInvalidArgs);
    mSlwTimeout = aTimeout;

exit:
    return error;
}

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

Error DirectHandler::SendScaUpdate(const Mac::ExtAddress &aPeerAddr)
{
    Error error = kErrorNone;

    VerifyOrExit(Get<DirectPeerTable>().FindPeer(aPeerAddr, Neighbor::kInStateValid) != nullptr,
                 error = kErrorNotFound);

    mScaUpdatePeerAddr = aPeerAddr;
    mScaUpdatePending  = true;
    Get<Mac::Mac>().RequestTdLinkCmdTransmission();

exit:
    return error;
}

Error DirectHandler::Unlink(const Mac::ExtAddress &aExtAddress)
{
    Error       error = kErrorNone;
    DirectPeer *peer  = Get<DirectPeerTable>().FindPeer(aExtAddress, Neighbor::kInStateValid);

    VerifyOrExit(peer != nullptr, error = kErrorNotFound);

    mTeardownKeyIndex    = peer->GetWakeKeyIndex();
    mTeardownLastScaRxTs = peer->GetLastScaRxTimestamp();
    mTeardownSlwPeriodUs = peer->GetSlwPeriodUs();
    mTeardownSlwPhaseUs  = peer->GetSlwPhaseUs();
    peer->Clear();
    Get<Mac::Mac>().RefreshThreadDirectSlwScheduling();
    DetermineNextSupervisionFireTime();
    LogInfo("TD: unlinked from %s, sending teardown", aExtAddress.ToString().AsCString());

    {
        otThreadDirectPeerInfo peerInfo;

        ClearAllBytes(peerInfo);
        static_cast<Mac::ExtAddress &>(peerInfo.mExtAddress) = aExtAddress;
        Get<Mac::Mac>().InvokeDirectEvent(OT_THREAD_DIRECT_EVENT_UNLINKED, &peerInfo);
    }

    mTeardownAddr    = aExtAddress;
    mTeardownPending = true;
    Get<Mac::Mac>().RequestTeardownTransmission();

exit:
    return error;
}

Mac::TxFrame *DirectHandler::PrepareTeardownFrame(Mac::TxFrames &aTxFrames)
{
    Mac::TxFrame *frame = nullptr;

    VerifyOrExit(mTeardownPending);
    mTeardownPending = false;

    Get<Mac::SubMac>().SetActiveBurstWakeKeyIndex(mTeardownKeyIndex);

#if OPENTHREAD_CONFIG_MULTI_RADIO
    frame = &aTxFrames.GetTxFrame(Mac::kRadioTypeIeee802154);
#else
    frame = &aTxFrames.GetTxFrame();
#endif

    if (frame->GenerateThreadDirectTeardown(Get<Mac::Mac>().GetPanId(), mTeardownAddr,
                                            Get<Mac::Mac>().GetExtAddress()) != kErrorNone)
    {
        LogWarn("TD: teardown frame build failed");
        frame = nullptr;
    }
    else
    {
        frame->SetCsmaCaEnabled(false);
        frame->SetMaxFrameRetries(0);

        // Use the SCA snapshot saved in Unlink() (before the peer entry was cleared)
        // to schedule the teardown frame to arrive at the peer's next SLW window.
        if (mTeardownSlwPeriodUs > 0)
        {
            uint32_t txRequestAheadUs =
                Mac::kDirectRequestAhead + Get<Mac::Mac>().CalculateRadioBusTransferTime(frame->GetLength());
            uint64_t firstWindowStart = mTeardownLastScaRxTs + mTeardownSlwPhaseUs;
            uint64_t minWindowStart   = static_cast<uint64_t>(Get<Radio>().GetNow()) + txRequestAheadUs;
            uint64_t nextWindowStart  = firstWindowStart;

            if (nextWindowStart < minWindowStart)
            {
                uint64_t elapsed        = minWindowStart - firstWindowStart;
                uint64_t periodsElapsed = elapsed / mTeardownSlwPeriodUs;
                nextWindowStart         = firstWindowStart + (periodsElapsed + 1) * mTeardownSlwPeriodUs;
            }

            uint64_t txDelay = nextWindowStart - mTeardownLastScaRxTs;

            if (txDelay <= NumericLimits<uint32_t>::kMax)
            {
                frame->SetChannel(Get<Mac::Mac>().GetRadioChannel());
                frame->SetTxDelay(static_cast<uint32_t>(txDelay));
                frame->SetTxDelayBaseTime(static_cast<uint32_t>(mTeardownLastScaRxTs));
            }
            else
            {
                LogWarn("TD: teardown txDelay overflow, sending unscheduled");
            }
        }
        else
        {
            LogWarn("TD: no SLW saved for teardown, sending unscheduled");
        }
    }

exit:
    return frame;
}

void DirectHandler::HandleTeardownRxd(const Mac::ExtAddress &aPeerAddr)
{
    DirectPeer *peer = Get<DirectPeerTable>().FindPeer(aPeerAddr, Neighbor::kInStateValid);

    if (peer != nullptr)
    {
        peer->Clear();
        Get<Mac::Mac>().RefreshThreadDirectSlwScheduling();
        DetermineNextSupervisionFireTime();
        LogInfo("TD: received teardown from %s", aPeerAddr.ToString().AsCString());

        {
            otThreadDirectPeerInfo peerInfo;

            ClearAllBytes(peerInfo);
            static_cast<Mac::ExtAddress &>(peerInfo.mExtAddress) = aPeerAddr;
            Get<Mac::Mac>().InvokeDirectEvent(OT_THREAD_DIRECT_EVENT_UNLINKED, &peerInfo);
        }
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
        if (!Get<DirectPeerTable>().HasPeers(DirectPeer::kInStateValid))
        {
            ResumeWakeListening();
        }
#endif
    }
    else
    {
        LogWarn("TD: teardown from unknown peer %s", aPeerAddr.ToString().AsCString());
    }
}

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

void DirectHandler::HandleWakeReceived(const Mac::WakeupInfo &aWakeupInfo)
{
    // Drop if already handling a handshake (a second WI won the race).
    VerifyOrExit(mWlState == kWlIdle);

    mWakeupInfo = aWakeupInfo;
    mWlState    = kWlAttachDelay;

    {
        uint32_t delayMs = (aWakeupInfo.mAttachDelayUs + Time::kOneMsecInUsec - 1) / Time::kOneMsecInUsec;

        LogInfo("TD WL: wake received from %s, attach delay %lums", aWakeupInfo.mExtAddress.ToString().AsCString(),
                ToUlong(delayMs));

        if (delayMs == 0)
        {
            mWlStateTimer.FireAt(TimerMilli::GetNow());
        }
        else
        {
            mWlStateTimer.FireAt(TimerMilli::GetNow() + delayMs);
        }
    }

exit:
    return;
}

void DirectHandler::HandleWlStateTimer(void)
{
    if (mWlState == kWlAttachDelay)
    {
        mWlState = kWlWaitingEnhAck;
        Get<Mac::SubMac>().SetActiveBurstWakeKeyIndex(mWakeupInfo.mWakeKeyIndex);

        // Start SLW now so GetLocalSca() computes an accurate non-zero phase.
        // The WI uses that phase to schedule message 4 to our first SLW window.
        Get<Mac::Mac>().BeginPreLinkThreadDirectSlw();

        Get<Mac::Mac>().RequestTdLinkCmdTransmission();
        ExitNow();
    }

    VerifyOrExit(mWlState == kWlWaitingPeerTdLinkCmd);

    LogInfo("TD WL: timed out waiting for peer TD Link Cmd from %s", mWakeupInfo.mExtAddress.ToString().AsCString());
    mWlState = kWlIdle;
    ResumeWakeListening();

exit:
    return;
}

void DirectHandler::StartWlPeerTdLinkCmdTimeout(void)
{
    uint64_t timeoutUs = static_cast<uint64_t>(kWlPeerTdLinkCmdTimeoutPeriods) * mSlwPeriodUs;
    uint32_t timeoutMs = static_cast<uint32_t>((timeoutUs + Time::kOneMsecInUsec - 1) / Time::kOneMsecInUsec);

    if (timeoutMs < kMinWlPeerTdLinkCmdTimeoutMs)
    {
        timeoutMs = kMinWlPeerTdLinkCmdTimeoutMs;
    }

    mWlStateTimer.FireAt(TimerMilli::GetNow() + timeoutMs);
    LogInfo("TD WL: waiting up to %lums for peer TD Link Cmd", ToUlong(timeoutMs));
}

bool DirectHandler::TryAddWlPeer(const Mac::ExtAddress &aPeerAddr,
                                 const Mac::ScaParams  &aPeerSca,
                                 uint64_t               aRxTimestamp)
{
    DirectPeer *peer;

    if ((mWlState != kWlWaitingPeerTdLinkCmd) || (aPeerAddr != mWakeupInfo.mExtAddress))
    {
        return false;
    }

    peer = Get<DirectPeerTable>().FindPeer(aPeerAddr, DirectPeer::kInStateAny);

    if (peer != nullptr)
    {
        peer->Clear();
    }
    else
    {
        peer = Get<DirectPeerTable>().GetNewPeer();
    }

    if (peer == nullptr)
    {
        LogWarn("TD WL: no buffer to add peer %s", aPeerAddr.ToString().AsCString());
        mWlState = kWlIdle;
        ResumeWakeListening();
        return true;
    }

    peer->SetExtAddress(aPeerAddr);
    peer->SetState(Neighbor::kStateValid);
    peer->SetWakeKeyIndex(mWakeupInfo.mWakeKeyIndex);
    peer->SetWakeKeyUsed(true);
    peer->SetLastWakeFrameCounter(mWakeupInfo.mWakeFrameCounter);
    peer->SetHasScaSchedule(aPeerSca.mHasSlw);
    peer->SetCoexEnabled(aPeerSca.mHasSlw && aPeerSca.mRamAvailable);

    if (aPeerSca.mHasSlw)
    {
        peer->UpdateSca(aPeerSca, aRxTimestamp);
    }
    else
    {
        Mac::ScaParams emptySca = Mac::ScaParams();

        emptySca.mSlotDuration = Mac::ScaSlotDuration::k625Usec;
        emptySca.mRamAvailable = false;
        peer->SetSca(emptySca);
        peer->SetLastScaRxTimestamp(0);
    }

    mWlStateTimer.Stop();

    Get<Mac::Mac>().RefreshThreadDirectSlwScheduling();
    DetermineNextSupervisionFireTime();
    LogInfo("TD WL: linked with %s (peer TD Link Cmd received)", aPeerAddr.ToString().AsCString());

    mWlState = kWlIdle;

    {
        otThreadDirectPeerInfo peerInfo;

        ClearAllBytes(peerInfo);
        static_cast<Mac::ExtAddress &>(peerInfo.mExtAddress) = aPeerAddr;
        Get<Mac::Mac>().InvokeDirectEvent(OT_THREAD_DIRECT_EVENT_LINKED, &peerInfo);
    }

    return true;
}

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

Mac::TxFrame *DirectHandler::PrepareTdLinkCmdFrame(Mac::TxFrames &aTxFrames)
{
    Mac::TxFrame *frame = nullptr;
    Error         error = kErrorNone;

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
    if (mWiState == kWiSendingTdLinkCmd)
    {
        uint8_t supervisionIntervalByte = EncodeSupervisionIntervalMs(GetSlwTimeout());

#if OPENTHREAD_CONFIG_MULTI_RADIO
        frame = &aTxFrames.GetTxFrame(Mac::kRadioTypeIeee802154);
#else
        frame = &aTxFrames.GetTxFrame();
#endif

        SuccessOrExit(error = frame->GenerateThreadDirectLinkCommand(Get<Mac::Mac>().GetPanId(), mWiPendingPeerAddr,
                                                                     Get<Mac::Mac>().GetExtAddress(), GetLocalSca(),
                                                                     nullptr, &supervisionIntervalByte, nullptr));

        frame->SetCsmaCaEnabled(false);
        frame->SetMaxFrameRetries(0);

        {
            Mac::Address dest;
            dest.SetExtended(mWiPendingPeerAddr);
            Get<Mac::Mac>().ApplyThreadDirectTxScheduling(*frame, dest);
        }

        ExitNow();
    }
#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE

    // Post-link SCA parameter update: TD Link Command with SCA LTV only, no challenge.
    // Phase is computed fresh by GetLocalSca() from the running SLW schedule, giving the
    // peer an accurate resync point.
    if (mScaUpdatePending)
    {
        mScaUpdatePending  = false;
        mScaUpdateInFlight = true;

#if OPENTHREAD_CONFIG_MULTI_RADIO
        frame = &aTxFrames.GetTxFrame(Mac::kRadioTypeIeee802154);
#else
        frame = &aTxFrames.GetTxFrame();
#endif

        SuccessOrExit(error = frame->GenerateThreadDirectLinkCommand(Get<Mac::Mac>().GetPanId(), mScaUpdatePeerAddr,
                                                                     Get<Mac::Mac>().GetExtAddress(), GetLocalSca(),
                                                                     nullptr, nullptr, nullptr));

        frame->SetCsmaCaEnabled(false);
        frame->SetMaxFrameRetries(0);

        {
            Mac::Address dest;
            dest.SetExtended(mScaUpdatePeerAddr);
            Get<Mac::Mac>().ApplyThreadDirectTxScheduling(*frame, dest);
        }

        ExitNow();
    }

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    VerifyOrExit(mWlState == kWlWaitingEnhAck);

    // Compute the challenge now, using the wake frame counter that
    // ProcessTransmitSecurity will stamp in the TD Link Command.
    SuccessOrExit(error = Get<KeyManager>().ComputeChallenge(mWakeupInfo.mWakeKeyIndex,
                                                             Get<Mac::SubMac>().GetWakeFrameCounter(),
                                                             mWakeupInfo.mWakeFrameCounter, nullptr, 0, mWlChallenge));

    {
        uint8_t supervisionIntervalByte = EncodeSupervisionIntervalMs(GetSlwTimeout());

#if OPENTHREAD_CONFIG_MULTI_RADIO
        frame = &aTxFrames.GetTxFrame(Mac::kRadioTypeIeee802154);
#else
        frame = &aTxFrames.GetTxFrame();
#endif

        SuccessOrExit(error = frame->GenerateThreadDirectLinkCommand(
                          Get<Mac::Mac>().GetPanId(), mWakeupInfo.mExtAddress, Get<Mac::Mac>().GetExtAddress(),
                          GetLocalSca(), &mWlChallenge, &supervisionIntervalByte, nullptr));
    }

    frame->SetCsmaCaEnabled(true);
    frame->SetMaxCsmaBackoffs(Mac::kMaxCsmaBackoffsDirect);
    frame->SetMaxFrameRetries(0);

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

exit:
    if (error != kErrorNone)
    {
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
        if (mWiState == kWiSendingTdLinkCmd)
        {
            LogWarn("TD WI: link command preparation failed: %s", ErrorToString(error));
            mWiState = kWiIdle;
            Get<Mac::Mac>().InvokeDirectEvent(OT_THREAD_DIRECT_EVENT_LINK_FAILED, nullptr);
            ExitNow(frame = nullptr);
        }
#endif
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
        LogWarn("TD WL: link command preparation failed: %s", ErrorToString(error));
        mWlState = kWlIdle;
        Get<Mac::SubMac>().SetActiveBurstWakeKeyIndex(Mac::Frame::kWakeKeyIndex);
        ResumeWakeListening();
#endif
        frame = nullptr;
    }

    return frame;
}

void DirectHandler::HandleTdLinkCmdTxDone(Mac::TxFrame &aFrame, Mac::RxFrame *aAckFrame, Error aError)
{
    OT_UNUSED_VARIABLE(aFrame);

    Error error = aError;

    if (mScaUpdateInFlight)
    {
        mScaUpdateInFlight = false;

        if (error != kErrorNone)
        {
            LogWarn("TD: SCA update to %s failed: %s", mScaUpdatePeerAddr.ToString().AsCString(), ErrorToString(error));
        }
        else
        {
            LogInfo("TD: SCA update sent to %s", mScaUpdatePeerAddr.ToString().AsCString());
        }

        ExitNow();
    }

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
    if (mWiState == kWiSendingTdLinkCmd)
    {
        mWiState = kWiIdle;

        if ((error != kErrorNone) || (aAckFrame == nullptr))
        {
            DirectPeer *peer = Get<DirectPeerTable>().FindPeer(mWiPendingPeerAddr, DirectPeer::kInStateAny);

            if (error != kErrorNone)
            {
                LogWarn("TD WI: own TD Link Command TX failed: %s", ErrorToString(error));
            }
            else
            {
                LogWarn("TD WI: own TD Link Command not ACKed");
            }

            if (peer != nullptr)
            {
                peer->Clear();
            }

            Get<Mac::Mac>().RefreshThreadDirectSlwScheduling();
            Get<Mac::Mac>().InvokeDirectEvent(OT_THREAD_DIRECT_EVENT_LINK_FAILED, nullptr);
            ExitNow();
        }

        // WI's TD Link Command carries only SCA LTV (no challenge), so the WL's
        // Enh-ACK is a plain ACK with no Thread Header IE required.  Receipt of any
        // Enh-ACK is sufficient to confirm the WL accepted the exchange.

        {
            DirectPeer *peer = Get<DirectPeerTable>().FindPeer(mWiPendingPeerAddr, DirectPeer::kInStateValid);

            if (peer != nullptr)
            {
                peer->SetLastActivityTime(TimerMilli::GetNow());
                peer->ResetSupervisionProbeAttempts();
                peer->SetSupervisionProbePending(false);
            }
        }

        Get<Mac::Mac>().RefreshThreadDirectSlwScheduling();
        DetermineNextSupervisionFireTime();
        LogInfo("TD WI: linked with %s (own TD Link Cmd ACKed)", mWiPendingPeerAddr.ToString().AsCString());

        {
            otThreadDirectPeerInfo peerInfo;

            ClearAllBytes(peerInfo);
            static_cast<Mac::ExtAddress &>(peerInfo.mExtAddress) = mWiPendingPeerAddr;
            Get<Mac::Mac>().InvokeDirectEvent(OT_THREAD_DIRECT_EVENT_LINKED, &peerInfo);
        }

        ExitNow();
    }
#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    VerifyOrExit(mWlState == kWlWaitingEnhAck);
    Get<Mac::SubMac>().SetActiveBurstWakeKeyIndex(Mac::Frame::kWakeKeyIndex);

    if (error != kErrorNone)
    {
        LogInfo("TD WL: link command TX failed: %s", ErrorToString(error));
        ExitNow();
    }

    if (aAckFrame == nullptr)
    {
        LogInfo("TD WL: link command TX no Enh-ACK");
        error = kErrorNoAck;
        ExitNow();
    }

    {
        const uint8_t    *ieData;
        uint8_t           ieLen;
        Mac::ChallengeLtv echoed;
        Mac::ScaParams    wiSca;

        memset(&echoed, 0, sizeof(echoed));
        memset(&wiSca, 0, sizeof(wiSca));

        ieData = aAckFrame->GetHeaderIe(Mac::ThreadHeaderIe::kElementId);

        if (ieData == nullptr)
        {
            LogWarn("TD WL: Enh-ACK has no Thread Header IE");
            error = kErrorNotFound;
            ExitNow();
        }

        ieLen = reinterpret_cast<const Mac::HeaderIe *>(ieData)->GetLength();

        if (Mac::ParseThreadHeaderIe(ieData + sizeof(Mac::HeaderIe), ieLen, &wiSca, &echoed) != kErrorNone)
        {
            LogWarn("TD WL: Enh-ACK Thread Header IE parse failed");
            error = kErrorParse;
            ExitNow();
        }

        if (memcmp(echoed.mChallenge, mWlChallenge.mChallenge, Mac::ChallengeLtv::kLength) != 0)
        {
            LogWarn("TD WL: challenge echo mismatch");
            error = kErrorSecurity;
            ExitNow();
        }
        OT_UNUSED_VARIABLE(wiSca);
        mWlState = kWlWaitingPeerTdLinkCmd;
        StartWlPeerTdLinkCmdTimeout();
        LogInfo("TD WL: challenge echoed by %s, waiting for peer TD Link Cmd",
                mWakeupInfo.mExtAddress.ToString().AsCString());
        ExitNow();
    }

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

exit:
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
    if (mWiState == kWiSendingTdLinkCmd)
    {
        mWiState = kWiIdle;
        LogWarn("TD WI: own TD Link Command failed (%s)", ErrorToString(error));
        Get<Mac::Mac>().InvokeDirectEvent(OT_THREAD_DIRECT_EVENT_LINK_FAILED, nullptr);
        return;
    }
#endif
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    if (mWlState == kWlWaitingEnhAck)
    {
        mWlState = kWlIdle;

        if (error != kErrorNone)
        {
            ResumeWakeListening();
        }
    }
#endif
}

void DirectHandler::HandleTdTeardownTxDone(Mac::TxFrame &aFrame, Error aError)
{
    OT_UNUSED_VARIABLE(aFrame);
    OT_UNUSED_VARIABLE(aError);

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    // Since we remove the peer while sending the teardown frame, we can safey resume wake listening.
    if (!Get<DirectPeerTable>().HasPeers(DirectPeer::kInStateValid))
    {
        ResumeWakeListening();
    }
#endif
}

uint32_t DirectHandler::GetEffectiveSupervisionIntervalMs(const DirectPeer &aPeer) const
{
    uint32_t local = mSlwTimeout;
    uint32_t peer  = aPeer.GetSupervisionIntervalMs();
    uint32_t effective;

    if (peer == 0)
    {
        effective = local;
    }
    else if (local == 0)
    {
        effective = peer;
    }
    else
    {
        effective = (local < peer) ? local : peer;
    }

    return effective;
}

uint32_t DirectHandler::GetSupervisionRetryDelayMs(const DirectPeer &aPeer) const
{
    // A retrying probe is paced at the peer's SLW schedule, otherwise we use
    // `kMinSupervisionRetryDelayMs`
    uint32_t delayMs = static_cast<uint32_t>(aPeer.GetSlwPeriodUs() / 1000);

    return (delayMs < kMinSupervisionRetryDelayMs) ? kMinSupervisionRetryDelayMs : delayMs;
}

TimeMilli DirectHandler::GetSupervisionDeadline(const DirectPeer &aPeer, uint32_t aIntervalMs) const
{
    // A peer with at least one outstanding failure is in a fast-retry backoff anchored to
    // the completion of its last probe attempt, not the (now stale) last-activity time --
    // otherwise every re-check would see the same expired idle deadline and retry with no
    // delay at all.
    return (aPeer.GetSupervisionProbeAttempts() > 0)
               ? (aPeer.GetLastSupervisionProbeTime() + GetSupervisionRetryDelayMs(aPeer))
               : (aPeer.GetLastActivityTime() + aIntervalMs);
}

void DirectHandler::RequestSupervisionProbe(DirectPeer &aPeer)
{
    VerifyOrExit(!mSupervisionPending);

    aPeer.SetSupervisionProbePending(true);
    mSupervisionAddr     = aPeer.GetExtAddress();
    mSupervisionKeyIndex = aPeer.GetWakeKeyIndex();
    mSupervisionPending  = true;
    Get<Mac::Mac>().RequestTdSupervisionTransmission();

exit:
    return;
}

void DirectHandler::DetermineNextSupervisionFireTime(void)
{
    NextFireTime nextFireTime;

    for (DirectPeer &peer : Get<DirectPeerTable>().Iterate(DirectPeer::kInStateValid))
    {
        uint32_t intervalMs = GetEffectiveSupervisionIntervalMs(peer);

        if (intervalMs == 0)
        {
            continue;
        }

        nextFireTime.UpdateIfEarlier(peer.IsSupervisionProbePending() ? (nextFireTime.GetNow() + intervalMs)
                                                                      : GetSupervisionDeadline(peer, intervalMs));
    }

    mSupervisionTimer.FireAt(nextFireTime);
}

void DirectHandler::HandleSupervisionTimer(void)
{
    TimeMilli now = TimerMilli::GetNow();

    for (DirectPeer &peer : Get<DirectPeerTable>().Iterate(DirectPeer::kInStateValid))
    {
        uint32_t intervalMs = GetEffectiveSupervisionIntervalMs(peer);

        if ((intervalMs != 0) && !peer.IsSupervisionProbePending() && (now >= GetSupervisionDeadline(peer, intervalMs)))
        {
            RequestSupervisionProbe(peer);
        }
    }

    DetermineNextSupervisionFireTime();
}

Mac::TxFrame *DirectHandler::PrepareSupervisionFrame(Mac::TxFrames &aTxFrames)
{
    Mac::TxFrame *frame = nullptr;

    VerifyOrExit(mSupervisionPending);
    mSupervisionPending = false;

    Get<Mac::SubMac>().SetActiveBurstWakeKeyIndex(mSupervisionKeyIndex);

#if OPENTHREAD_CONFIG_MULTI_RADIO
    frame = &aTxFrames.GetTxFrame(Mac::kRadioTypeIeee802154);
#else
    frame = &aTxFrames.GetTxFrame();
#endif

    if (frame->GenerateThreadDirectSupervision(Get<Mac::Mac>().GetPanId(), mSupervisionAddr,
                                               Get<Mac::Mac>().GetExtAddress(), GetLocalSca()) != kErrorNone)
    {
        LogWarn("TD: supervision frame build failed");
        ExitNow(frame = nullptr);
    }

    frame->SetCsmaCaEnabled(false);
    frame->SetMaxFrameRetries(0);

    {
        Mac::Address dest;

        dest.SetExtended(mSupervisionAddr);
        Get<Mac::Mac>().ApplyThreadDirectTxScheduling(*frame, dest);
    }

exit:
    return frame;
}

void DirectHandler::HandleSupervisionTxDone(Mac::TxFrame &aFrame, Error aError)
{
    DirectPeer *peer = Get<DirectPeerTable>().FindPeer(mSupervisionAddr, DirectPeer::kInStateValid);

    OT_UNUSED_VARIABLE(aFrame);

    VerifyOrExit(peer != nullptr);

    peer->SetSupervisionProbePending(false);
    peer->SetLastSupervisionProbeTime(TimerMilli::GetNow());

    if (aError == kErrorNone)
    {
        peer->ResetSupervisionProbeAttempts();
        peer->SetLastActivityTime(TimerMilli::GetNow());
        LogDebg("TD: supervision probe to %s ACKed", mSupervisionAddr.ToString().AsCString());
        DetermineNextSupervisionFireTime();
        ExitNow();
    }

    peer->IncrementSupervisionProbeAttempts();

    if (peer->GetSupervisionProbeAttempts() >= kMaxSupervisionFailures)
    {
        LogWarn("TD: supervision probe to %s failed %u times, unlinking", mSupervisionAddr.ToString().AsCString(),
                kMaxSupervisionFailures);
        IgnoreError(Unlink(mSupervisionAddr));
        ExitNow();
    }

    // The retry is scheduled a fast-retry delay out from this attempt's completion,
    // rather than requested immediately.
    LogInfo("TD: supervision probe to %s failed (%u/%u), retrying", mSupervisionAddr.ToString().AsCString(),
            peer->GetSupervisionProbeAttempts(), kMaxSupervisionFailures);
    DetermineNextSupervisionFireTime();

exit:
    return;
}

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

void DirectHandler::ResumeWakeListening(void)
{
    mWlStateTimer.Stop();

    // Stop pre-link SLW if it was started in HandleWlAttachDelayTimer but
    // linking failed before a valid peer was created.
    Get<Mac::Mac>().RefreshThreadDirectSlwScheduling();

    IgnoreError(Get<Mac::Mac>().SetWakeupListenEnabled(true));
}

#if !OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
void DirectHandler::HandleTdDirectFrame(const Mac::RxFrame &aFrame)
{
    Mac::Address   srcAddress;
    const uint8_t *ieData;
    uint8_t        ieLen;
    Mac::ScaParams wiSca;

    SuccessOrExit(aFrame.GetSrcAddr(srcAddress));
    VerifyOrExit(srcAddress.IsExtended());

    ieData = aFrame.GetHeaderIe(Mac::ThreadHeaderIe::kElementId);
    VerifyOrExit(ieData != nullptr);

    ieLen = reinterpret_cast<const Mac::HeaderIe *>(ieData)->GetLength();

    {
        bool teardown = false;

        memset(&wiSca, 0, sizeof(wiSca));
        IgnoreError(Mac::ParseThreadHeaderIe(ieData + sizeof(Mac::HeaderIe), ieLen, &wiSca, nullptr, &teardown));

        if (teardown)
        {
            HandleTeardownRxd(srcAddress.GetExtended());
            ExitNow();
        }

        IgnoreReturnValue(TryAddWlPeer(srcAddress.GetExtended(), wiSca, aFrame.GetTimestamp()));

        if (wiSca.mHasSlw)
        {
            DirectPeer *peer = Get<DirectPeerTable>().FindPeer(srcAddress.GetExtended(), DirectPeer::kInStateValid);

            if (peer != nullptr)
            {
                peer->UpdateSca(wiSca, aFrame.GetTimestamp());
                Get<Mac::Mac>().RefreshThreadDirectSlwScheduling();
            }
            else
            {
                LogInfo("TD WL: SCA update from %s ignored, no linked peer",
                        srcAddress.GetExtended().ToString().AsCString());
            }
        }

        {
            uint16_t    supervisionIntervalMs = ParseTdLinkCommandSupervisionIntervalMs(aFrame);
            DirectPeer *peer = Get<DirectPeerTable>().FindPeer(srcAddress.GetExtended(), DirectPeer::kInStateValid);

            if (peer != nullptr && supervisionIntervalMs != 0)
            {
                peer->SetSupervisionIntervalMs(supervisionIntervalMs);
            }
        }
    }

exit:
    return;
}
#endif // !OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE

void DirectHandler::HandleTdLinkCommand(const Mac::RxFrame &aFrame)
{
    Error             error = kErrorNone;
    Mac::Address      srcAddress;
    const uint8_t    *ieData;
    uint8_t           ieLen;
    Mac::ScaParams    wlSca;
    Mac::ChallengeLtv receivedChallenge;
    uint8_t           wakeKeyIndex;
    uint32_t          linkFrameCounter;

    Get<WakeupTxScheduler>().NotifyTdLinkCommandReceived();

    SuccessOrExit(error = aFrame.GetSrcAddr(srcAddress));
    VerifyOrExit(srcAddress.IsExtended(), error = kErrorDrop);

    SuccessOrExit(error = aFrame.GetKeyId(wakeKeyIndex));
    SuccessOrExit(error = aFrame.GetFrameCounter(linkFrameCounter));

    ieData = aFrame.GetHeaderIe(Mac::ThreadHeaderIe::kElementId);
    VerifyOrExit(ieData != nullptr, error = kErrorNotFound);

    ieLen = reinterpret_cast<const Mac::HeaderIe *>(ieData)->GetLength();

    {
        bool teardown         = false;
        bool challengePresent = false;

        memset(&wlSca, 0, sizeof(wlSca));
        memset(&receivedChallenge, 0, sizeof(receivedChallenge));
        VerifyOrExit(Mac::ParseThreadHeaderIe(ieData + sizeof(Mac::HeaderIe), ieLen, &wlSca, &receivedChallenge,
                                              &teardown, &challengePresent) == kErrorNone,
                     error = kErrorParse);

        if (teardown)
        {
            HandleTeardownRxd(srcAddress.GetExtended());
            ExitNow();
        }

        if (!challengePresent)
        {
            // No Challenge LTV: this is the peer's own TD Link Command carrying its SCA LTV
            // (the 4th message in the handshake).  Update the peer's schedule and return.
            DirectPeer *peer = nullptr;

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
            IgnoreReturnValue(TryAddWlPeer(srcAddress.GetExtended(), wlSca, aFrame.GetTimestamp()));
#endif

            peer = Get<DirectPeerTable>().FindPeer(srcAddress.GetExtended(), DirectPeer::kInStateValid);

            if (peer != nullptr)
            {
                uint16_t supervisionIntervalMs = ParseTdLinkCommandSupervisionIntervalMs(aFrame);

                if (wlSca.mHasSlw)
                {
                    peer->UpdateSca(wlSca, aFrame.GetTimestamp());
                    Get<Mac::Mac>().RefreshThreadDirectSlwScheduling();
                    LogInfo("TD: updated peer SCA from %s", srcAddress.GetExtended().ToString().AsCString());
                }

                if (supervisionIntervalMs != 0)
                {
                    peer->SetSupervisionIntervalMs(supervisionIntervalMs);
                }
            }

            ExitNow();
        }
    }

    {
        DirectPeer *peer = Get<DirectPeerTable>().FindPeer(srcAddress.GetExtended(), DirectPeer::kInStateAny);

        if (peer == nullptr)
        {
            peer = Get<DirectPeerTable>().GetNewPeer();
            VerifyOrExit(peer != nullptr, error = kErrorNoBufs);
        }
        else if (peer->GetState() == Neighbor::kStateValid)
        {
            // Already linked with this peer; re-echo the challenge so
            // the WL can complete its own handshake on retry.
            LogInfo("TD WI: re-echoing challenge for already-linked peer %s",
                    srcAddress.GetExtended().ToString().AsCString());
            ExitNow();
        }
        uint16_t supervisionIntervalMs = ParseTdLinkCommandSupervisionIntervalMs(aFrame);

        peer->SetExtAddress(srcAddress.GetExtended());
        peer->SetState(Neighbor::kStateValid);
        peer->SetWakeKeyIndex(wakeKeyIndex);
        peer->SetWakeKeyUsed(true);
        peer->SetLastWakeFrameCounter(linkFrameCounter);

        if (wlSca.mHasSlw)
        {
            peer->UpdateSca(wlSca, aFrame.GetTimestamp());
        }

        if (supervisionIntervalMs != 0)
        {
            peer->SetSupervisionIntervalMs(supervisionIntervalMs);
        }

        LogInfo("TD WI: WL handshake done, sending own TD Link Cmd to %s",
                srcAddress.GetExtended().ToString().AsCString());
    }

    Get<WakeupTxScheduler>().Stop();

    // Start WI's own SLW before building message 4 so GetLocalSca() returns an
    // accurate non-zero phase. When phase-1 uses the same SLW period on both
    // sides, place the WI sample point half a period away from the WL sample to
    // keep their windows from overlapping.
    {
        uint32_t wiSampleTimeRadio;

        // Revisit: This may be achieved by using the ram available bitmask when we add the support.
        if (CalculateMidpointSlwSampleTime(wlSca, mLocalSca, static_cast<uint32_t>(aFrame.GetTimestamp()),
                                           static_cast<uint32_t>(Get<Radio>().GetNow()), wiSampleTimeRadio))
        {
            Get<Mac::Mac>().BeginPreLinkThreadDirectSlw(wiSampleTimeRadio);
        }
        else
        {
            // fallback to the default behavior of starting SLW at the current radio time
            Get<Mac::Mac>().BeginPreLinkThreadDirectSlw();
        }
    }

    mWiState           = kWiSendingTdLinkCmd;
    mWiPendingPeerAddr = srcAddress.GetExtended();
    Get<Mac::Mac>().RequestTdLinkCmdTransmission();

exit:
    if (error != kErrorNone)
    {
        LogWarn("TD WI: link handshake failed (%s)", ErrorToString(error));
        Get<Mac::Mac>().RefreshThreadDirectSlwScheduling();
        Get<Mac::Mac>().InvokeDirectEvent(OT_THREAD_DIRECT_EVENT_LINK_FAILED, nullptr);
    }
}

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE

void DirectHandler::UpdateDerivedSlwTiming(void)
{
    mSlwSlotDurationUs = ScaSlotDurationToUs(mLocalSca.mSlotDuration);
    mSlwPeriodUs       = static_cast<uint64_t>(mSlwSlotDurationUs) * mLocalSca.mSlwPeriodSlots;
}

} // namespace ot

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
