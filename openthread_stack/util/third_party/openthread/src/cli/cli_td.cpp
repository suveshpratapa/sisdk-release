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
 *   This file implements the `direct` CLI command group for Thread Direct.
 */

#include "cli_td.hpp"

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#include <string.h>

#include <openthread/link.h>
#include <openthread/thread_direct.h>

#include "cli/cli.hpp"
#include "common/as_core_type.hpp"
#include "instance/instance.hpp"
#include "mac/direct_handler.hpp"
#include "thread/direct_peer_table.hpp"

namespace ot {
namespace Cli {

// clang-format off
const ThreadDirect::Command ThreadDirect::sCommands[] = {
    {"channel", &ThreadDirect::ProcessChannel},
    {"help",    &ThreadDirect::ProcessHelp},
    {"link",    &ThreadDirect::ProcessLink},
    {"unlink",  &ThreadDirect::ProcessUnlink},
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
    {"wake", &ThreadDirect::ProcessWake},
#endif
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    {"wakelisten", &ThreadDirect::ProcessWakeListen},
#endif
};
// clang-format on

ThreadDirect::ThreadDirect(otInstance *aInstance, OutputImplementer &aOutputImplementer)
    : Utils(aInstance, aOutputImplementer)
{
}

otError ThreadDirect::ProcessHelp(Arg aArgs[])
{
    OT_UNUSED_VARIABLE(aArgs);

    for (const Command &command : sCommands)
    {
        OutputLine("%s", command.mName);
    }

    return OT_ERROR_NONE;
}

otError ThreadDirect::ProcessChannel(Arg aArgs[])
{
    return ProcessGetSet(aArgs, otLinkGetWakeupChannel, otLinkSetWakeupChannel);
}

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
otError ThreadDirect::ProcessWake(Arg aArgs[])
{
    otError                error = OT_ERROR_NONE;
    otExtAddress           extAddress;
    otThreadDirectWakeType wakeType   = OT_THREAD_DIRECT_WAKE_TYPE_LINK;
    uint16_t               intervalUs = 0;
    uint16_t               durationMs = 0;
    uint8_t                keyIndex   = 0;

    SuccessOrExit(error = aArgs[0].ParseAsHexString(extAddress.m8));

    if (!aArgs[1].IsEmpty())
    {
        SuccessOrExit(error = aArgs[1].ParseAsUint8(keyIndex));

        VerifyOrExit(keyIndex == 0 || keyIndex == OT_MAC_FRAME_WAKE_KEY_INDEX ||
                         (keyIndex >= OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MIN &&
                          keyIndex <= OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MAX),
                     error = OT_ERROR_INVALID_ARGS);

        if (!aArgs[2].IsEmpty())
        {
            uint8_t typeVal;

            SuccessOrExit(error = aArgs[2].ParseAsUint8(typeVal));
            VerifyOrExit(typeVal <= 2, error = OT_ERROR_INVALID_ARGS);
            wakeType = static_cast<otThreadDirectWakeType>(typeVal);
        }

        if (!aArgs[3].IsEmpty())
        {
            SuccessOrExit(error = aArgs[3].ParseAsUint16(intervalUs));
        }

        if (!aArgs[4].IsEmpty())
        {
            SuccessOrExit(error = aArgs[4].ParseAsUint16(durationMs));
        }
    }

    error = otThreadDirectWakeup(GetInstancePtr(), &extAddress, wakeType, intervalUs, durationMs, keyIndex);

exit:
    return error;
}
#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
otError ThreadDirect::ProcessWakeListen(Arg aArgs[])
{
    return ProcessEnableDisable(aArgs, otThreadDirectIsWakeListenerEnabled, otThreadDirectWakeListenerEnable);
}
#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

otError ThreadDirect::ProcessLink(Arg aArgs[])
{
    otError error = OT_ERROR_NONE;

    VerifyOrExit(!aArgs[0].IsEmpty(), error = OT_ERROR_INVALID_ARGS);

    if (aArgs[0] == "slw")
        error = ProcessLinkSlw(aArgs + 1);
    else if (aArgs[0] == "ram")
        error = ProcessLinkRam(aArgs + 1);
    else if (aArgs[0] == "state")
        error = ProcessLinkState(aArgs + 1);
    else if (aArgs[0] == "peers")
        error = ProcessLinkPeers(aArgs + 1);
    else if (aArgs[0] == "timeout")
        error = ProcessLinkTimeout(aArgs + 1);
    else if (aArgs[0] == "key")
        error = ProcessLinkKey(aArgs + 1);
    else if (aArgs[0] == "keyremove")
        error = ProcessLinkKeyRemove(aArgs + 1);
    else
    {
        OT_UNUSED_VARIABLE(aArgs);
        error = OT_ERROR_NOT_IMPLEMENTED;
    }

exit:
    return error;
}

otError ThreadDirect::ProcessUnlink(Arg aArgs[])
{
    otError      error;
    otExtAddress extAddress;

    SuccessOrExit(error = aArgs[0].ParseAsHexString(extAddress.m8));
    VerifyOrExit(aArgs[1].IsEmpty(), error = OT_ERROR_INVALID_ARGS);
    SuccessOrExit(error = otThreadDirectUnlink(GetInstancePtr(), &extAddress));

exit:
    return error;
}

otError ThreadDirect::ProcessLinkSlw(Arg aArgs[])
{
    otError error = OT_ERROR_NONE;

    if (aArgs[0].IsEmpty())
    {
        otThreadDirectLocalSca sca;

        SuccessOrExit(error = otThreadDirectGetLocalSca(GetInstancePtr(), &sca));
        OutputLine("slot-duration: %u", sca.mSlotDuration);
        OutputLine("period: %u slots", sca.mSlwPeriodSlots);
    }
    else
    {
        uint16_t period;

        SuccessOrExit(error = aArgs[0].ParseAsUint16(period));
        VerifyOrExit(aArgs[1].IsEmpty(), error = OT_ERROR_INVALID_ARGS);

        error = otThreadDirectSetSlwSchedule(GetInstancePtr(), period);
    }

exit:
    return error;
}

otError ThreadDirect::ProcessLinkRam(Arg aArgs[])
{
    otError error = OT_ERROR_NONE;

    if (aArgs[0].IsEmpty())
    {
        otThreadDirectLocalSca sca;

        SuccessOrExit(error = otThreadDirectGetLocalSca(GetInstancePtr(), &sca));
        OutputLine("available: %s", sca.mRam.mAvailable ? "yes" : "no");
        OutputLine("duration: %u", sca.mRam.mDuration);
        OutputLine("offset:   %d us", static_cast<int>(sca.mRam.mOffsetUs));

        if (sca.mRam.mAvailable && (sca.mRam.mDuration > 0))
        {
            uint8_t numBytes = static_cast<uint8_t>((sca.mRam.mDuration + 7) / 8);

            OutputFormat("bits:     0x");

            for (uint8_t i = 0; i < numBytes && i < sizeof(sca.mRam.mBits); i++)
            {
                OutputFormat("%02x", sca.mRam.mBits[i]);
            }

            OutputNewLine();
        }
    }
    else if (aArgs[0] == "clear")
    {
        otThreadDirectRamParams params;

        memset(&params, 0, sizeof(params));
        error = otThreadDirectSetRamOverride(GetInstancePtr(), &params);
    }
    else if (aArgs[0] == "set")
    {
        otThreadDirectRamParams params;
        uint8_t                 duration;
        int16_t                 offsetUs;
        uint8_t                 hexBuf[sizeof(params.mBits)];
        uint16_t                hexLen = sizeof(hexBuf);

        memset(&params, 0, sizeof(params));

        SuccessOrExit(error = aArgs[1].ParseAsHexString(hexLen, hexBuf));
        SuccessOrExit(error = aArgs[2].ParseAsInt16(offsetUs));
        SuccessOrExit(error = aArgs[3].ParseAsUint8(duration));
        VerifyOrExit(aArgs[4].IsEmpty(), error = OT_ERROR_INVALID_ARGS);

        memcpy(params.mBits, hexBuf, hexLen);
        params.mAvailable = true;
        params.mOffsetUs  = offsetUs;
        params.mDuration  = duration;

        error = otThreadDirectSetRamOverride(GetInstancePtr(), &params);
    }
    else
    {
        error = OT_ERROR_INVALID_ARGS;
    }

exit:
    return error;
}

otError ThreadDirect::ProcessLinkState(Arg aArgs[])
{
    otError                error = OT_ERROR_NONE;
    otThreadDirectLocalSca sca;
    bool                   hasLinkedPeer;

    OT_UNUSED_VARIABLE(aArgs);

    hasLinkedPeer = AsCoreType(GetInstancePtr()).Get<DirectPeerTable>().HasPeers(Neighbor::kInStateValid);

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
    OutputLine("role:  wi");
    if (hasLinkedPeer)
    {
        OutputLine("state: linked");
    }
    else if (otThreadDirectIsWakeBurstActive(GetInstancePtr()))
    {
        OutputLine("state: waking");
    }
    else
    {
        OutputLine("state: idle");
    }
#elif OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    OutputLine("role:  wl");
    if (hasLinkedPeer)
    {
        OutputLine("state: linked");
    }
    else if (otThreadDirectIsWakeListenerEnabled(GetInstancePtr()))
    {
        OutputLine("state: listening");
    }
    else
    {
        OutputLine("state: disabled");
    }
#endif

    SuccessOrExit(error = otThreadDirectGetLocalSca(GetInstancePtr(), &sca));

    OutputLine("slot-duration: %u", sca.mSlotDuration);
    OutputLine("slw-period:  %u slots", sca.mSlwPeriodSlots);
    OutputLine("slw-timeout: %lu ms", static_cast<unsigned long>(otThreadDirectGetSlwTimeout(GetInstancePtr())));
    OutputLine("ram-available: %s", sca.mRam.mAvailable ? "yes" : "no");
    OutputLine("ram-duration: %u", sca.mRam.mDuration);
    OutputLine("ram-offset:   %d us", static_cast<int>(sca.mRam.mOffsetUs));

    if (sca.mRam.mAvailable && (sca.mRam.mDuration > 0))
    {
        uint8_t numBytes = static_cast<uint8_t>((sca.mRam.mDuration + 7) / 8);

        OutputFormat("ram-bits:     0x");

        for (uint8_t i = 0; i < numBytes && i < sizeof(sca.mRam.mBits); i++)
        {
            OutputFormat("%02x", sca.mRam.mBits[i]);
        }

        OutputNewLine();
    }

exit:
    return error;
}

otError ThreadDirect::ProcessLinkPeers(Arg aArgs[])
{
    OT_UNUSED_VARIABLE(aArgs);

    uint32_t         localIntervalMs = otThreadDirectGetSlwTimeout(GetInstancePtr());
    DirectPeerTable &peerTable       = AsCoreType(GetInstancePtr()).Get<DirectPeerTable>();

    if (!peerTable.HasPeers(Neighbor::kInStateValid))
    {
        OutputLine("no peers");
    }

    for (const DirectPeer &peer : peerTable.Iterate(Neighbor::kInStateValid))
    {
        {
            const Mac::ExtAddress &a = peer.GetExtAddress();

            if (peer.GetSlwPeriodSlots() != 0)
            {
                OutputLine(
                    "addr: %02X%02X%02X%02X%02X%02X%02X%02X  key: %u  slw: %u/%u slots", a.m8[0], a.m8[1], a.m8[2],
                    a.m8[3], a.m8[4], a.m8[5], a.m8[6], a.m8[7], static_cast<unsigned>(peer.GetWakeKeyIndex()),
                    static_cast<unsigned>(peer.GetSlwPeriodSlots()), static_cast<unsigned>(peer.GetSlwPhaseSlots()));
            }
            else
            {
                OutputLine("addr: %02X%02X%02X%02X%02X%02X%02X%02X  key: %u  slw: not configured", a.m8[0], a.m8[1],
                           a.m8[2], a.m8[3], a.m8[4], a.m8[5], a.m8[6], a.m8[7],
                           static_cast<unsigned>(peer.GetWakeKeyIndex()));
            }
        }

        {
            uint32_t peerIntervalMs = peer.GetSupervisionIntervalMs();

            OutputLine("  supervision: local=%lu peer=%lu failures=%u/%u", static_cast<unsigned long>(localIntervalMs),
                       static_cast<unsigned long>(peerIntervalMs),
                       static_cast<unsigned>(peer.GetSupervisionProbeAttempts()),
                       static_cast<unsigned>(DirectHandler::kMaxSupervisionFailures));
        }
    }

    return OT_ERROR_NONE;
}

otError ThreadDirect::ProcessLinkTimeout(Arg aArgs[])
{
    otError error = OT_ERROR_NONE;

    if (aArgs[0].IsEmpty())
    {
        OutputLine("%lu", static_cast<unsigned long>(otThreadDirectGetSlwTimeout(GetInstancePtr())));
    }
    else
    {
        uint32_t timeout;

        SuccessOrExit(error = aArgs[0].ParseAsUint32(timeout));
        VerifyOrExit(aArgs[1].IsEmpty(), error = OT_ERROR_INVALID_ARGS);
        error = otThreadDirectSetSlwTimeout(GetInstancePtr(), timeout);
    }

exit:
    return error;
}

otError ThreadDirect::ProcessLinkKey(Arg aArgs[])
{
    otError               error = OT_ERROR_NONE;
    uint8_t               keyIndex;
    otThreadDirectWakeKey key;

    SuccessOrExit(error = aArgs[0].ParseAsUint8(keyIndex));
    VerifyOrExit(keyIndex >= OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MIN && keyIndex <= OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MAX,
                 error = OT_ERROR_INVALID_ARGS);
    SuccessOrExit(error = aArgs[1].ParseAsHexString(key.m8));

    error = otThreadDirectSetGuestWakeKey(GetInstancePtr(), keyIndex, &key);

exit:
    return error;
}

otError ThreadDirect::ProcessLinkKeyRemove(Arg aArgs[])
{
    otError error = OT_ERROR_NONE;
    uint8_t keyIndex;

    SuccessOrExit(error = aArgs[0].ParseAsUint8(keyIndex));
    VerifyOrExit(keyIndex >= OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MIN && keyIndex <= OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MAX,
                 error = OT_ERROR_INVALID_ARGS);

    error = otThreadDirectRemoveGuestWakeKey(GetInstancePtr(), keyIndex);

exit:
    return error;
}

otError ThreadDirect::Process(Arg aArgs[])
{
    otError        error = OT_ERROR_INVALID_COMMAND;
    const Command *command;

    if (aArgs[0].IsEmpty() || (strcmp(aArgs[0].GetCString(), "help") == 0))
    {
        IgnoreError(ProcessHelp(aArgs));
        ExitNow(error = OT_ERROR_NONE);
    }

    command = BinarySearch::Find(aArgs[0].GetCString(), sCommands);

    VerifyOrExit(command != nullptr);
    error = (this->*command->mHandler)(aArgs + 1);

exit:
    return error;
}

} // namespace Cli
} // namespace ot

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
