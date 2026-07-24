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
 *   This file tests `ThreadDirectTxScheduler`'s handling of back-to-back queued
 *   direct-transmission messages: `TrySchedule()`'s busy/pending gating,
 *   `HandleFrameRequest()`'s speculative-vs-actual frame-length recomputation
 *   (it schedules against `kMaxFrameSize` up front, then re-derives the request-ahead
 *   time from the real prepared frame length), and `HandleSentFrame()`'s
 *   Clear()-unblocks-the-next-message handoff -- none of which the existing
 *   `test_td_link_handshake.cpp` exercises directly (it only ever has one
 *   in-flight message at a time).
 */

#include <stdio.h>
#include <string.h>

#include <openthread/thread_direct.h>

#include "net/ip6.hpp"
#include "net/udp6.hpp"
#include "platform/nexus_core.hpp"
#include "platform/nexus_node.hpp"
#include "thread/mle.hpp"

namespace ot {
namespace Nexus {

static constexpr uint32_t kHandshakeTimeMs = 5 * 1000;

static constexpr uint8_t  kNetworkKey[OT_NETWORK_KEY_SIZE] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                                              0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static constexpr uint16_t kPanId                           = 0xD001;

struct TdEventInfo
{
    uint32_t            mLinkedCount;
    otThreadDirectEvent mLastEvent;
};

static void HandleTdEvent(otThreadDirectEvent aEvent, const otThreadDirectPeerInfo *aPeerInfo, void *aContext)
{
    TdEventInfo *info = static_cast<TdEventInfo *>(aContext);

    OT_UNUSED_VARIABLE(aPeerInfo);

    info->mLastEvent = aEvent;

    if (aEvent == OT_THREAD_DIRECT_EVENT_LINKED)
    {
        info->mLinkedCount++;
    }
}

// Tracks every received datagram's arrival order (via each payload's distinguishing first
// byte) so a burst of back-to-back sends of varying sizes can be checked for in-order,
// lossless delivery through the TD Tx Scheduler's single-in-flight-message state machine.
struct UdpRxInfo
{
    static constexpr uint8_t kMaxOrderEntries = 8;

    uint32_t mRxCount;
    uint16_t mLastFullLen;
    uint8_t  mRxOrder[kMaxOrderEntries];
};

static void HandleUdpReceive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    OT_UNUSED_VARIABLE(aMessageInfo);

    UdpRxInfo *info = static_cast<UdpRxInfo *>(aContext);
    uint16_t   len  = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);
    uint8_t    firstByte;

    info->mLastFullLen = len;

    if ((len > 0) && (info->mRxCount < UdpRxInfo::kMaxOrderEntries))
    {
        otMessageRead(aMessage, otMessageGetOffset(aMessage), &firstByte, sizeof(firstByte));
        info->mRxOrder[info->mRxCount] = firstByte;
    }

    info->mRxCount++;
}

// Sends a UDP datagram of the given length from `aFrom` to `aToAddr`:`aToPort`, filled with
// a byte pattern starting at `aFirstByte` (identifies this datagram at the receiver).
static void SendUdpDatagram(Node               &aFrom,
                            const Ip6::Address &aToAddr,
                            uint16_t            aToPort,
                            uint16_t            aLength,
                            uint8_t             aFirstByte)
{
    otUdpSocket   socket;
    otSockAddr    sockAddr;
    otMessageInfo msgInfo;
    otMessage    *msg;
    uint8_t       payload[150];

    OT_ASSERT(aLength <= sizeof(payload));

    for (uint16_t i = 0; i < aLength; i++)
    {
        payload[i] = static_cast<uint8_t>(aFirstByte + i);
    }

    memset(&socket, 0, sizeof(socket));
    memset(&sockAddr, 0, sizeof(sockAddr));
    sockAddr.mPort = aToPort;

    SuccessOrQuit(otUdpOpen(&aFrom.GetInstance(), &socket, nullptr, nullptr));
    SuccessOrQuit(otUdpBind(&aFrom.GetInstance(), &socket, &sockAddr, OT_NETIF_THREAD_HOST));

    msg = otUdpNewMessage(&aFrom.GetInstance(), nullptr);
    VerifyOrQuit(msg != nullptr);
    SuccessOrQuit(otMessageAppend(msg, payload, aLength));

    memset(&msgInfo, 0, sizeof(msgInfo));
    AsCoreType(&msgInfo.mPeerAddr) = aToAddr;
    msgInfo.mPeerPort              = aToPort;

    SuccessOrQuit(otUdpSend(&aFrom.GetInstance(), &socket, msg, &msgInfo));
    SuccessOrQuit(otUdpClose(&aFrom.GetInstance(), &socket));
}

// Queues four back-to-back messages -- alternating small and near-`kMaxFrameSize` (150
// byte) lengths -- before any of them have been transmitted, then verifies all four are
// delivered, in order, with no drops. This exercises:
//  - `ThreadDirectTxScheduler::IsPending()`/`TrySchedule()`'s busy gating: only the first
//    message is handed to the scheduler immediately; the other three sit in
//    `MeshForwarder`'s send queue until each prior message's `HandleSentFrame()` -> `Clear()`
//    unblocks `ScheduleTransmissionTask()`.
//  - `HandleFrameRequest()`'s speculative-vs-actual frame-length handling: `TrySchedule()`
//    always books its initial window against the worst-case `kMaxFrameSize`, then
//    `HandleFrameRequest()` re-derives `mRequestAheadUs` from the real prepared frame's
//    length once it's known -- alternating small/large messages forces this recomputation
//    to run repeatedly with different actual lengths back-to-back.
void TestTdTxSchedulerBackToBackMessages(void)
{
    // SLW period: 400 slots x 625 us/slot = 250 ms.
    static constexpr uint16_t kSlwPeriodSlots = 400;
    static constexpr uint32_t kSlwPeriodMs    = 250;
    static constexpr uint16_t kUdpPort        = 49152;

    Core nexus;

    Node &wi = nexus.CreateNode();
    Node &wl = nexus.CreateNode();

    TdEventInfo wiEvents;
    TdEventInfo wlEvents;
    UdpRxInfo   wlUdpRx;

    memset(&wiEvents, 0, sizeof(wiEvents));
    memset(&wlEvents, 0, sizeof(wlEvents));
    memset(&wlUdpRx, 0, sizeof(wlUdpRx));

    wi.SetName("WI");
    wl.SetName("WL");

    AllowLinkBetween(wi, wl);

    nexus.AdvanceTime(0);

    SuccessOrQuit(Instance::SetGlobalLogLevel(kLogLevelDebg));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 1: Configure both nodes with a shared network key and SLW period of 400 slots");

    otNetworkKey networkKey;
    memcpy(networkKey.m8, kNetworkKey, sizeof(kNetworkKey));

    SuccessOrQuit(otThreadSetNetworkKey(&wi.GetInstance(), &networkKey));
    SuccessOrQuit(otThreadSetNetworkKey(&wl.GetInstance(), &networkKey));

    SuccessOrQuit(otLinkSetPanId(&wi.GetInstance(), kPanId));
    SuccessOrQuit(otLinkSetPanId(&wl.GetInstance(), kPanId));

    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wi.GetInstance(), kSlwPeriodSlots));
    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wl.GetInstance(), kSlwPeriodSlots));

    SuccessOrQuit(otIp6SetEnabled(&wi.GetInstance(), true));
    SuccessOrQuit(otIp6SetEnabled(&wl.GetInstance(), true));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 2: Register TD event callbacks, enable WL wake listen, and open WL's UDP socket");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    Ip6::Udp::SocketHandle wlSocket;
    Ip6::SockAddr          wlSockAddr;

    wlSocket.Clear();
    wlSockAddr.Clear();
    wlSockAddr.SetPort(kUdpPort);

    SuccessOrQuit(wl.Get<Ip6::Udp>().Open(wlSocket, Ip6::kNetifThreadInternal, HandleUdpReceive, &wlUdpRx));
    SuccessOrQuit(wl.Get<Ip6::Udp>().Bind(wlSocket, wlSockAddr));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 3: WI starts wake burst targeting WL and the handshake completes");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(), &wlAddr, OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));
        VerifyOrQuit(otThreadDirectIsWakeBurstActive(&wi.GetInstance()));
    }

    nexus.AdvanceTime(kHandshakeTimeMs + 2 * kSlwPeriodMs);

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: WI queues four back-to-back datagrams (small/large/small/large) before any");
    Log("        of them have been transmitted");

    {
        Ip6::Address wlLinkLocal = wl.Get<Mle::Mle>().GetLinkLocalAddress();

        SendUdpDatagram(wi, wlLinkLocal, kUdpPort, /* aLength */ 8, /* aFirstByte */ 0xA1);
        SendUdpDatagram(wi, wlLinkLocal, kUdpPort, /* aLength */ 150, /* aFirstByte */ 0xB1);
        SendUdpDatagram(wi, wlLinkLocal, kUdpPort, /* aLength */ 8, /* aFirstByte */ 0xC1);
        SendUdpDatagram(wi, wlLinkLocal, kUdpPort, /* aLength */ 100, /* aFirstByte */ 0xD1);
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 5: Advance time for all four messages to be scheduled and delivered one at a time");

    nexus.AdvanceTime(40 * kSlwPeriodMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 6: Verify all four datagrams arrived, in order, with the right lengths");

    VerifyOrQuit(wlUdpRx.mRxCount == 4);
    VerifyOrQuit(wlUdpRx.mLastFullLen == 100);
    VerifyOrQuit(wlUdpRx.mRxOrder[0] == 0xA1);
    VerifyOrQuit(wlUdpRx.mRxOrder[1] == 0xB1);
    VerifyOrQuit(wlUdpRx.mRxOrder[2] == 0xC1);
    VerifyOrQuit(wlUdpRx.mRxOrder[3] == 0xD1);

    SuccessOrQuit(wl.Get<Ip6::Udp>().Close(wlSocket));
}

// Forces multiple consecutive direct data transmission attempts to fail with
// `kErrorNoAck` by temporarily enabling WL's radio filter, then clears the
// filter so the scheduler's retry path must deliver the original pending
// datagram before draining the next queued datagram. This exercises the staged retry behavior in
// `ThreadDirectTxScheduler::HandleSentFrame()` and `Mac::HandleTransmitDone()`:
//  - a `kErrorNoAck` must keep the original message pending instead of finalizing it,
//  - repeated failures must continue re-arming the same pending message,
//  - the retry must preserve enough frame state for the retransmission to succeed, and
//  - the next queued message must stay blocked until the retried message completes.
void TestTdTxSchedulerRetryKeepsQueuedOrder(void)
{
    static constexpr uint16_t kSlwPeriodSlots = 400;
    static constexpr uint32_t kSlwPeriodMs    = 250;
    static constexpr uint16_t kUdpPort        = 49153;
    static constexpr uint8_t  kBlockedRetries = 3;

    Core nexus;

    Node &wi = nexus.CreateNode();
    Node &wl = nexus.CreateNode();

    TdEventInfo wiEvents;
    TdEventInfo wlEvents;
    UdpRxInfo   wlUdpRx;

    memset(&wiEvents, 0, sizeof(wiEvents));
    memset(&wlEvents, 0, sizeof(wlEvents));
    memset(&wlUdpRx, 0, sizeof(wlUdpRx));

    wi.SetName("WI-retry");
    wl.SetName("WL-retry");

    AllowLinkBetween(wi, wl);

    nexus.AdvanceTime(0);

    Log("---------------------------------------------------------------------------------------");
    Log("Retry Step 1: Configure both nodes with a shared network key and SLW period of 400 slots");

    otNetworkKey networkKey;
    memcpy(networkKey.m8, kNetworkKey, sizeof(kNetworkKey));

    SuccessOrQuit(otThreadSetNetworkKey(&wi.GetInstance(), &networkKey));
    SuccessOrQuit(otThreadSetNetworkKey(&wl.GetInstance(), &networkKey));

    SuccessOrQuit(otLinkSetPanId(&wi.GetInstance(), kPanId));
    SuccessOrQuit(otLinkSetPanId(&wl.GetInstance(), kPanId));

    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wi.GetInstance(), kSlwPeriodSlots));
    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wl.GetInstance(), kSlwPeriodSlots));

    // This test exercises `ThreadDirectTxScheduler`'s own data-retry recovery across a
    // deliberately long radio outage; disable link supervision on both sides so it does not
    // independently declare link loss and unlink mid-outage.
    SuccessOrQuit(otThreadDirectSetSlwTimeout(&wi.GetInstance(), 0));
    SuccessOrQuit(otThreadDirectSetSlwTimeout(&wl.GetInstance(), 0));

    SuccessOrQuit(otIp6SetEnabled(&wi.GetInstance(), true));
    SuccessOrQuit(otIp6SetEnabled(&wl.GetInstance(), true));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Retry Step 2: Register TD callbacks, enable WL wake listen, and open WL's UDP socket");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    Ip6::Udp::SocketHandle wlSocket;
    Ip6::SockAddr          wlSockAddr;

    wlSocket.Clear();
    wlSockAddr.Clear();
    wlSockAddr.SetPort(kUdpPort);

    SuccessOrQuit(wl.Get<Ip6::Udp>().Open(wlSocket, Ip6::kNetifThreadInternal, HandleUdpReceive, &wlUdpRx));
    SuccessOrQuit(wl.Get<Ip6::Udp>().Bind(wlSocket, wlSockAddr));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Retry Step 3: WI starts wake burst targeting WL and waits for the handshake to complete");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(), &wlAddr, OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));
        VerifyOrQuit(otThreadDirectIsWakeBurstActive(&wi.GetInstance()));
    }

    nexus.AdvanceTime(kHandshakeTimeMs + 2 * kSlwPeriodMs);

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);

    Log("---------------------------------------------------------------------------------------");
    Log("Retry Step 4: Block WL's radio so multiple TD data transmission attempts cannot be ACKed");

    wl.Get<Mac::Mac>().SetRadioFilterEnabled(true);
    VerifyOrQuit(wl.Get<Mac::Mac>().IsRadioFilterEnabled());

    Log("---------------------------------------------------------------------------------------");
    Log("Retry Step 5: Queue two datagrams while the first one is destined to fail with repeated NoAck");

    {
        Ip6::Address wlLinkLocal = wl.Get<Mle::Mle>().GetLinkLocalAddress();

        SendUdpDatagram(wi, wlLinkLocal, kUdpPort, /* aLength */ 24, /* aFirstByte */ 0xA1);
        SendUdpDatagram(wi, wlLinkLocal, kUdpPort, /* aLength */ 80, /* aFirstByte */ 0xB1);
    }

    // Keep the WL filtered long enough for multiple TD send opportunities to be
    // missed, forcing the scheduler to re-arm the same packet more than once.
    nexus.AdvanceTime((2 * kBlockedRetries + 1) * kSlwPeriodMs);

    VerifyOrQuit(wlUdpRx.mRxCount == 0);

    Log("---------------------------------------------------------------------------------------");
    Log("Retry Step 6: Re-enable WL radio so the pending datagram must succeed after multiple retries");

    wl.Get<Mac::Mac>().SetRadioFilterEnabled(false);
    VerifyOrQuit(!wl.Get<Mac::Mac>().IsRadioFilterEnabled());

    nexus.AdvanceTime(20 * kSlwPeriodMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Retry Step 7: Verify the retried datagram is delivered first and the queued datagram follows");

    VerifyOrQuit(wlUdpRx.mRxCount == 2);
    VerifyOrQuit(wlUdpRx.mLastFullLen == 80);
    VerifyOrQuit(wlUdpRx.mRxOrder[0] == 0xA1);
    VerifyOrQuit(wlUdpRx.mRxOrder[1] == 0xB1);

    SuccessOrQuit(wl.Get<Ip6::Udp>().Close(wlSocket));
}

} // namespace Nexus
} // namespace ot

int main(void)
{
    ot::Nexus::TestTdTxSchedulerBackToBackMessages();
    ot::Nexus::TestTdTxSchedulerRetryKeepsQueuedOrder();

    printf("All tests passed\n");
    return 0;
}
