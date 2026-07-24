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
static constexpr uint32_t kTeardownTimeMs  = 2 * 1000;

static constexpr uint8_t  kNetworkKey[OT_NETWORK_KEY_SIZE] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                                              0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static constexpr uint8_t  kGuestKey[OT_NETWORK_KEY_SIZE]   = {0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x07, 0x18,
                                                              0x29, 0x3a, 0x4b, 0x5c, 0x6d, 0x7e, 0x8f, 0x90};
static constexpr uint8_t  kGuestKeyIndex                   = 130;
static constexpr uint16_t kPanId                           = 0xD001;

struct TdEventInfo
{
    uint32_t            mLinkedCount;
    uint32_t            mLinkFailedCount;
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
    case OT_THREAD_DIRECT_EVENT_LINK_FAILED:
        info->mLinkFailedCount++;
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

static void StartWakeBurst(Node &aWi, Node &aWl)
{
    const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&aWl.mRadio.mExtAddress);

    SuccessOrQuit(otThreadDirectWakeup(&aWi.GetInstance(), &wlAddr, OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));
    VerifyOrQuit(otThreadDirectIsWakeBurstActive(&aWi.GetInstance()));
}

void TestTdLinkHandshake(void)
{
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
    Log("Step 1: Configure both nodes with a shared network key (detached, no Thread network)");

    otNetworkKey networkKey;
    memcpy(networkKey.m8, kNetworkKey, sizeof(kNetworkKey));

    SuccessOrQuit(otThreadSetNetworkKey(&wi.GetInstance(), &networkKey));
    SuccessOrQuit(otThreadSetNetworkKey(&wl.GetInstance(), &networkKey));

    SuccessOrQuit(otLinkSetPanId(&wi.GetInstance(), kPanId));
    SuccessOrQuit(otLinkSetPanId(&wl.GetInstance(), kPanId));

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

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(), &wlAddr, OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));
        VerifyOrQuit(otThreadDirectIsWakeBurstActive(&wi.GetInstance()));
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: Advance time for handshake to complete");

    nexus.AdvanceTime(kHandshakeTimeMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 5: Verify both sides received LINKED event");

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mWakeReceivedCount >= 1);

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);
        const otExtAddress &wiAddr = *reinterpret_cast<const otExtAddress *>(&wi.mRadio.mExtAddress);

        VerifyOrQuit(memcmp(wiEvents.mLastPeerAddr.m8, wlAddr.m8, OT_EXT_ADDRESS_SIZE) == 0);
        VerifyOrQuit(memcmp(wlEvents.mLastPeerAddr.m8, wiAddr.m8, OT_EXT_ADDRESS_SIZE) == 0);
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 6: WI initiates teardown");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectUnlink(&wi.GetInstance(), &wlAddr));
    }

    nexus.AdvanceTime(kTeardownTimeMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 7: Verify both sides received UNLINKED event");

    VerifyOrQuit(wiEvents.mUnlinkedCount == 1);
    VerifyOrQuit(wlEvents.mUnlinkedCount == 1);
}

void TestTdLinkHandshakeGuestKey(void)
{
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
    Log("Step 1: Configure both nodes with a shared network key and guest wake key at index 130");

    otNetworkKey networkKey;
    memcpy(networkKey.m8, kNetworkKey, sizeof(kNetworkKey));

    SuccessOrQuit(otThreadSetNetworkKey(&wi.GetInstance(), &networkKey));
    SuccessOrQuit(otThreadSetNetworkKey(&wl.GetInstance(), &networkKey));

    SuccessOrQuit(otLinkSetPanId(&wi.GetInstance(), kPanId));
    SuccessOrQuit(otLinkSetPanId(&wl.GetInstance(), kPanId));

    {
        otThreadDirectWakeKey guestKey;
        memcpy(guestKey.m8, kGuestKey, sizeof(guestKey.m8));

        SuccessOrQuit(otThreadDirectSetGuestWakeKey(&wi.GetInstance(), kGuestKeyIndex, &guestKey));
        SuccessOrQuit(otThreadDirectSetGuestWakeKey(&wl.GetInstance(), kGuestKeyIndex, &guestKey));
    }

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
    Log("Step 3: WI starts wake burst using guest key index 130");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(
            otThreadDirectWakeup(&wi.GetInstance(), &wlAddr, OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, kGuestKeyIndex));
        VerifyOrQuit(otThreadDirectIsWakeBurstActive(&wi.GetInstance()));
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: Advance time for handshake to complete");

    nexus.AdvanceTime(kHandshakeTimeMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 5: Verify both sides received LINKED event");

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mWakeReceivedCount >= 1);

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);
        const otExtAddress &wiAddr = *reinterpret_cast<const otExtAddress *>(&wi.mRadio.mExtAddress);

        VerifyOrQuit(memcmp(wiEvents.mLastPeerAddr.m8, wlAddr.m8, OT_EXT_ADDRESS_SIZE) == 0);
        VerifyOrQuit(memcmp(wlEvents.mLastPeerAddr.m8, wiAddr.m8, OT_EXT_ADDRESS_SIZE) == 0);
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 6: WI initiates teardown");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectUnlink(&wi.GetInstance(), &wlAddr));
    }

    nexus.AdvanceTime(kTeardownTimeMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 7: Verify both sides received UNLINKED event");

    VerifyOrQuit(wiEvents.mUnlinkedCount == 1);
    VerifyOrQuit(wlEvents.mUnlinkedCount == 1);
}

void TestTdLinkHandshakeLinksOnlyAfterWiFollowUp(void)
{
    static constexpr uint16_t kLongSlwPeriodSlots = 2000;
    static constexpr uint32_t kPreFollowUpCheckMs = 200;
    static constexpr uint32_t kPollStepMs         = 100;
    static constexpr uint32_t kHandshakeLimitMs   = 10 * 1000;

    Core nexus;

    Node &wi = nexus.CreateNode();
    Node &wl = nexus.CreateNode();

    TdEventInfo            wiEvents;
    TdEventInfo            wlEvents;
    otThreadDirectPeerInfo peerInfo;
    const otExtAddress    &wiAddr = *reinterpret_cast<const otExtAddress *>(&wi.mRadio.mExtAddress);

    memset(&wiEvents, 0, sizeof(wiEvents));
    memset(&wlEvents, 0, sizeof(wlEvents));

    wi.SetName("WI-follow-up");
    wl.SetName("WL-follow-up");

    AllowLinkBetween(wi, wl);

    nexus.AdvanceTime(0);

    Log("---------------------------------------------------------------------------------------");
    Log("FollowUp Step 1: Configure both nodes with a shared network key and a long SLW period");

    otNetworkKey networkKey;
    memcpy(networkKey.m8, kNetworkKey, sizeof(kNetworkKey));

    SuccessOrQuit(otThreadSetNetworkKey(&wi.GetInstance(), &networkKey));
    SuccessOrQuit(otThreadSetNetworkKey(&wl.GetInstance(), &networkKey));

    SuccessOrQuit(otLinkSetPanId(&wi.GetInstance(), kPanId));
    SuccessOrQuit(otLinkSetPanId(&wl.GetInstance(), kPanId));

    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wi.GetInstance(), kLongSlwPeriodSlots));
    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wl.GetInstance(), kLongSlwPeriodSlots));

    SuccessOrQuit(otIp6SetEnabled(&wi.GetInstance(), true));
    SuccessOrQuit(otIp6SetEnabled(&wl.GetInstance(), true));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("FollowUp Step 2: Register TD event callbacks, enable WL wake listen, and start wake burst");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    nexus.AdvanceTime(100);
    StartWakeBurst(wi, wl);

    Log("---------------------------------------------------------------------------------------");
    Log("FollowUp Step 3: Verify WL is not linked before WI's scheduled follow-up TD Link Command");

    nexus.AdvanceTime(kPreFollowUpCheckMs);

    VerifyOrQuit(wlEvents.mWakeReceivedCount == 1);
    VerifyOrQuit(wiEvents.mLinkedCount == 0);
    VerifyOrQuit(wlEvents.mLinkedCount == 0);
    VerifyOrQuit(otThreadDirectGetPeerInfo(&wl.GetInstance(), &wiAddr, &peerInfo) == OT_ERROR_NOT_FOUND);

    Log("---------------------------------------------------------------------------------------");
    Log("FollowUp Step 4: Advance until both sides report LINKED");

    for (uint32_t waited = 0; waited < kHandshakeLimitMs; waited += kPollStepMs)
    {
        nexus.AdvanceTime(kPollStepMs);

        if ((wiEvents.mLinkedCount == 1) && (wlEvents.mLinkedCount == 1))
        {
            break;
        }
    }

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);
    SuccessOrQuit(otThreadDirectGetPeerInfo(&wl.GetInstance(), &wiAddr, &peerInfo));
}

void TestTdLinkHandshakeWlTimeoutResumesListening(void)
{
    static constexpr uint16_t kLongSlwPeriodSlots = 2000;
    static constexpr uint32_t kPreFollowUpCheckMs = 200;
    static constexpr uint32_t kTimeoutWaitMs      = 8 * 1000;

    Core nexus;

    Node &wi = nexus.CreateNode();
    Node &wl = nexus.CreateNode();

    TdEventInfo            wiEvents;
    TdEventInfo            wlEvents;
    otThreadDirectPeerInfo peerInfo;
    const otExtAddress    &wiAddr = *reinterpret_cast<const otExtAddress *>(&wi.mRadio.mExtAddress);

    memset(&wiEvents, 0, sizeof(wiEvents));
    memset(&wlEvents, 0, sizeof(wlEvents));

    wi.SetName("WI-timeout");
    wl.SetName("WL-timeout");

    AllowLinkBetween(wi, wl);

    nexus.AdvanceTime(0);

    Log("---------------------------------------------------------------------------------------");
    Log("Timeout Step 1: Configure both nodes with a shared network key and a long SLW period");

    otNetworkKey networkKey;
    memcpy(networkKey.m8, kNetworkKey, sizeof(kNetworkKey));

    SuccessOrQuit(otThreadSetNetworkKey(&wi.GetInstance(), &networkKey));
    SuccessOrQuit(otThreadSetNetworkKey(&wl.GetInstance(), &networkKey));

    SuccessOrQuit(otLinkSetPanId(&wi.GetInstance(), kPanId));
    SuccessOrQuit(otLinkSetPanId(&wl.GetInstance(), kPanId));

    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wi.GetInstance(), kLongSlwPeriodSlots));
    SuccessOrQuit(otThreadDirectSetSlwSchedule(&wl.GetInstance(), kLongSlwPeriodSlots));

    SuccessOrQuit(otIp6SetEnabled(&wi.GetInstance(), true));
    SuccessOrQuit(otIp6SetEnabled(&wl.GetInstance(), true));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Timeout Step 2: Register TD event callbacks, enable WL wake listen, and start wake burst");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    nexus.AdvanceTime(100);
    StartWakeBurst(wi, wl);

    Log("---------------------------------------------------------------------------------------");
    Log("Timeout Step 3: Enable WL radio filter after wake reception to drop WI's follow-up TD Link Command");

    nexus.AdvanceTime(kPreFollowUpCheckMs);
    VerifyOrQuit(wlEvents.mWakeReceivedCount == 1);

    wl.Get<Mac::Mac>().SetRadioFilterEnabled(true);
    VerifyOrQuit(wl.Get<Mac::Mac>().IsRadioFilterEnabled());

    Log("---------------------------------------------------------------------------------------");
    Log("Timeout Step 4: Advance through WI link failure and WL timeout recovery");

    nexus.AdvanceTime(kTimeoutWaitMs);

    VerifyOrQuit(wiEvents.mLinkedCount == 0);
    VerifyOrQuit(wlEvents.mLinkedCount == 0);
    VerifyOrQuit(wiEvents.mLinkFailedCount == 1);
    VerifyOrQuit(wlEvents.mLinkFailedCount == 0);
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));
    VerifyOrQuit(otThreadDirectGetPeerInfo(&wl.GetInstance(), &wiAddr, &peerInfo) == OT_ERROR_NOT_FOUND);

    wl.Get<Mac::Mac>().SetRadioFilterEnabled(false);
}

struct UdpRxInfo
{
    uint32_t mRxCount;
    uint16_t mLastFullLen; // Untruncated payload length, so large (multi-frame) payloads can still be size-checked.
    uint8_t  mLastPayload[32];
    uint8_t  mLastPayloadLen;
};

static void HandleUdpReceive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    OT_UNUSED_VARIABLE(aMessageInfo);

    UdpRxInfo *info = static_cast<UdpRxInfo *>(aContext);
    uint16_t   len  = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);

    info->mRxCount++;
    info->mLastFullLen = len;

    if (len > sizeof(info->mLastPayload))
    {
        len = sizeof(info->mLastPayload);
    }

    info->mLastPayloadLen = static_cast<uint8_t>(len);
    otMessageRead(aMessage, otMessageGetOffset(aMessage), info->mLastPayload, len);
}

static void FillPayloadPattern(uint8_t *aBuffer, uint16_t aLength, uint8_t aSeed)
{
    for (uint16_t i = 0; i < aLength; i++)
    {
        aBuffer[i] = static_cast<uint8_t>(aSeed + i);
    }
}

static bool VerifyPayloadPatternPrefix(const uint8_t *aBuffer, uint8_t aLength, uint8_t aSeed)
{
    bool matches = true;

    for (uint8_t i = 0; i < aLength; i++)
    {
        if (aBuffer[i] != static_cast<uint8_t>(aSeed + i))
        {
            matches = false;
            break;
        }
    }

    return matches;
}

void TestTdLinkHandshakeWithSca(void)
{
    // SLW period: 800 slots x 625 us/slot = 500 ms.
    static constexpr uint16_t kSlwPeriodSlots = 800;
    static constexpr uint32_t kSlwPeriodMs    = 500;
    static constexpr uint16_t kUdpPort        = 49152;
    static constexpr uint16_t kUdpPortWi      = 49153;

    static constexpr uint8_t kUdpPayloadPre[]   = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0x00, 0x01};
    static constexpr uint8_t kUdpPayloadPost[]  = {0x5C, 0xA0, 0x5C, 0xA0, 0x5C, 0xA0, 0x00, 0x02};
    static constexpr uint8_t kUdpPayloadReply[] = {0x8E, 0x8E, 0x8E, 0x8E, 0x8E, 0x8E, 0x00, 0x03};

    Core nexus;

    Node &wi = nexus.CreateNode();
    Node &wl = nexus.CreateNode();

    TdEventInfo wiEvents;
    TdEventInfo wlEvents;
    UdpRxInfo   wlUdpRx;
    UdpRxInfo   wiUdpRx;

    memset(&wiEvents, 0, sizeof(wiEvents));
    memset(&wlEvents, 0, sizeof(wlEvents));
    memset(&wlUdpRx, 0, sizeof(wlUdpRx));
    memset(&wiUdpRx, 0, sizeof(wiUdpRx));

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
    Log("Step 2: Register TD event callbacks and enable WL wake listen");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 3: WI and WL each open a UDP socket to receive data from the other");

    Ip6::Udp::SocketHandle wlSocket;
    Ip6::SockAddr          wlSockAddr;

    wlSocket.Clear();
    wlSockAddr.Clear();
    wlSockAddr.SetPort(kUdpPort);

    SuccessOrQuit(wl.Get<Ip6::Udp>().Open(wlSocket, Ip6::kNetifThreadInternal, HandleUdpReceive, &wlUdpRx));
    SuccessOrQuit(wl.Get<Ip6::Udp>().Bind(wlSocket, wlSockAddr));

    Ip6::Udp::SocketHandle wiRxSocket;
    Ip6::SockAddr          wiRxSockAddr;

    wiRxSocket.Clear();
    wiRxSockAddr.Clear();
    wiRxSockAddr.SetPort(kUdpPortWi);

    SuccessOrQuit(wi.Get<Ip6::Udp>().Open(wiRxSocket, Ip6::kNetifThreadInternal, HandleUdpReceive, &wiUdpRx));
    SuccessOrQuit(wi.Get<Ip6::Udp>().Bind(wiRxSocket, wiRxSockAddr));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: WI starts wake burst targeting WL");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(), &wlAddr, OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));
        VerifyOrQuit(otThreadDirectIsWakeBurstActive(&wi.GetInstance()));
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 5: Advance time -- handshake now includes WI's own TD Link Command");
    Log("        Expected trace: Wake(s) | WL->WI: TD Link Cmd(Chal+SCA) | WI->WL: Enh-ACK(Chal)");
    Log("                      | WI->WL: TD Link Cmd(SCA) [at WL's SLW window] | WL->WI: Enh-ACK");

    // Handshake timeout + 2x SLW period to allow WI's TD Link Cmd to be scheduled and sent.
    nexus.AdvanceTime(kHandshakeTimeMs + 2 * kSlwPeriodMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 6: Verify both sides received LINKED event and have peer SCA params");

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mWakeReceivedCount >= 1);

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);
        const otExtAddress &wiAddr = *reinterpret_cast<const otExtAddress *>(&wi.mRadio.mExtAddress);

        VerifyOrQuit(memcmp(wiEvents.mLastPeerAddr.m8, wlAddr.m8, OT_EXT_ADDRESS_SIZE) == 0);
        VerifyOrQuit(memcmp(wlEvents.mLastPeerAddr.m8, wiAddr.m8, OT_EXT_ADDRESS_SIZE) == 0);
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 7: WI sends first UDP to WL (pre-SCA-update, phase=0 schedule)");

    {
        Ip6::Address  wlLinkLocal = wl.Get<Mle::Mle>().GetLinkLocalAddress();
        otUdpSocket   wiSocket;
        otSockAddr    wiSockAddr;
        otMessageInfo msgInfo;
        otMessage    *msg;

        memset(&wiSocket, 0, sizeof(wiSocket));
        memset(&wiSockAddr, 0, sizeof(wiSockAddr));
        wiSockAddr.mPort = kUdpPort;

        SuccessOrQuit(otUdpOpen(&wi.GetInstance(), &wiSocket, nullptr, nullptr));
        SuccessOrQuit(otUdpBind(&wi.GetInstance(), &wiSocket, &wiSockAddr, OT_NETIF_THREAD_HOST));

        msg = otUdpNewMessage(&wi.GetInstance(), nullptr);
        VerifyOrQuit(msg != nullptr);
        SuccessOrQuit(otMessageAppend(msg, kUdpPayloadPre, sizeof(kUdpPayloadPre)));

        memset(&msgInfo, 0, sizeof(msgInfo));
        AsCoreType(&msgInfo.mPeerAddr) = wlLinkLocal;
        msgInfo.mPeerPort              = kUdpPort;

        SuccessOrQuit(otUdpSend(&wi.GetInstance(), &wiSocket, msg, &msgInfo));

        nexus.AdvanceTime(2 * kSlwPeriodMs);

        SuccessOrQuit(otUdpClose(&wi.GetInstance(), &wiSocket));
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 8: Verify WL received the first UDP (pre-update)");

    VerifyOrQuit(wlUdpRx.mRxCount == 1);
    VerifyOrQuit(wlUdpRx.mLastPayloadLen == sizeof(kUdpPayloadPre));
    VerifyOrQuit(memcmp(wlUdpRx.mLastPayload, kUdpPayloadPre, sizeof(kUdpPayloadPre)) == 0);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 9: WL sends post-link SCA update to WI (SLW running -- phase is non-zero)");
    Log("        Expected trace: WL->WI: TD Link Cmd(SCA, phase != 0) [at WI's SLW window]");

    {
        const otExtAddress &wiAddr = *reinterpret_cast<const otExtAddress *>(&wi.mRadio.mExtAddress);

        // Advance one SLW period so the running SLW cycle has turned over and
        // GetLocalSca() returns a non-zero phase for the outgoing TD Link Command.
        nexus.AdvanceTime(kSlwPeriodMs);

        SuccessOrQuit(otThreadDirectSendScaUpdate(&wl.GetInstance(), &wiAddr));

        nexus.AdvanceTime(kSlwPeriodMs);
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 10: WI sends second UDP to WL (post-SCA-update, using corrected phase schedule)");

    {
        Ip6::Address  wlLinkLocal = wl.Get<Mle::Mle>().GetLinkLocalAddress();
        otUdpSocket   wiSocket;
        otSockAddr    wiSockAddr;
        otMessageInfo msgInfo;
        otMessage    *msg;

        memset(&wiSocket, 0, sizeof(wiSocket));
        memset(&wiSockAddr, 0, sizeof(wiSockAddr));
        wiSockAddr.mPort = kUdpPort;

        SuccessOrQuit(otUdpOpen(&wi.GetInstance(), &wiSocket, nullptr, nullptr));
        SuccessOrQuit(otUdpBind(&wi.GetInstance(), &wiSocket, &wiSockAddr, OT_NETIF_THREAD_HOST));

        msg = otUdpNewMessage(&wi.GetInstance(), nullptr);
        VerifyOrQuit(msg != nullptr);
        SuccessOrQuit(otMessageAppend(msg, kUdpPayloadPost, sizeof(kUdpPayloadPost)));

        memset(&msgInfo, 0, sizeof(msgInfo));
        AsCoreType(&msgInfo.mPeerAddr) = wlLinkLocal;
        msgInfo.mPeerPort              = kUdpPort;

        SuccessOrQuit(otUdpSend(&wi.GetInstance(), &wiSocket, msg, &msgInfo));

        nexus.AdvanceTime(2 * kSlwPeriodMs);

        SuccessOrQuit(otUdpClose(&wi.GetInstance(), &wiSocket));
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 11: Verify WL received the second UDP (post-update)");

    VerifyOrQuit(wlUdpRx.mRxCount == 2);
    VerifyOrQuit(wlUdpRx.mLastPayloadLen == sizeof(kUdpPayloadPost));
    VerifyOrQuit(memcmp(wlUdpRx.mLastPayload, kUdpPayloadPost, sizeof(kUdpPayloadPost)) == 0);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 12: WL sends a UDP reply to WI (WL->WI direction, exercising WL's own");
    Log("         ThreadDirectTxScheduler and WI's receive-window scheduling as the receiver)");

    {
        Ip6::Address  wiLinkLocal = wi.Get<Mle::Mle>().GetLinkLocalAddress();
        otUdpSocket   wlSendSocket;
        otSockAddr    wlSendSockAddr;
        otMessageInfo msgInfo;
        otMessage    *msg;

        memset(&wlSendSocket, 0, sizeof(wlSendSocket));
        memset(&wlSendSockAddr, 0, sizeof(wlSendSockAddr));
        wlSendSockAddr.mPort = kUdpPortWi;

        SuccessOrQuit(otUdpOpen(&wl.GetInstance(), &wlSendSocket, nullptr, nullptr));
        SuccessOrQuit(otUdpBind(&wl.GetInstance(), &wlSendSocket, &wlSendSockAddr, OT_NETIF_THREAD_HOST));

        msg = otUdpNewMessage(&wl.GetInstance(), nullptr);
        VerifyOrQuit(msg != nullptr);
        SuccessOrQuit(otMessageAppend(msg, kUdpPayloadReply, sizeof(kUdpPayloadReply)));

        memset(&msgInfo, 0, sizeof(msgInfo));
        AsCoreType(&msgInfo.mPeerAddr) = wiLinkLocal;
        msgInfo.mPeerPort              = kUdpPortWi;

        SuccessOrQuit(otUdpSend(&wl.GetInstance(), &wlSendSocket, msg, &msgInfo));

        nexus.AdvanceTime(2 * kSlwPeriodMs);

        SuccessOrQuit(otUdpClose(&wl.GetInstance(), &wlSendSocket));
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 13: Verify WI received the UDP reply from WL");

    VerifyOrQuit(wiUdpRx.mRxCount == 1);
    VerifyOrQuit(wiUdpRx.mLastPayloadLen == sizeof(kUdpPayloadReply));
    VerifyOrQuit(memcmp(wiUdpRx.mLastPayload, kUdpPayloadReply, sizeof(kUdpPayloadReply)) == 0);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 14: WI initiates teardown");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectUnlink(&wi.GetInstance(), &wlAddr));
    }

    nexus.AdvanceTime(kTeardownTimeMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 15: Verify both sides received UNLINKED event");

    VerifyOrQuit(wiEvents.mUnlinkedCount == 1);
    VerifyOrQuit(wlEvents.mUnlinkedCount == 1);

    SuccessOrQuit(wl.Get<Ip6::Udp>().Close(wlSocket));
    SuccessOrQuit(wi.Get<Ip6::Udp>().Close(wiRxSocket));
}

void TestTdLinkHandshakeShortPeriodLargeFrame(void)
{
    // 5 ms SLW period (8 slots x 625 us) on purpose: it's short enough that
    // CalculatePeriodicSampleWindowEdges, which sizes the receive window off the
    // period, ends up giving it less room than a full-size frame actually needs.
    static constexpr uint16_t kSlwPeriodSlots = 8;
    static constexpr uint32_t kSlwPeriodMs    = 5;
    static constexpr uint16_t kUdpPort        = 49152;
    static constexpr uint16_t kUdpPortWi      = 49153;

    // Larger than a single 802.15.4 frame's usable payload after MAC/6LoWPAN/IPHC/UDP-NHC
    // headers (~127 byte PSDU budget), forcing 6LoWPAN fragmentation across multiple frames,
    // each independently going through TD TX scheduling.
    static constexpr uint16_t kLargePayloadLen = 150;

    Core nexus;

    Node &wi = nexus.CreateNode();
    Node &wl = nexus.CreateNode();

    TdEventInfo wiEvents;
    TdEventInfo wlEvents;
    UdpRxInfo   wlUdpRx;
    UdpRxInfo   wiUdpRx;

    memset(&wiEvents, 0, sizeof(wiEvents));
    memset(&wlEvents, 0, sizeof(wlEvents));
    memset(&wlUdpRx, 0, sizeof(wlUdpRx));
    memset(&wiUdpRx, 0, sizeof(wiUdpRx));

    wi.SetName("WI");
    wl.SetName("WL");

    AllowLinkBetween(wi, wl);

    nexus.AdvanceTime(0);

    SuccessOrQuit(Instance::SetGlobalLogLevel(kLogLevelDebg));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 1: Configure both nodes with a shared network key and a short SLW period (5 ms)");

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
    Log("Step 2: Register TD event callbacks and enable WL wake listen");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 3: WI and WL each open a UDP socket to receive data from the other");

    Ip6::Udp::SocketHandle wlSocket;
    Ip6::SockAddr          wlSockAddr;

    wlSocket.Clear();
    wlSockAddr.Clear();
    wlSockAddr.SetPort(kUdpPort);

    SuccessOrQuit(wl.Get<Ip6::Udp>().Open(wlSocket, Ip6::kNetifThreadInternal, HandleUdpReceive, &wlUdpRx));
    SuccessOrQuit(wl.Get<Ip6::Udp>().Bind(wlSocket, wlSockAddr));

    Ip6::Udp::SocketHandle wiRxSocket;
    Ip6::SockAddr          wiRxSockAddr;

    wiRxSocket.Clear();
    wiRxSockAddr.Clear();
    wiRxSockAddr.SetPort(kUdpPortWi);

    SuccessOrQuit(wi.Get<Ip6::Udp>().Open(wiRxSocket, Ip6::kNetifThreadInternal, HandleUdpReceive, &wiUdpRx));
    SuccessOrQuit(wi.Get<Ip6::Udp>().Bind(wiRxSocket, wiRxSockAddr));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: WI starts wake burst targeting WL");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(), &wlAddr, OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));
        VerifyOrQuit(otThreadDirectIsWakeBurstActive(&wi.GetInstance()));
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 5: Advance time for handshake to complete");

    nexus.AdvanceTime(kHandshakeTimeMs + 2 * kSlwPeriodMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 6: Verify both sides received LINKED event");

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mWakeReceivedCount >= 1);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 7: WI sends a large (fragmented) UDP payload to WL");

    {
        Ip6::Address  wlLinkLocal = wl.Get<Mle::Mle>().GetLinkLocalAddress();
        otUdpSocket   wiSocket;
        otSockAddr    wiSockAddr;
        otMessageInfo msgInfo;
        otMessage    *msg;
        uint8_t       payload[kLargePayloadLen];

        FillPayloadPattern(payload, sizeof(payload), /* aSeed */ 0x10);

        memset(&wiSocket, 0, sizeof(wiSocket));
        memset(&wiSockAddr, 0, sizeof(wiSockAddr));
        wiSockAddr.mPort = kUdpPort;

        SuccessOrQuit(otUdpOpen(&wi.GetInstance(), &wiSocket, nullptr, nullptr));
        SuccessOrQuit(otUdpBind(&wi.GetInstance(), &wiSocket, &wiSockAddr, OT_NETIF_THREAD_HOST));

        msg = otUdpNewMessage(&wi.GetInstance(), nullptr);
        VerifyOrQuit(msg != nullptr);
        SuccessOrQuit(otMessageAppend(msg, payload, sizeof(payload)));

        memset(&msgInfo, 0, sizeof(msgInfo));
        AsCoreType(&msgInfo.mPeerAddr) = wlLinkLocal;
        msgInfo.mPeerPort              = kUdpPort;

        SuccessOrQuit(otUdpSend(&wi.GetInstance(), &wiSocket, msg, &msgInfo));

        nexus.AdvanceTime(20 * kSlwPeriodMs);

        SuccessOrQuit(otUdpClose(&wi.GetInstance(), &wiSocket));
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 8: Verify WL received the large payload intact");

    VerifyOrQuit(wlUdpRx.mRxCount == 1);
    VerifyOrQuit(wlUdpRx.mLastFullLen == kLargePayloadLen);
    VerifyOrQuit(VerifyPayloadPatternPrefix(wlUdpRx.mLastPayload, wlUdpRx.mLastPayloadLen, 0x10));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 9: WL sends a large (fragmented) UDP payload back to WI");

    {
        Ip6::Address  wiLinkLocal = wi.Get<Mle::Mle>().GetLinkLocalAddress();
        otUdpSocket   wlSendSocket;
        otSockAddr    wlSendSockAddr;
        otMessageInfo msgInfo;
        otMessage    *msg;
        uint8_t       payload[kLargePayloadLen];

        FillPayloadPattern(payload, sizeof(payload), /* aSeed */ 0x80);

        memset(&wlSendSocket, 0, sizeof(wlSendSocket));
        memset(&wlSendSockAddr, 0, sizeof(wlSendSockAddr));
        wlSendSockAddr.mPort = kUdpPortWi;

        SuccessOrQuit(otUdpOpen(&wl.GetInstance(), &wlSendSocket, nullptr, nullptr));
        SuccessOrQuit(otUdpBind(&wl.GetInstance(), &wlSendSocket, &wlSendSockAddr, OT_NETIF_THREAD_HOST));

        msg = otUdpNewMessage(&wl.GetInstance(), nullptr);
        VerifyOrQuit(msg != nullptr);
        SuccessOrQuit(otMessageAppend(msg, payload, sizeof(payload)));

        memset(&msgInfo, 0, sizeof(msgInfo));
        AsCoreType(&msgInfo.mPeerAddr) = wiLinkLocal;
        msgInfo.mPeerPort              = kUdpPortWi;

        SuccessOrQuit(otUdpSend(&wl.GetInstance(), &wlSendSocket, msg, &msgInfo));

        nexus.AdvanceTime(20 * kSlwPeriodMs);

        SuccessOrQuit(otUdpClose(&wl.GetInstance(), &wlSendSocket));
    }

    Log("---------------------------------------------------------------------------------------");
    Log("Step 10: Verify WI received the large payload intact");

    VerifyOrQuit(wiUdpRx.mRxCount == 1);
    VerifyOrQuit(wiUdpRx.mLastFullLen == kLargePayloadLen);
    VerifyOrQuit(VerifyPayloadPatternPrefix(wiUdpRx.mLastPayload, wiUdpRx.mLastPayloadLen, 0x80));

    Log("---------------------------------------------------------------------------------------");
    Log("Step 11: WI initiates teardown");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectUnlink(&wi.GetInstance(), &wlAddr));
    }

    nexus.AdvanceTime(kTeardownTimeMs);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 12: Verify both sides received UNLINKED event");

    VerifyOrQuit(wiEvents.mUnlinkedCount == 1);
    VerifyOrQuit(wlEvents.mUnlinkedCount == 1);

    SuccessOrQuit(wl.Get<Ip6::Udp>().Close(wlSocket));
    SuccessOrQuit(wi.Get<Ip6::Udp>().Close(wiRxSocket));
}

} // namespace Nexus
} // namespace ot

int main(void)
{
    setenv("OT_NEXUS_PCAP_FILE", (getenv("OT_NEXUS_PCAP_FILE_DEFAULT") ? getenv("OT_NEXUS_PCAP_FILE_DEFAULT") : ""),
           /*overwrite=*/1);
    ot::Nexus::TestTdLinkHandshake();
    ot::Nexus::TestTdLinkHandshakeLinksOnlyAfterWiFollowUp();
    ot::Nexus::TestTdLinkHandshakeWlTimeoutResumesListening();

    setenv("OT_NEXUS_PCAP_FILE", (getenv("OT_NEXUS_PCAP_FILE_GUEST") ? getenv("OT_NEXUS_PCAP_FILE_GUEST") : ""),
           /*overwrite=*/1);
    ot::Nexus::TestTdLinkHandshakeGuestKey();

    setenv("OT_NEXUS_PCAP_FILE", (getenv("OT_NEXUS_PCAP_FILE_SCA") ? getenv("OT_NEXUS_PCAP_FILE_SCA") : ""),
           /*overwrite=*/1);
    ot::Nexus::TestTdLinkHandshakeWithSca();

    setenv("OT_NEXUS_PCAP_FILE",
           (getenv("OT_NEXUS_PCAP_FILE_SHORT_PERIOD") ? getenv("OT_NEXUS_PCAP_FILE_SHORT_PERIOD") : ""),
           /*overwrite=*/1);
    ot::Nexus::TestTdLinkHandshakeShortPeriodLargeFrame();

    printf("All tests passed\n");
    return 0;
}
