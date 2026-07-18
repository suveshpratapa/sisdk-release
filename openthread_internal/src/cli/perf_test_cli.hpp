/*******************************************************************************
 * @file
 * @brief Performance Testing CLI
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: LicenseRef-MSLA
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of the Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement
 * By installing, copying or otherwise using this software, you agree to the
 * terms of the MSLA.
 *
 ******************************************************************************/

#ifndef PERF_TEST_CLI_HPP_
#define PERF_TEST_CLI_HPP_

#include "openthread-core-config.h"

#include "cli/cli_utils.hpp"

#include "openthread/coap.h"
#include "openthread/icmp6.h"
#include "openthread/udp.h"

#include "common/timer.hpp"

namespace ot {
namespace Cli {

extern "C" otError PerfTestCommand(void *context, uint8_t argc, char *argv[]);
extern "C" void    sl_ot_perf_test_cli_init(void);

/**
 * This class implements the performance testing CLI.
 *
 */
class PerfTest : private Utils
{
    friend otError PerfTestCommand(void *context, uint8_t argc, char *argv[]);
    friend void    sl_ot_perf_test_cli_init(void);

public:
    typedef Utils::Arg Arg;

    explicit PerfTest(Instance *aInstance, OutputImplementer &aOutputImplementer);

    static PerfTest &GetPerfTest(void) { return *sPerfTest; }

    otError Process(Arg aArgs[]);

    struct TestMessageContext
    {
        PerfTest *mPerfTest;
        uint16_t  mSequence;
        uint32_t  mStartTime;
    };

private:
    static PerfTest *sPerfTest;

    enum
    {
        kMaxArgs                    = 32,
        kMaxBufferSize              = 16,
        kPerfTestId                 = 0xB0FF,
        kPerfTestMaxInFlight        = 4,
        kPerfTestMaxPayloadLength   = 500,
        kDefaultPerfCoapTimeoutMs   = 5000,
        kDefaultPerfPingTimeoutMs   = 1000,
        kDefaultMulticastPort       = 22110,
        kPerfTestMaxMxPayloadLength = 64,
    };

    using Command = CommandEntry<PerfTest>;

    template <CommandId kCommandId> otError Process(Arg aArgs[]);

    otError CommonPerfTestCommand(Arg aArgs[], bool aUseCoap);

    static void HandleTimer(Timer &aTimer);
    void        HandleTimer(void);

    otError     CoapSendRequest(TestMessageContext *aTestMessageContext);
    static void HandleCoapResponse(void                *aContext,
                                   otMessage           *aMessage,
                                   const otMessageInfo *aMessageInfo,
                                   otError              aError);
    void HandleCoapResponse(otMessage *aMessage, const otMessageInfo *aMessageInfo, otError aError, uint16_t aSequence);

    otError     Icmp6SendEchoRequest(TestMessageContext *aTestMessageContext);
    static void HandleIcmpReceive(void                *aContext,
                                  otMessage           *aMessage,
                                  const otMessageInfo *aMessageInfo,
                                  const otIcmp6Header *aIcmpHeader);
    void HandleIcmpReceive(otMessage *aMessage, const otMessageInfo *aMessageInfo, const otIcmp6Header *aIcmpHeader);

    otError     SendUdpMessage(otIp6Address aDestAddr, uint16_t aDestPort, uint32_t aSequence, uint16_t aPayloadLength);
    static bool HandleUdpReceive(void *aContext, const otMessage *aMessage, const otMessageInfo *aMessageInfo);
    bool        HandleUdpReceive(const otMessage *aMessage, const otMessageInfo *aMessageInfo);

    void PrintMessage(otMessage *aMessage, const otMessageInfo *aMessageInfo);

    bool              mTestInProgress : 1;
    TimerMilliContext mTimer;
    otIcmp6Handler    mIcmpHandler;
    otUdpReceiver     mUdpReceiver;
    otUdpSocket       mUdpSock;
    uint8_t           mPayload[kPerfTestMaxPayloadLength];

    // coap / ping test variables
    bool         mUseCoap : 1;
    uint16_t     mTestCount;
    otIp6Address mTestDestAddr;
    uint16_t     mTestPayloadLength;
    uint8_t      mTestNumberInFlight;
    uint16_t     mTestTimeoutMs;

    // coap / ping test stats
    uint16_t           mTestNextSequence;
    uint16_t           mTestFailures;
    uint16_t           mTestPingCounter;
    TestMessageContext mTestMessageContexts[kPerfTestMaxInFlight];
    uint32_t           mTestStartTime;

    union OT_TOOL_PACKED_FIELD
    {
        uint8_t  m8[4];
        uint16_t m16[2];
        uint32_t m32[1];
    } mMessageFields;
};

} // namespace Cli
} // namespace ot

#endif // PERF_TEST_CLI_HPP_
