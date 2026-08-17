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
 *   This test drives the real `direct link state` / `direct link peers` CLI
 *   commands (src/cli/cli_td.cpp) through `otCliInputLine()` and asserts on
 *   their literal text output across the wake/link/unlink lifecycle.
 *
 *   Before the ProcessLinkState() fix, this command only ever inspected the
 *   role-specific idle sub-state (wake-burst-active for WI, wake-listener-
 *   enabled for WL) and never checked whether a peer link was actually
 *   established, so it kept printing "state: idle" (WI) / "state: disabled"
 *   (WL) even once the TD link handshake had fully completed.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <openthread/cli.h>
#include <openthread/thread_direct.h>

#include "platform/nexus_core.hpp"
#include "platform/nexus_node.hpp"

namespace ot {
namespace Nexus {

static constexpr uint32_t kHandshakeTimeMs = 5 * 1000;
static constexpr uint32_t kTeardownTimeMs  = 2 * 1000;

static constexpr uint8_t  kNetworkKey[OT_NETWORK_KEY_SIZE] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                                              0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static constexpr uint16_t kPanId                           = 0xD001;

struct TdEventInfo
{
    uint32_t mLinkedCount;
    uint32_t mUnlinkedCount;
};

static void HandleTdEvent(otThreadDirectEvent aEvent, const otThreadDirectPeerInfo *aPeerInfo, void *aContext)
{
    TdEventInfo *info = static_cast<TdEventInfo *>(aContext);

    OT_UNUSED_VARIABLE(aPeerInfo);

    switch (aEvent)
    {
    case OT_THREAD_DIRECT_EVENT_LINKED:
        info->mLinkedCount++;
        break;
    case OT_THREAD_DIRECT_EVENT_UNLINKED:
        info->mUnlinkedCount++;
        break;
    default:
        break;
    }
}

static char   sCliOutput[2048];
static size_t sCliOutputLen;

static int HandleCliOutput(void *aContext, const char *aFormat, va_list aArguments)
{
    int len;

    OT_UNUSED_VARIABLE(aContext);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    len = vsnprintf(&sCliOutput[sCliOutputLen], sizeof(sCliOutput) - sCliOutputLen, aFormat, aArguments);
#pragma GCC diagnostic pop

    if (len > 0)
    {
        sCliOutputLen += static_cast<size_t>(len);

        if (sCliOutputLen >= sizeof(sCliOutput))
        {
            sCliOutputLen = sizeof(sCliOutput) - 1;
        }
    }

    return len;
}

// Runs `aCommand` through the real CLI interpreter against `aNode` and returns
// the accumulated output text (valid until the next `RunCliCommand()` call).
static const char *RunCliCommand(Node &aNode, const char *aCommand)
{
    char commandBuf[128];

    sCliOutput[0] = '\0';
    sCliOutputLen = 0;

    strncpy(commandBuf, aCommand, sizeof(commandBuf) - 1);
    commandBuf[sizeof(commandBuf) - 1] = '\0';

    otCliInit(&aNode.GetInstance(), HandleCliOutput, nullptr);
    otCliInputLine(commandBuf);

    return sCliOutput;
}

void TestTdCliLinkStateReflectsPeerTable(void)
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
    Log("Step 2: Before any wake, `direct link state` must NOT claim \"linked\" on either node");

    // NOTE: this Nexus binary compiles BOTH OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
    // and _LISTENER_ENABLE (required so one process can host both a WI and a WL node and drive a
    // full handshake between them -- see COMMON_COMPILE_OPTIONS in tests/nexus/CMakeLists.txt).
    // ProcessLinkState()'s role banner ("role: wi" vs "role: wl") is chosen at compile time via
    // `#if .._INITIATOR_ENABLE #elif .._LISTENER_ENABLE`, so with both flags on it always resolves
    // to the WI branch, on every node, regardless of which one is actually acting as WI or WL here.
    // Real sleepy-demo-wi/sleepy-demo-wl binaries each compile with exactly one flag, so they don't
    // hit this; what we *can* and do verify below is the actual bug: whether "linked" is reported.
    VerifyOrQuit(strstr(RunCliCommand(wi, "direct link state"), "state: idle") != nullptr);
    VerifyOrQuit(strstr(sCliOutput, "state: linked") == nullptr);

    VerifyOrQuit(strstr(RunCliCommand(wl, "direct link state"), "state: idle") != nullptr);
    VerifyOrQuit(strstr(sCliOutput, "state: linked") == nullptr);

    VerifyOrQuit(strstr(RunCliCommand(wi, "direct link peers"), "no peers") != nullptr);
    VerifyOrQuit(strstr(RunCliCommand(wl, "direct link peers"), "no peers") != nullptr);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 3: Register TD event callbacks and enable WL wake listen");

    otThreadDirectSetEventCallback(&wi.GetInstance(), HandleTdEvent, &wiEvents);
    otThreadDirectSetEventCallback(&wl.GetInstance(), HandleTdEvent, &wlEvents);

    SuccessOrQuit(otThreadDirectWakeListenerEnable(&wl.GetInstance(), true));
    VerifyOrQuit(otThreadDirectIsWakeListenerEnabled(&wl.GetInstance()));

    nexus.AdvanceTime(100);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 4: WI starts wake burst targeting WL");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectWakeup(&wi.GetInstance(), &wlAddr, OT_THREAD_DIRECT_WAKE_TYPE_LINK, 0, 0, 0));
        VerifyOrQuit(otThreadDirectIsWakeBurstActive(&wi.GetInstance()));
    }

    VerifyOrQuit(strstr(RunCliCommand(wi, "direct link state"), "state: waking") != nullptr);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 5: Advance time for handshake to complete");

    nexus.AdvanceTime(kHandshakeTimeMs);

    VerifyOrQuit(wiEvents.mLinkedCount == 1);
    VerifyOrQuit(wlEvents.mLinkedCount == 1);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 6: Once LINKED fires, `direct link state` must print \"state: linked\" on BOTH");
    Log("        nodes' peer table -- before this fix, ProcessLinkState() never checked for an");
    Log("        established peer and kept printing the idle/disabled sub-state forever.");

    VerifyOrQuit(strstr(RunCliCommand(wi, "direct link state"), "state: linked") != nullptr);
    VerifyOrQuit(strstr(RunCliCommand(wl, "direct link state"), "state: linked") != nullptr);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 7: `direct link peers` must list the peer instead of staying silent");

    VerifyOrQuit(strstr(RunCliCommand(wi, "direct link peers"), "addr:") != nullptr);
    VerifyOrQuit(strstr(RunCliCommand(wl, "direct link peers"), "addr:") != nullptr);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 8: WI initiates teardown");

    {
        const otExtAddress &wlAddr = *reinterpret_cast<const otExtAddress *>(&wl.mRadio.mExtAddress);

        SuccessOrQuit(otThreadDirectUnlink(&wi.GetInstance(), &wlAddr));
    }

    nexus.AdvanceTime(kTeardownTimeMs);

    VerifyOrQuit(wiEvents.mUnlinkedCount == 1);
    VerifyOrQuit(wlEvents.mUnlinkedCount == 1);

    Log("---------------------------------------------------------------------------------------");
    Log("Step 9: After UNLINKED, both sides must fall back to reporting no linked peer");

    VerifyOrQuit(strstr(RunCliCommand(wi, "direct link state"), "state: idle") != nullptr);
    VerifyOrQuit(strstr(sCliOutput, "state: linked") == nullptr);

    VerifyOrQuit(strstr(RunCliCommand(wl, "direct link state"), "state: idle") != nullptr);
    VerifyOrQuit(strstr(sCliOutput, "state: linked") == nullptr);

    VerifyOrQuit(strstr(RunCliCommand(wi, "direct link peers"), "no peers") != nullptr);
    VerifyOrQuit(strstr(RunCliCommand(wl, "direct link peers"), "no peers") != nullptr);
}

} // namespace Nexus
} // namespace ot

int main(void)
{
    ot::Nexus::TestTdCliLinkStateReflectsPeerTable();

    printf("All tests passed\n");
    return 0;
}
