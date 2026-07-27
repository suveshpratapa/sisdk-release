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
 *   This file tests Thread Direct link supervision: Supervision Interval negotiation,
 *   idle-triggered keepalive probes, retry-based link loss detection and recovery, and
 *   SCA LTV delivery on ordinary data frames.
 */

#include <stdio.h>
#include <string.h>

#include <openthread/thread_direct.h>

#include "mac/direct_handler.hpp"
#include "net/ip6.hpp"
#include "net/udp6.hpp"
#include "platform/nexus_core.hpp"
#include "platform/nexus_node.hpp"
#include "thread/direct_peer_table.hpp"
#include "thread/mle.hpp"

namespace ot {
namespace Nexus {

static constexpr uint32_t kHandshakeTimeMs = 5 * 1000;

static constexpr uint8_t  kNetworkKey[OT_NETWORK_KEY_SIZE] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                                              0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static constexpr uint16_t kPanId                           = 0xD001;

// SLW period: 800 slots x 625 us/slot = 500 ms.
static constexpr uint16_t kSlwPeriodSlots = 800;
static constexpr uint32_t kSlwPeriodMs    = 500;

struct TdEventInfo
{
    uint32_t            mLinkedCount;
    uint32_t            mUnlinkedCount;
    uint32_t            mWakeReceivedCount;
    otThreadDirectEvent mLastEvent;
    otExtAddress        mLastPeerAddr;
};

static void HandleTdEvent(otThreadDirectEvent aEvent, const otThreadDirectPeerInfo *aPeerInfo, void *aContext)
{
    TdEventInfo *info = static_cast<TdEventInfo *>(aContext);

    info->mLastEvent = aEvent;

    if (aPeerInfo != nullptr)
    {
        info->mLastPeerAddr = aPeerInfo->mExtAddress;
    }

    switch (aEvent)
    {
    case OT_THREAD_DIRECT_EVENT_LINKED:
        info->mLinkedCount++;
        break;
    case OT_THREAD_DIRECT_EVENT_UNLINKED:
        info->mUnlinkedCount++;
        break;
    case OT_THREAD_DIRECT_EVENT_WAKE_RECEIVED:
        info->mWakeReceivedCount++;
        break;
    default:
        break;
    }
}

struct UdpRxInfo
{
    uint32_t mRxCount;
};

static void HandleUdpReceive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    OT_UNUSED_VARIABLE(aMessage);
    OT_UNUSED_VARIABLE(aMessageInfo);

    static_cast<UdpRxInfo *>(aContext)->mRxCount++;
}

// Returns the minimum of two Supervision Interval values, treating 0 (no requirement) as
// deferring entirely to the other side -- the same rule `DirectHandler` applies internally.
static uint32_t EffectiveSupervisionIntervalMs(uint32_t aLocalMs, uint32_t aPeerMs)
{
    uint32_t effectiveMs;

    if (aPeerMs == 0)
    {
        effectiveMs = aLocalMs;
    }
    else if (aLocalMs == 0)
    {
        effectiveMs = aPeerMs;
    }
    else
    {
        effectiveMs = (aLocalMs < aPeerMs) ? aLocalMs : aPeerMs;
    }

    return effectiveMs;
}

// WI configures a longer local Supervision Interval than WL; verifies the effective interval
// negotiated for the link -- the minimum of the two peers' advertised values -- resolves to
// WL's shorter value on both sides.
void TestTdSupervisionNegotiation(void)
{
    static constexpr uint32_t kWiIntervalMs = 2000;
    static constexpr uint32_t kWlIntervalMs = 500;

    Core nexus;

    Node &wi = nexus.CreateNode();
    Node &wl = nexus.CreateNode();

    TdEventInfo wiEvents;
    TdEventInfo wlEvents;

    memset(&wiEvents, 0, sizeof(wiEvents));
    memset(&wlEvents, 0, sizeof(wlEvents));

    wi.SetName("WI");
    wl.SetName("WL");

    AllowLinkBetween(wi, wl);

    nexus.AdvanceTime(0);

    SuccessOrQuit(Instance::SetGlobalLogLevel(kLogLevelDebg));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 1: Configure both nodes with a shared network key and asymmetric Supervision Intervals");

    otNetworkKey networkKey;
    memcpy(networkKey.m8, kNetworkKey, sizeof(kNetworkKey));

    SuccessOrQuit(otThreadSetNetworkKey(&wi.GetInstance(), &networkKey));
    SuccessOrQuit(otThreadSetNetworkKey(&wl.GetInstance(), &networkKey));

    SuccessOrQuit(otLinkSetPanId(&wi.GetInstance(), kPanId));
    SuccessOrQuit(otLinkSetPanId(&wl.GetInstance(), kPanId));

    SuccessOrQuit(otThreadDirectSetSlwTimeout(&wi.GetInstance(), kWiIntervalMs));
    SuccessOrQuit(otThreadDirectSetSlwTimeout(&wl.GetInstance(), kWlIntervalMs));

    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wi.GetInstance(), kSlwPeriodSlots));
    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wl.GetInstance(), kSlwPeriodSlots));

    SuccessOrQuit(otIp6SetEnabled(&wi.GetInstance(), true));
    SuccessOrQuit(otIp6SetEnabled(&wl.GetInstance(), true));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 2: Register TD event callbacks and enable WL wake listen");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 3: WI starts wake burst targeting WL");

    SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(),
                                       reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress),
                                       OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));
    VerifyOrQuit(otThreadDirectIsWakeBurstActive(&wi.GetInstance()));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: Advance time for handshake to complete, including WI's own TD Link Command");

    nexus.AdvanceTime(kHandshakeTimeMs + 2 * kSlwPeriodMs);

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 5: Verify each side received the other's advertised Supervision Interval");

    DirectPeer *wiViewOfWl = wi.Get<DirectPeerTable>().FindPeer(wl.mRadio.mExtAddress, DirectPeer::kInStateValid);
    DirectPeer *wlViewOfWi = wl.Get<DirectPeerTable>().FindPeer(wi.mRadio.mExtAddress, DirectPeer::kInStateValid);

    VerifyOrQuit(wiViewOfWl != nullptr);
    VerifyOrQuit(wlViewOfWi != nullptr);

    VerifyOrQuit(wiViewOfWl->GetSupervisionIntervalMs() == kWlIntervalMs);
    VerifyOrQuit(wlViewOfWi->GetSupervisionIntervalMs() == kWiIntervalMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 6: Verify the effective interval -- min(local, peer) -- resolves to 500 ms on both sides");

    uint32_t wiEffectiveMs = EffectiveSupervisionIntervalMs(otThreadDirectGetSlwTimeout(&wi.GetInstance()),
                                                            wiViewOfWl->GetSupervisionIntervalMs());
    uint32_t wlEffectiveMs = EffectiveSupervisionIntervalMs(otThreadDirectGetSlwTimeout(&wl.GetInstance()),
                                                            wlViewOfWi->GetSupervisionIntervalMs());

    VerifyOrQuit(wiEffectiveMs == kWlIntervalMs);
    VerifyOrQuit(wlEffectiveMs == kWlIntervalMs);
}

// With no application traffic at all for several Supervision Intervals, the idle-triggered
// probe mechanism must keep exchanging keepalive frames so the link never reaches
// UNLINKED, and every probe must succeed (zero consecutive failures) -- proving the
// periodic MAC-layer traffic that bounds the SLW drift/uncertainty guard window is actually
// flowing, not just application data.
void TestTdSupervisionIdleKeepalive(void)
{
    static constexpr uint32_t kSupervisionIntervalMs = 750;
    static constexpr uint32_t kIdleCheckpointMs      = 2 * kSlwPeriodMs;
    static constexpr uint8_t  kNumCheckpoints        = 8;

    Core nexus;

    Node &wi = nexus.CreateNode();
    Node &wl = nexus.CreateNode();

    TdEventInfo wiEvents;
    TdEventInfo wlEvents;

    memset(&wiEvents, 0, sizeof(wiEvents));
    memset(&wlEvents, 0, sizeof(wlEvents));

    wi.SetName("WI");
    wl.SetName("WL");

    AllowLinkBetween(wi, wl);

    nexus.AdvanceTime(0);

    SuccessOrQuit(Instance::SetGlobalLogLevel(kLogLevelDebg));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 1: Configure both nodes with a shared network key and a 750 ms Supervision Interval");

    otNetworkKey networkKey;
    memcpy(networkKey.m8, kNetworkKey, sizeof(kNetworkKey));

    SuccessOrQuit(otThreadSetNetworkKey(&wi.GetInstance(), &networkKey));
    SuccessOrQuit(otThreadSetNetworkKey(&wl.GetInstance(), &networkKey));

    SuccessOrQuit(otLinkSetPanId(&wi.GetInstance(), kPanId));
    SuccessOrQuit(otLinkSetPanId(&wl.GetInstance(), kPanId));

    SuccessOrQuit(otThreadDirectSetSlwTimeout(&wi.GetInstance(), kSupervisionIntervalMs));
    SuccessOrQuit(otThreadDirectSetSlwTimeout(&wl.GetInstance(), kSupervisionIntervalMs));

    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wi.GetInstance(), kSlwPeriodSlots));
    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wl.GetInstance(), kSlwPeriodSlots));

    SuccessOrQuit(otIp6SetEnabled(&wi.GetInstance(), true));
    SuccessOrQuit(otIp6SetEnabled(&wl.GetInstance(), true));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 2: Register TD event callbacks and enable WL wake listen");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 3: WI starts wake burst targeting WL and the handshake completes");

    SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(),
                                       reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress),
                                       OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));

    nexus.AdvanceTime(kHandshakeTimeMs + 2 * kSlwPeriodMs);

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);

    DirectPeer *wiViewOfWl = wi.Get<DirectPeerTable>().FindPeer(wl.mRadio.mExtAddress, DirectPeer::kInStateValid);
    DirectPeer *wlViewOfWi = wl.Get<DirectPeerTable>().FindPeer(wi.mRadio.mExtAddress, DirectPeer::kInStateValid);

    VerifyOrQuit(wiViewOfWl != nullptr);
    VerifyOrQuit(wlViewOfWi != nullptr);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: Advance time with zero application traffic across several Supervision Intervals,");
    Log("        checking after each checkpoint that keepalive probes are succeeding and no side");
    Log("        has gone idle beyond the negotiated interval");

    for (uint8_t i = 0; i < kNumCheckpoints; i++)
    {
        TimeMilli beforeWi = wiViewOfWl->GetLastActivityTime();
        TimeMilli beforeWl = wlViewOfWi->GetLastActivityTime();

        nexus.AdvanceTime(kIdleCheckpointMs);

        VerifyOrQuit(wiEvents.mUnlinkedCount == 0);
        VerifyOrQuit(wlEvents.mUnlinkedCount == 0);
        VerifyOrQuit(wiViewOfWl->GetSupervisionProbeAttempts() == 0);
        VerifyOrQuit(wlViewOfWi->GetSupervisionProbeAttempts() == 0);

        // Each idle checkpoint spans more than one Supervision Interval, so the idle
        // clock on at least one side must have been reset by a successful probe.
        VerifyOrQuit((wiViewOfWl->GetLastActivityTime() > beforeWi) || (wlViewOfWi->GetLastActivityTime() > beforeWl));
    }
}

// Enables WL's radio filter for a short span -- long enough to accumulate a handful of
// consecutive un-acked supervision probes but well short of `kMaxSupervisionFailures` -- then
// clears it. The link must survive the outage without an UNLINKED event, and probe attempts
// must return to zero once a probe succeeds again.
void TestTdSupervisionGracePeriod(void)
{
    static constexpr uint32_t kSupervisionIntervalMs = 2 * kSlwPeriodMs;
    static constexpr uint32_t kOutageMs              = 3 * kSlwPeriodMs;
    static constexpr uint32_t kRecoveryMs            = 4 * kSlwPeriodMs;

    Core nexus;

    Node &wi = nexus.CreateNode();
    Node &wl = nexus.CreateNode();

    TdEventInfo wiEvents;
    TdEventInfo wlEvents;

    memset(&wiEvents, 0, sizeof(wiEvents));
    memset(&wlEvents, 0, sizeof(wlEvents));

    wi.SetName("WI");
    wl.SetName("WL");

    AllowLinkBetween(wi, wl);

    nexus.AdvanceTime(0);

    SuccessOrQuit(Instance::SetGlobalLogLevel(kLogLevelDebg));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 1: Configure both nodes with a shared network key and a 1000 ms Supervision Interval");

    otNetworkKey networkKey;
    memcpy(networkKey.m8, kNetworkKey, sizeof(kNetworkKey));

    SuccessOrQuit(otThreadSetNetworkKey(&wi.GetInstance(), &networkKey));
    SuccessOrQuit(otThreadSetNetworkKey(&wl.GetInstance(), &networkKey));

    SuccessOrQuit(otLinkSetPanId(&wi.GetInstance(), kPanId));
    SuccessOrQuit(otLinkSetPanId(&wl.GetInstance(), kPanId));

    SuccessOrQuit(otThreadDirectSetSlwTimeout(&wi.GetInstance(), kSupervisionIntervalMs));
    SuccessOrQuit(otThreadDirectSetSlwTimeout(&wl.GetInstance(), kSupervisionIntervalMs));

    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wi.GetInstance(), kSlwPeriodSlots));
    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wl.GetInstance(), kSlwPeriodSlots));

    SuccessOrQuit(otIp6SetEnabled(&wi.GetInstance(), true));
    SuccessOrQuit(otIp6SetEnabled(&wl.GetInstance(), true));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 2: Register TD event callbacks and enable WL wake listen");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 3: WI starts wake burst targeting WL and the handshake completes");

    SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(),
                                       reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress),
                                       OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));

    nexus.AdvanceTime(kHandshakeTimeMs + 2 * kSlwPeriodMs);

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);

    DirectPeer *wiViewOfWl = wi.Get<DirectPeerTable>().FindPeer(wl.mRadio.mExtAddress, DirectPeer::kInStateValid);
    DirectPeer *wlViewOfWi = wl.Get<DirectPeerTable>().FindPeer(wi.mRadio.mExtAddress, DirectPeer::kInStateValid);

    VerifyOrQuit(wiViewOfWl != nullptr);
    VerifyOrQuit(wlViewOfWi != nullptr);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: Block WL's radio for a short outage -- long enough for a handful of un-acked");
    Log("        probes, well short of the link loss threshold");

    wl.Get<Mac::Mac>().SetRadioFilterEnabled(true);
    VerifyOrQuit(wl.Get<Mac::Mac>().IsRadioFilterEnabled());

    nexus.AdvanceTime(kOutageMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 5: Verify the link survived the outage: no UNLINKED, and failure counts are bounded");

    VerifyOrQuit(wiEvents.mUnlinkedCount == 0);
    VerifyOrQuit(wlEvents.mUnlinkedCount == 0);
    VerifyOrQuit((wiViewOfWl->GetSupervisionProbeAttempts() > 0) || (wlViewOfWi->GetSupervisionProbeAttempts() > 0));
    VerifyOrQuit(wiViewOfWl->GetSupervisionProbeAttempts() < DirectHandler::kMaxSupervisionFailures);
    VerifyOrQuit(wlViewOfWi->GetSupervisionProbeAttempts() < DirectHandler::kMaxSupervisionFailures);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 6: Clear the outage and verify the link recovers -- probe attempts return to zero");

    wl.Get<Mac::Mac>().SetRadioFilterEnabled(false);
    VerifyOrQuit(!wl.Get<Mac::Mac>().IsRadioFilterEnabled());

    nexus.AdvanceTime(kRecoveryMs);

    VerifyOrQuit(wiEvents.mUnlinkedCount == 0);
    VerifyOrQuit(wlEvents.mUnlinkedCount == 0);
    VerifyOrQuit(wiViewOfWl->GetSupervisionProbeAttempts() == 0);
    VerifyOrQuit(wlViewOfWi->GetSupervisionProbeAttempts() == 0);
}

// Blocks WL's radio long enough to exhaust `kMaxSupervisionFailures` consecutive un-acked
// probes on both sides. Verifies UNLINKED fires, WL resumes wake listening on its own, and
// WI successfully re-links end to end with a fresh `otThreadDirectWakeup()` call -- the
// app-driven reconnection model.
void TestTdSupervisionHardLossAndRecovery(void)
{
    static constexpr uint32_t kSupervisionIntervalMs = 2 * kSlwPeriodMs;
    // Initial idle deadline, plus `kMaxSupervisionFailures` fast retries. Each retry is
    // paced at roughly one SLW window, but when the sender's own radio is functional and
    // separately waits out the peer's next window before transmitting, the two delays can
    // add up to roughly two SLW windows per attempt; size the outage for that worst case
    // plus margin.
    static constexpr uint32_t kOutageMs = kSupervisionIntervalMs + 2 * 8 * kSlwPeriodMs;

    Core nexus;

    Node &wi = nexus.CreateNode();
    Node &wl = nexus.CreateNode();

    TdEventInfo wiEvents;
    TdEventInfo wlEvents;

    memset(&wiEvents, 0, sizeof(wiEvents));
    memset(&wlEvents, 0, sizeof(wlEvents));

    wi.SetName("WI");
    wl.SetName("WL");

    AllowLinkBetween(wi, wl);

    nexus.AdvanceTime(0);

    SuccessOrQuit(Instance::SetGlobalLogLevel(kLogLevelDebg));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 1: Configure both nodes with a shared network key and a 1000 ms Supervision Interval");

    otNetworkKey networkKey;
    memcpy(networkKey.m8, kNetworkKey, sizeof(kNetworkKey));

    SuccessOrQuit(otThreadSetNetworkKey(&wi.GetInstance(), &networkKey));
    SuccessOrQuit(otThreadSetNetworkKey(&wl.GetInstance(), &networkKey));

    SuccessOrQuit(otLinkSetPanId(&wi.GetInstance(), kPanId));
    SuccessOrQuit(otLinkSetPanId(&wl.GetInstance(), kPanId));

    SuccessOrQuit(otThreadDirectSetSlwTimeout(&wi.GetInstance(), kSupervisionIntervalMs));
    SuccessOrQuit(otThreadDirectSetSlwTimeout(&wl.GetInstance(), kSupervisionIntervalMs));

    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wi.GetInstance(), kSlwPeriodSlots));
    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wl.GetInstance(), kSlwPeriodSlots));

    SuccessOrQuit(otIp6SetEnabled(&wi.GetInstance(), true));
    SuccessOrQuit(otIp6SetEnabled(&wl.GetInstance(), true));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 2: Register TD event callbacks and enable WL wake listen");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 3: WI starts wake burst targeting WL and the handshake completes");

    const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

    SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(), &wlAddr, OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));

    nexus.AdvanceTime(kHandshakeTimeMs + 2 * kSlwPeriodMs);

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: Block WL's radio long enough to exhaust the link loss threshold on both sides");

    wl.Get<Mac::Mac>().SetRadioFilterEnabled(true);
    VerifyOrQuit(wl.Get<Mac::Mac>().IsRadioFilterEnabled());

    nexus.AdvanceTime(kOutageMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 5: Verify both sides declared link loss and unlinked");

    VerifyOrQuit(wiEvents.mUnlinkedCount == 1);
    VerifyOrQuit(wlEvents.mUnlinkedCount == 1);
    VerifyOrQuit(wi.Get<DirectPeerTable>().FindPeer(wl.mRadio.mExtAddress, DirectPeer::kInStateValid) == nullptr);
    VerifyOrQuit(wl.Get<DirectPeerTable>().FindPeer(wi.mRadio.mExtAddress, DirectPeer::kInStateValid) == nullptr);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 6: Clear the outage and let WL settle back into wake listening");

    wl.Get<Mac::Mac>().SetRadioFilterEnabled(false);
    VerifyOrQuit(!wl.Get<Mac::Mac>().IsRadioFilterEnabled());

    nexus.AdvanceTime(2 * kSlwPeriodMs);

    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 7: WI re-links with a fresh wake burst -- app-driven reconnection after link loss");

    SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(), &wlAddr, OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));

    nexus.AdvanceTime(kHandshakeTimeMs + 2 * kSlwPeriodMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 8: Verify both sides re-linked successfully");

    VerifyOrQuit(wiEvents.mLinkedCount == 2);
    VerifyOrQuit(wlEvents.mLinkedCount == 2);
}

// Sends a single ordinary application data frame from WI to WL on an established link and
// verifies WL's cached view of WI's SCA state -- specifically the RX timestamp anchor --
// advances from that data frame alone, with no dedicated `otThreadDirectSendScaUpdate` call.
// This is the observable effect of including a fresh SCA LTV in every TD frame, data frames
// included, rather than only in link-management frames.
void TestTdSupervisionScaOnDataFrame(void)
{
    static constexpr uint16_t kUdpPort = 49152;

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
    Log("Step 1: Configure both nodes with a shared network key and SLW period of 800 slots");

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

    SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(),
                                       reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress),
                                       OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));

    nexus.AdvanceTime(kHandshakeTimeMs + 2 * kSlwPeriodMs);

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);

    DirectPeer *wlViewOfWi = wl.Get<DirectPeerTable>().FindPeer(wi.mRadio.mExtAddress, DirectPeer::kInStateValid);
    VerifyOrQuit(wlViewOfWi != nullptr);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: Capture WL's cached SCA RX timestamp for WI right after the handshake");

    uint64_t scaRxTsBeforeData = wlViewOfWi->GetLastScaRxTimestamp();

    // Let the running SLW cycle turn over so a subsequent SCA sample is guaranteed to carry a
    // strictly later RX timestamp than the one captured above.
    nexus.AdvanceTime(kSlwPeriodMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 5: WI sends one ordinary UDP datagram to WL -- no otThreadDirectSendScaUpdate call");

    {
        Ip6::Address  wlLinkLocal = wl.Get<Mle::Mle>().GetLinkLocalAddress();
        otUdpSocket   wiSocket;
        otSockAddr    wiSockAddr;
        otMessageInfo msgInfo;
        otMessage    *msg;
        uint8_t       payload[] = {0xAA, 0xBB, 0xCC, 0xDD};

        memset(&wiSocket, 0, sizeof(wiSocket));
        memset(&wiSockAddr, 0, sizeof(wiSockAddr));

        SuccessOrQuit(otUdpOpen(&wi.GetInstance(), &wiSocket, nullptr, nullptr));
        SuccessOrQuit(otUdpBind(&wi.GetInstance(), &wiSocket, &wiSockAddr, OT_NETIF_THREAD_HOST));

        msg = otUdpNewMessage(&wi.GetInstance(), nullptr);
        VerifyOrQuit(msg != nullptr);
        SuccessOrQuit(otMessageAppend(msg, payload, sizeof(payload)));

        memset(&msgInfo, 0, sizeof(msgInfo));
        AsCoreType(&msgInfo.mPeerAddr) = wlLinkLocal;
        msgInfo.mPeerPort              = kUdpPort;

        SuccessOrQuit(otUdpSend(&wi.GetInstance(), &wiSocket, msg, &msgInfo));

        nexus.AdvanceTime(2 * kSlwPeriodMs);

        SuccessOrQuit(otUdpClose(&wi.GetInstance(), &wiSocket));
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 6: Verify WL's cached SCA RX timestamp for WI advanced from the ordinary data frame");

    uint64_t scaRxTsAfterData = wlViewOfWi->GetLastScaRxTimestamp();

    VerifyOrQuit(wlUdpRx.mRxCount == 1);
    VerifyOrQuit(scaRxTsAfterData > scaRxTsBeforeData);

    SuccessOrQuit(wl.Get<Ip6::Udp>().Close(wlSocket));
}

} // namespace Nexus
} // namespace ot

int main(void)
{
    setenv("OT_NEXUS_PCAP_FILE",
           (getenv("OT_NEXUS_PCAP_FILE_NEGOTIATION") ? getenv("OT_NEXUS_PCAP_FILE_NEGOTIATION") : ""),
           /*overwrite=*/1);
    ot::Nexus::TestTdSupervisionNegotiation();

    setenv("OT_NEXUS_PCAP_FILE",
           (getenv("OT_NEXUS_PCAP_FILE_IDLE_KEEPALIVE") ? getenv("OT_NEXUS_PCAP_FILE_IDLE_KEEPALIVE") : ""),
           /*overwrite=*/1);
    ot::Nexus::TestTdSupervisionIdleKeepalive();

    setenv("OT_NEXUS_PCAP_FILE",
           (getenv("OT_NEXUS_PCAP_FILE_GRACE_PERIOD") ? getenv("OT_NEXUS_PCAP_FILE_GRACE_PERIOD") : ""),
           /*overwrite=*/1);
    ot::Nexus::TestTdSupervisionGracePeriod();

    setenv("OT_NEXUS_PCAP_FILE", (getenv("OT_NEXUS_PCAP_FILE_HARD_LOSS") ? getenv("OT_NEXUS_PCAP_FILE_HARD_LOSS") : ""),
           /*overwrite=*/1);
    ot::Nexus::TestTdSupervisionHardLossAndRecovery();

    setenv("OT_NEXUS_PCAP_FILE", (getenv("OT_NEXUS_PCAP_FILE_SCA_DATA") ? getenv("OT_NEXUS_PCAP_FILE_SCA_DATA") : ""),
           /*overwrite=*/1);
    ot::Nexus::TestTdSupervisionScaOnDataFrame();

    printf("All tests passed\n");
    return 0;
}
