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

#ifdef SL_COMPONENT_CATALOG_PRESENT
#include "sl_component_catalog.h"
#endif

#include "perf_test_cli.hpp"

#include "cli/cli.hpp"

#include "common/new.hpp"

#if defined(SL_CATALOG_OT_DEBUG_CHANNEL_PRESENT)
#include "debug_channel.h"
#include <sl_iostream_debug.h>
#include <sl_iostream_swo_itm_8.h>
#endif

#if SL_OPENTHREAD_MULTI_INSTANCE_CLI_ENABLE
#include "cli/multi_instance_cli.h"
#endif

using ot::BigEndian::HostSwap16;
using ot::BigEndian::HostSwap32;

namespace ot {
namespace Cli {

PerfTest *PerfTest::sPerfTest = nullptr;
static OT_DEFINE_ALIGNED_VAR(sPerfTestRaw, sizeof(PerfTest), uint64_t);

PerfTest::PerfTest(Instance *aInstance, OutputImplementer &aOutputImplementer)
    : Utils(aInstance, aOutputImplementer)
    , mTestInProgress(false)
    , mTimer(*aInstance, HandleTimer, this)
{
    memset(mPayload, 'P', sizeof(mPayload));
    memset(&mUdpSock, 0, sizeof(mUdpSock));

    // Register the ICMP6 receive callback
    mIcmpHandler.mReceiveCallback = HandleIcmpReceive;
    mIcmpHandler.mContext         = this;
    mIcmpHandler.mNext            = nullptr;
    IgnoreError(otIcmp6RegisterHandler(aInstance, &mIcmpHandler));

    // Start the Coap service
    IgnoreError(otCoapStart(aInstance, OT_DEFAULT_COAP_PORT));

    // Register the CLI UDP receive callback
    mUdpReceiver.mHandler = HandleUdpReceive;
    mUdpReceiver.mContext = this;
    mUdpReceiver.mNext    = nullptr;
    IgnoreError(otUdpAddReceiver(aInstance, &mUdpReceiver));
}

extern "C" void sl_ot_perf_test_cli_init(void)
{
    Instance *instance;
#if OPENTHREAD_CONFIG_MULTIPLE_STATIC_INSTANCE_ENABLE
#if SL_OPENTHREAD_MULTI_INSTANCE_CLI_ENABLE
    uint8_t currentInstanceIndex = sl_ot_get_current_instance_index();
    instance                     = static_cast<Instance *>(otInstanceGetInstance(currentInstanceIndex));
#else
    instance = static_cast<Instance *>(otInstanceGetInstance(0));
#endif

#else // !OPENTHREAD_CONFIG_MULTIPLE_STATIC_INSTANCE_ENABLE
    instance = static_cast<Instance *>(otInstanceGetSingle());
#endif
    PerfTest::sPerfTest = new (&sPerfTestRaw) PerfTest(instance, Interpreter::GetInterpreter());
}

extern "C" otError PerfTestCommand(void *aContext, uint8_t aArgc, char *aArgv[])
{
    OT_UNUSED_VARIABLE(aContext);
    otError    error = OT_ERROR_NONE;
    Utils::Arg args[PerfTest::kMaxArgs + 1];

    VerifyOrExit(aArgc < PerfTest::kMaxArgs + 1, error = OT_ERROR_INVALID_ARGS);

    for (uint8_t i = 0; i < PerfTest::kMaxArgs + 1; i++)
    {
        if (i < aArgc)
        {
            args[i].SetCString(aArgv[i]);
        }
        else
        {
            args[i].Clear();
        }
    }

    error = PerfTest::GetPerfTest().Process(args);

exit:
    return error;
}

// Usage:
// perf cancel
//
// Cancel the current test
template <> otError PerfTest::Process<Cmd("cancel")>(Arg aArgs[])
{
    OT_UNUSED_VARIABLE(aArgs);

    mTimer.Stop();
    mTestInProgress = false;

    return OT_ERROR_NONE;
}

// Usage:
// perf coap <count> <destIp> <length> <num in flight> <timeout>*
//
// "perf coap" sends <count> coap messages to <destIp> with payload length <length> and
// keeps <num in flight> in process at a time. Response is expected to be recevied by
// <timeout> ms. If <timeout> is not specified it default to kDefaultPerfCoapTimeoutMs.
template <> otError PerfTest::Process<Cmd("coap")>(Arg aArgs[])
{
    return CommonPerfTestCommand(aArgs, true);
}

// Usage:
// perf mx <length> <sequence number> <dest port>*
//
// "perf mx" sends a multicast UDP message to address "ff03::1" with payload length <length>.
// The <sequence number> is stored in the first 4 bytes of the payload.  If <dest port> is
// specfied then the multicast message is directed to that port otherwise it is sent to
// the default port of 22110.
template <> otError PerfTest::Process<Cmd("mx")>(Arg aArgs[])
{
    otError      error        = OT_ERROR_NONE;
    const char  *errorMessage = nullptr;
    uint16_t     payloadLength;
    uint32_t     sequence = 0;
    otIp6Address destAddr;
    uint16_t     destPort = kDefaultMulticastPort;

    errorMessage = "Test in progress";
    VerifyOrExit(!mTestInProgress, error = OT_ERROR_INVALID_STATE);

    errorMessage = "Failure while parsing <length>";
    SuccessOrExit(error = aArgs[0].ParseAsUint16(payloadLength));
    if (payloadLength < 4 || payloadLength > kPerfTestMaxMxPayloadLength)
    {
        errorMessage = nullptr;
        OutputLine("Length must be greater than or equal to 4 and less than or equal to %d",
                   kPerfTestMaxMxPayloadLength);
        ExitNow(error = OT_ERROR_INVALID_ARGS);
    }

    errorMessage = "Failure while parsing <sequence>";
    SuccessOrExit(error = aArgs[1].ParseAsUint32(sequence));

    if (!aArgs[2].IsEmpty())
    {
        errorMessage = "Failure while parsing <dest port>";
        SuccessOrExit(error = aArgs[2].ParseAsUint16(destPort));
    }

    IgnoreError(otIp6AddressFromString("ff03::1", &destAddr));

    // Attempt to send a UDP multicast message
    errorMessage = nullptr;
    SuccessOrExit(error = SendUdpMessage(destAddr, destPort, sequence, payloadLength));

exit:
    if (errorMessage != nullptr)
    {
        OutputLine("%s", errorMessage);
    }

    return error;
}

// Usage:
// perf ping <count> <destIp> <length> <num in flight>
//
// "perf ping" does the same as "perf coap" but uses ICMP pings instead of coap messages.
// If <timeout> is not specified it default to kDefaultPerfPingTimeoutMs.
template <> otError PerfTest::Process<Cmd("ping")>(Arg aArgs[])
{
    return CommonPerfTestCommand(aArgs, false);
}

// Usage:
// perf print_ping_counter
template <> otError PerfTest::Process<Cmd("print_ping_counter")>(Arg aArgs[])
{
    OT_UNUSED_VARIABLE(aArgs);

    OutputLine("Received %d pings since last reset.", mTestPingCounter);
    return OT_ERROR_NONE;
}

// Usage:
// perf reset_ping_counter
template <> otError PerfTest::Process<Cmd("reset_ping_counter")>(Arg aArgs[])
{
    OT_UNUSED_VARIABLE(aArgs);

    mTestPingCounter = 0;
    return OT_ERROR_NONE;
}

otError PerfTest::Process(Arg aArgs[])
{
#define CmdEntry(aCommandString) {aCommandString, &PerfTest::Process<Cmd(aCommandString)>}

    static constexpr Command kCommands[] = {
        CmdEntry("cancel"),
        CmdEntry("coap"),
        CmdEntry("mx"),
        CmdEntry("ping"),
        CmdEntry("print_ping_counter"),
        CmdEntry("reset_ping_counter"),
    };

#undef CmdEntry

    static_assert(BinarySearch::IsSorted(kCommands), "kCommands is not sorted");

    otError        error = OT_ERROR_NONE;
    const Command *command;

    if (aArgs[0].IsEmpty() || (aArgs[0] == "help"))
    {
        OutputCommandTable(kCommands);
        ExitNow();
    }

    command = BinarySearch::Find(aArgs[0].GetCString(), kCommands);
    VerifyOrExit(command != nullptr, error = OT_ERROR_INVALID_COMMAND);

    error = (this->*command->mHandler)(aArgs + 1);

exit:
    return error;
}

otError PerfTest::CommonPerfTestCommand(Arg aArgs[], bool aUseCoap)
{
    otError     error        = OT_ERROR_NONE;
    const char *errorMessage = nullptr;

    errorMessage = "Test in progress";
    VerifyOrExit(!mTestInProgress, error = OT_ERROR_INVALID_STATE);

    errorMessage = "Failure while parsing <count>";
    SuccessOrExit(error = aArgs[0].ParseAsUint16(mTestCount));

    errorMessage = "Failure while parsing <IP address>";
    SuccessOrExit(error = aArgs[1].ParseAsIp6Address(mTestDestAddr));

    errorMessage = "Failure while parsing <length>";
    SuccessOrExit(error = aArgs[2].ParseAsUint16(mTestPayloadLength));
    if (mTestPayloadLength > kPerfTestMaxPayloadLength)
    {
        errorMessage = nullptr;
        OutputLine("Length must be less than or equal to %d", kPerfTestMaxPayloadLength);
        ExitNow(error = OT_ERROR_INVALID_ARGS);
    }

    errorMessage = "Failure while parsing <number in flight>";
    SuccessOrExit(error = aArgs[3].ParseAsUint8(mTestNumberInFlight));
    if (mTestNumberInFlight == 0 || mTestNumberInFlight > kPerfTestMaxInFlight)
    {
        errorMessage = nullptr;
        OutputLine("Number in flight must be between 1 and %d, inclusive", kPerfTestMaxInFlight);
        ExitNow(error = OT_ERROR_INVALID_ARGS);
    }

    if (!aArgs[4].IsEmpty())
    {
        errorMessage = "Failure while parsing <timeout>";
        SuccessOrExit(error = aArgs[4].ParseAsUint16(mTestTimeoutMs));
    }
    else
    {
        mTestTimeoutMs = (aUseCoap) ? kDefaultPerfCoapTimeoutMs : kDefaultPerfPingTimeoutMs;
    }

    errorMessage = nullptr;
    OutputFormat("type:%s count:%d destIp:", (aUseCoap) ? "coap" : "ping", mTestCount);
    OutputIp6Address(mTestDestAddr);
    OutputLine(" length:%d num-in-flight:%d timeout:%dms", mTestPayloadLength, mTestNumberInFlight, mTestTimeoutMs);

    // Initialize Performance Test Params
    mUseCoap          = aUseCoap;
    mTestNextSequence = 1;
    mTestFailures     = 0;
    mTestPingCounter  = 0;
    memset(mTestMessageContexts, 0, sizeof(mTestMessageContexts));
    mTestStartTime = otPlatAlarmMilliGetNow();

    mTestInProgress = true;
    mTimer.Start(0);

exit:
    if (errorMessage != nullptr)
    {
        OutputLine("%s", errorMessage);
    }

    return error;
}

void PerfTest::HandleTimer(Timer &aTimer)
{
    static_cast<PerfTest *>(static_cast<TimerMilliContext &>(aTimer).GetContext())->HandleTimer();
}

// Each time the handler is called, we inspect <mTestNumberInFlight>
// entries of the <mTestMessageContexts> array.
// - if the entry is empty (value of 0 for mSequence), and there are more messages to be sent,
//   we send one and store the sequence and start time.
// - if the entry is nonempty, we check if it has timed out.
// We reschedule the event for the next timeout, or immediately if any entries
// have been freed up.  Otherwise, we're done.
void PerfTest::HandleTimer(void)
{
    mTimer.Stop();
    uint32_t now       = otPlatAlarmMilliGetNow();
    bool     setActive = false;
    uint32_t delayMs   = mTestTimeoutMs;

    VerifyOrExit(mTestInProgress);

    for (int i = 0; i < mTestNumberInFlight; i++)
    {
        if (mTestMessageContexts[i].mSequence == 0)
        {
            // Empty entry, send a new coap message or ping.
            if (mTestNextSequence <= mTestCount)
            {
                setActive     = true;
                otError error = OT_ERROR_NONE;

                mTestMessageContexts[i].mPerfTest  = this;
                mTestMessageContexts[i].mSequence  = mTestNextSequence;
                mTestMessageContexts[i].mStartTime = now;

                if (mUseCoap)
                {
                    error = CoapSendRequest(&mTestMessageContexts[i]);
                }
                else
                {
                    error = Icmp6SendEchoRequest(&mTestMessageContexts[i]);
                }

                if (error != OT_ERROR_NONE)
                {
                    OutputLine("%d: TX Failed", mTestNextSequence);
                    mTestFailures++;
                    mTestMessageContexts[i].mSequence = 0;
                    delayMs                           = 0;
                }
                mTestNextSequence++;
            }
        }
        else
        {
            // Check if the entry has timed out.
            setActive        = true;
            uint32_t elapsed = (now - mTestMessageContexts[i].mStartTime);
            if (elapsed > mTestTimeoutMs)
            {
                OutputLine("%d: Timed out", mTestMessageContexts[i].mSequence);
                mTestFailures++;
                mTestMessageContexts[i].mSequence = 0;
                delayMs                           = 0;
            }
            else
            {
                delayMs = OT_MIN(mTestTimeoutMs - elapsed, delayMs);
            }
        }
    }

    if (setActive)
    {
        mTimer.Start(delayMs);
    }
    else
    {
        uint32_t elapsed = (now - mTestStartTime);
        mTestInProgress  = false;
        OutputLine("sent:%d type:%s time:%lums fail:%d",
                   mTestCount,
                   mUseCoap ? "coap" : "ping",
                   ToUlong(elapsed),
                   mTestFailures);
    }

exit:
    return;
}

otError PerfTest::CoapSendRequest(TestMessageContext *aTestMessageContext)
{
    otError       error   = OT_ERROR_NONE;
    otMessage    *message = nullptr;
    otMessageInfo messageInfo;

    memset(&messageInfo, 0, sizeof(messageInfo));
    messageInfo.mPeerAddr = mTestDestAddr;
    messageInfo.mPeerPort = OT_DEFAULT_COAP_PORT;

    message = otCoapNewMessage(GetInstancePtr(), nullptr);
    VerifyOrExit(message != nullptr, error = OT_ERROR_NO_BUFS);

    otCoapMessageInit(message, OT_COAP_TYPE_CONFIRMABLE, OT_COAP_CODE_POST);
    otCoapMessageGenerateToken(message, OT_COAP_DEFAULT_TOKEN_LENGTH);

    SuccessOrExit(error = otCoapMessageAppendUriPathOptions(message, "c/perf"));
    SuccessOrExit(error = otCoapMessageSetPayloadMarker(message));
    SuccessOrExit(error = otMessageAppend(message, mPayload, mTestPayloadLength));

    SuccessOrExit(error = otCoapSendRequest(GetInstancePtr(),
                                            message,
                                            &messageInfo,
                                            &PerfTest::HandleCoapResponse,
                                            aTestMessageContext));

exit:
    if (error != OT_ERROR_NONE)
    {
        OutputLine("CoapSendRequest: Failed - %s", otThreadErrorToString(error));
        if (message != nullptr)
        {
            otMessageFree(message);
        }
    }

    return error;
}

void PerfTest::HandleCoapResponse(void                *aContext,
                                  otMessage           *aMessage,
                                  const otMessageInfo *aMessageInfo,
                                  otError              aError)
{
    PerfTest::TestMessageContext *testMessageContext = static_cast<TestMessageContext *>(aContext);
    testMessageContext->mPerfTest->HandleCoapResponse(aMessage, aMessageInfo, aError, testMessageContext->mSequence);
}

void PerfTest::HandleCoapResponse(otMessage           *aMessage,
                                  const otMessageInfo *aMessageInfo,
                                  otError              aError,
                                  uint16_t             aSequence)
{
    uint32_t now   = otPlatAlarmMilliGetNow();
    otError  error = OT_ERROR_NONE;

    VerifyOrExit(mTestInProgress);
    VerifyOrExit(aError == OT_ERROR_NONE, error = aError);

    for (int i = 0; i < mTestNumberInFlight; i++)
    {
        if (mTestMessageContexts[i].mSequence == aSequence)
        {
            uint32_t elapsed = (now - mTestMessageContexts[i].mStartTime);
            OutputLine("%d: %lu ms", aSequence, ToUlong(elapsed));
            mTestMessageContexts[i].mSequence = 0;
            mTimer.Start(0);
            ExitNow();
        }
    }

    OutputLine("%d: arrived after timeout", aSequence);

exit:
    if (error != OT_ERROR_NONE)
    {
        OutputFormat("%d: coap response error %d: %s", aSequence, error, otThreadErrorToString(error));
        PrintMessage(aMessage, aMessageInfo);
        OutputNewLine();
    }
}

otError PerfTest::Icmp6SendEchoRequest(TestMessageContext *aTestMessageContext)
{
    otError       error   = OT_ERROR_NONE;
    otMessage    *message = nullptr;
    otMessageInfo messageInfo;

    memset(&messageInfo, 0, sizeof(messageInfo));
    messageInfo.mPeerAddr = mTestDestAddr;

    message = otIp6NewMessage(GetInstancePtr(), nullptr);
    VerifyOrExit(message != nullptr, error = OT_ERROR_NO_BUFS);

    mMessageFields.m16[0] = HostSwap16(kPerfTestId);
    mMessageFields.m16[1] = HostSwap16(aTestMessageContext->mSequence);

    SuccessOrExit(error = otMessageAppend(message, mMessageFields.m8, 4));
    SuccessOrExit(error = otMessageAppend(message, mPayload, mTestPayloadLength));

    SuccessOrExit(error = otIcmp6SendEchoRequest(GetInstancePtr(), message, &messageInfo, kPerfTestId));

exit:
    if (error != OT_ERROR_NONE)
    {
        OutputLine("Icmp6SendEchoRequest: Failed - %s", otThreadErrorToString(error));
        if (message != nullptr)
        {
            otMessageFree(message);
        }
    }

    return error;
}

void PerfTest::HandleIcmpReceive(void                *aContext,
                                 otMessage           *aMessage,
                                 const otMessageInfo *aMessageInfo,
                                 const otIcmp6Header *aIcmpHeader)
{
    static_cast<PerfTest *>(aContext)->HandleIcmpReceive(aMessage, aMessageInfo, aIcmpHeader);
}

void PerfTest::HandleIcmpReceive(otMessage           *aMessage,
                                 const otMessageInfo *aMessageInfo,
                                 const otIcmp6Header *aIcmpHeader)
{
    uint16_t    id;
    uint16_t    sequence;
    uint32_t    now          = otPlatAlarmMilliGetNow();
    const char *errorMessage = nullptr;

    VerifyOrExit(mTestInProgress);
    mTestPingCounter++;

    VerifyOrExit(otMessageRead(aMessage, otMessageGetOffset(aMessage), mMessageFields.m8, 4) == 4,
                 errorMessage = "ICMP RX: Invalid length");

    id       = HostSwap16(mMessageFields.m16[0]);
    sequence = HostSwap16(mMessageFields.m16[1]);

    VerifyOrExit(aIcmpHeader->mType != OT_ICMP6_TYPE_DST_UNREACH, errorMessage = "ICMP RX: DEST UNREACHABLE");
    VerifyOrExit(id == kPerfTestId, errorMessage = "ICMP RX: Invalid Id");
    VerifyOrExit(aIcmpHeader->mType == OT_ICMP6_TYPE_ECHO_REPLY, errorMessage = "ICMP RX: Invalid Type");

    for (int i = 0; i < mTestNumberInFlight; i++)
    {
        if (mTestMessageContexts[i].mSequence == sequence)
        {
            uint32_t elapsed = (now - mTestMessageContexts[i].mStartTime);
            OutputLine("%d: %lu ms", sequence, ToUlong(elapsed));
            mTestMessageContexts[i].mSequence = 0;
            mTimer.Start(0);
            ExitNow();
        }
    }

    OutputLine("%d: arrived after timeout", sequence);

exit:
    if (errorMessage != nullptr)
    {
        OutputFormat("%s", errorMessage);
        PrintMessage(aMessage, aMessageInfo);
        OutputNewLine();
    }

    return;
}

otError PerfTest::SendUdpMessage(otIp6Address aDestAddr,
                                 uint16_t     aDestPort,
                                 uint32_t     aSequence,
                                 uint16_t     aPayloadLength)
{
    otError       error   = OT_ERROR_NONE;
    otMessage    *message = nullptr;
    otMessageInfo messageInfo;

    if (!otUdpIsOpen(GetInstancePtr(), &mUdpSock))
    {
        otSockAddr bindAddr;

        memset(&bindAddr, 0, sizeof(bindAddr));
        bindAddr.mPort = kDefaultMulticastPort;

        SuccessOrExit(error = otUdpOpen(GetInstancePtr(), &mUdpSock, NULL, NULL));
        SuccessOrExit(error = otUdpBind(GetInstancePtr(), &mUdpSock, &bindAddr, OT_NETIF_THREAD_INTERNAL));
    }

    memset(&messageInfo, 0, sizeof(messageInfo));
    messageInfo.mPeerAddr = aDestAddr;
    messageInfo.mPeerPort = aDestPort;

    // Create a new message
    message = otUdpNewMessage(GetInstancePtr(), NULL);
    VerifyOrExit(message != NULL, error = OT_ERROR_NO_BUFS);

    // Create payload with sequence number at the start of the payload
    mMessageFields.m32[0] = HostSwap32(aSequence);

    // Flip endianness of sequence so that sl_debug_binary_format prints it as big-endian
    aSequence = LittleEndian::HostSwap32(mMessageFields.m32[0]);

    // Append message
    SuccessOrExit(error = otMessageAppend(message, mMessageFields.m8, 4));
    SuccessOrExit(error = otMessageAppend(message, mPayload, aPayloadLength - 4));

    // Send message
    SuccessOrExit(error = otUdpSend(GetInstancePtr(), &mUdpSock, message, &messageInfo));

#if defined(SL_CATALOG_OT_DEBUG_CHANNEL_PRESENT)
    // Print message on debug backchannel
    sl_debug_binary_format(EM_DEBUG_LATENCY,
                           "BBD",
                           0, // frame control
                           4, // length of sequence number
                           aSequence);
#else
    OutputLine("EM_DEBUG_LATENCY: frame control:0, sequence number:%08x", aSequence);
#endif // SL_CATALOG_OT_DEBUG_CHANNEL_PRESENT

exit:
    if (error != OT_ERROR_NONE)
    {
        OutputLine("SendUdpMessage: Failed - %s", otThreadErrorToString(error));
        if (message != nullptr)
        {
            otMessageFree(message);
        }
    }

    return error;
}

bool PerfTest::HandleUdpReceive(void *aContext, const otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    return static_cast<PerfTest *>(aContext)->HandleUdpReceive(aMessage, aMessageInfo);
}

bool PerfTest::HandleUdpReceive(const otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    bool     handled = false;
    uint16_t payloadLength;
    uint32_t sequence;

    VerifyOrExit(aMessageInfo->mSockPort == kDefaultMulticastPort);
    payloadLength = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);
    VerifyOrExit(payloadLength >= 4 && payloadLength <= kPerfTestMaxMxPayloadLength);

    IgnoreReturnValue(otMessageRead(aMessage, otMessageGetOffset(aMessage), mMessageFields.m8, 4));
    sequence = LittleEndian::HostSwap32(mMessageFields.m32[0]);

#if defined(SL_CATALOG_OT_DEBUG_CHANNEL_PRESENT)
    // Print message on debug backchannel
    sl_debug_binary_format(EM_DEBUG_LATENCY,
                           "BBD",
                           1, // frame control
                           4, // length of sequence number
                           sequence);
#else
    OutputLine("EM_DEBUG_LATENCY: frame control:1, sequence number:%08x\r\n", sequence);
#endif

    // The message is handled by this receiver and should not be further processed.
    handled = true;

exit:
    return handled;
}

void PerfTest::PrintMessage(otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    uint8_t  buf[kMaxBufferSize];
    uint16_t bytesToPrint;
    uint16_t bytesPrinted = 0;
    uint16_t length       = (aMessage != nullptr) ? (otMessageGetLength(aMessage) - otMessageGetOffset(aMessage)) : 0;

    if (length > 0)
    {
        OutputFormat(" from ");
        OutputIp6Address(aMessageInfo->mPeerAddr);
        OutputFormat(" with payload: ");

        while (length > 0)
        {
            bytesToPrint = (length < sizeof(buf)) ? length : sizeof(buf);
            otMessageRead(aMessage, otMessageGetOffset(aMessage) + bytesPrinted, buf, bytesToPrint);

            OutputBytes(buf, static_cast<uint8_t>(bytesToPrint));

            length -= bytesToPrint;
            bytesPrinted += bytesToPrint;
        }
    }
}

} // namespace Cli
} // namespace ot
