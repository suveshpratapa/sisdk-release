/*
Apple Inc.
Apple HomeKit Reliability Features for Thread
PLA Sample Code License Agreement

IMPORTANT: This Apple software is supplied to you by Apple Inc. ("Apple"), under a Master Prototype License and Confidentiality Agreement (“PLA”) between you and Apple. Use of this Apple software is governed by and subject to the terms and conditions of the PLA and any applicable Project exhibits under the PLA (collectively, "PLA Agreements"), including, but not limited to, the restrictions specified in the PLA provision entitled "License Limitations", and is further subject to your agreement to the following additional terms, and your agreement that the use, installation, or modification of this Apple software constitutes acceptance of these additional terms. Except as otherwise provided, capitalized terms used herein are defined in the PLA. If you do not agree with these additional terms, you may not use, install, or modify this Apple software.

Subject to all of these terms and in consideration of your agreement to abide by them, Apple grants you, during the period specified in the PLA Agreements, a personal, non-exclusive license, under Apple's copyrights in this Apple software (the "Apple Software"), to use, reproduce, and modify the Apple Software in source form, and to use, reproduce, and modify the Apple Software, with or without modifications, in binary form, in each of the foregoing cases solely in accordance with the terms of the PLA Agreements. Neither the name, trademarks, service marks, or logos of Apple Inc. may be used to endorse or promote products derived from the Apple Software without specific prior written permission from Apple. Except as expressly stated in this notice, no other rights or licenses, express or implied, are granted by Apple herein, including but not limited to any patent rights that may be infringed by your derivative works or by other works in which the Apple Software may be incorporated. Apple may terminate this license to the Apple Software in accordance with the terms of the PLA Agreements. This license will automatically terminate upon the termination of the PLA or the applicable Project exhibit.

Unless you explicitly state otherwise, if you provide any ideas, suggestions, recommendations, bug fixes or enhancements to Apple in connection with this software ("Feedback"), you hereby grant to Apple a non-exclusive, fully paid-up, perpetual, irrevocable, worldwide license to make, use, reproduce, incorporate, modify, display, perform, sell, make or have made derivative works of, distribute (directly or indirectly) and sublicense, such Feedback in connection with Apple products and services. Providing this Feedback is voluntary, but if you do provide Feedback to Apple, you acknowledge and agree that Apple may exercise the license granted above without the payment of royalties and without any other obligations or restrictions. 

The Apple Software is provided by Apple on an "AS IS" basis. APPLE MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.

IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT,
TORT (INCLUDING NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Copyright (c) 2025 Apple Inc. All Rights Reserved.
*/

#include "cli_prioritized_routing.hpp"

#include <openthread/thread_ftd.h>
#include <openthread/prioritized_routing.h>

#include "cli/cli.hpp"

#include "openthread-core-config.h"

#include <openthread/thread_ftd.h>

#if PRIORITIZED_ROUTING_ENABLE

#include "cli/cli_utils.hpp"

namespace ot {
namespace Cli {

otError PrioritizedRouting::Process(Arg aArgs[])
{
    otError error = OT_ERROR_INVALID_COMMAND;

    // Skip "prioritizedrouting" if present
    if (aArgs[0] == "prioritizedrouting")
    {
        aArgs++;
    }

    if (aArgs[0].IsEmpty() || (aArgs[0] == "help"))
    {
        OutputLine("enable");
        OutputLine("nexthop");
        OutputLine("routecostthreshold");
        OutputLine("routetable");

        ExitNow(error = aArgs[0].IsEmpty() ? error : OT_ERROR_NONE);
    }

    if (aArgs[0] == "enable")
    {
        error = ProcessEnable(&aArgs[1]);
        ExitNow();
    }
    if (aArgs[0] == "routetable")
    {
        error = ProcessRouteTable(&aArgs[1]);
        ExitNow();
    }
    else if (aArgs[0] == "nexthop")
    {
        error = ProcessPrioritizedNexthop(&aArgs[1]);
        ExitNow();
    }
    else if (aArgs[0] == "routecostthreshold")
    {
        error = ProcessPrioritizedRouteCostThreshold(&aArgs[1]);
        ExitNow();
    }
exit:
    return error;
}

otError PrioritizedRouting::ProcessPrioritizedNexthop(Arg aArgs[])
{
    constexpr uint8_t  kRouterIdOffset = 10; // Bit offset of Router ID in RLOC16
    constexpr uint16_t kInvalidRloc16  = 0xfffe;

    otError  error = OT_ERROR_NONE;
    uint16_t destRloc16;
    uint16_t nextHopRloc16;
    uint8_t  pathCost;

    if (aArgs[0].IsEmpty())
    {
        static const char *const kNextHopTableTitles[] = {
            "ID",
            "NxtHop",
            "Cost",
        };

        static const uint8_t kNextHopTableColumnWidths[] = {
            6,
            6,
            6,
        };

        OutputTableHeader(kNextHopTableTitles, kNextHopTableColumnWidths);

        for (uint8_t routerId = 0; routerId <= OT_NETWORK_MAX_ROUTER_ID; routerId++)
        {

            if (!otThreadIsRouterIdAllocated(GetInstancePtr(), routerId))
            {
                continue;
            }

            destRloc16 = routerId;
            destRloc16 <<= kRouterIdOffset;

            otThreadGetPrioritizedNextHopAndPathCost(GetInstancePtr(), destRloc16, &nextHopRloc16, &pathCost);

            OutputFormat("| %4u | ", routerId);

            if (nextHopRloc16 != kInvalidRloc16)
            {
                OutputLine("%4u | %4u |", nextHopRloc16 >> kRouterIdOffset, pathCost);
            }
            else
            {
                OutputLine("%4s | %4s |", "-", "-");
            }
        }
    }
    else
    {
        SuccessOrExit(error = aArgs[0].ParseAsUint16(destRloc16));
        otThreadGetPrioritizedNextHopAndPathCost(GetInstancePtr(), destRloc16, &nextHopRloc16, &pathCost);
        OutputLine("0x%04x cost:%u", nextHopRloc16, pathCost);
    }

exit:
    return error;
}

/**
 * @cli enable (get,set)
 * @code
 * prioritizedrouting enable
 * 1
 * Done
 * @endcode
 * @code
 * prioritizedrouting enable 1
 * Done
 * @endcode
 * @cparam prioritizedrouting enable [@ca{value}]
 * @par
 * Enable/Disable or Read the PRIORITIZED_ROUTING_ENABLED value.
 * @sa otThreadGetPrioritizedRoutingEnabled
 * @sa otThreadSetPrioritizedRoutingEnabled
 */
otError PrioritizedRouting::ProcessEnable(Arg aArgs[])
{
    otError error = OT_ERROR_NONE;
    bool    capable;

    if (aArgs[0].IsEmpty())
    {
        SuccessOrExit(error = otThreadGetPrioritizedRoutingEnabled(GetInstancePtr(), &capable));
        OutputLine("PrioritizedRoutingCapable=%d", capable ? 1 : 0);
    }
    else
    {
        uint8_t capableValue;

        SuccessOrExit(error = aArgs[0].ParseAsUint8(capableValue));
        SuccessOrExit(error = otThreadSetPrioritizedRoutingEnabled(GetInstancePtr(), capableValue != 0));
    }
exit:
    return error;
}

otError PrioritizedRouting::ProcessPrioritizedRouteCostThreshold(Arg aArgs[])
{
    otError error = OT_ERROR_NONE;
    
    if (aArgs[0].IsEmpty())
    {
        // Get current threshold
        OutputLine("%d", otThreadGetPrioritizedRouteCostThreshold(GetInstancePtr()));
    }
    else
    {
        // Set new threshold
        uint8_t threshold;
        SuccessOrExit(error = aArgs[0].ParseAsUint8(threshold));
        otThreadSetPrioritizedRouteCostThreshold(GetInstancePtr(), threshold);
    }
    
exit:
    return error;
}

otError PrioritizedRouting::ProcessRouteTable(Arg aArgs[])
{
    OT_UNUSED_VARIABLE(aArgs);
    constexpr uint8_t  kRouterIdOffset = 10; // Bit offset of Router ID in RLOC16
    constexpr uint16_t kInvalidRloc16  = 0xfffe;

    otError  error = OT_ERROR_NONE;
    uint16_t destRloc16;
    uint16_t nextHopRloc16;
    uint8_t  pathCost;

    static const char *const kRouteTableTitles[] = {
        "ID",
        "RLOC16",
        "Next Hop",
        "Path Cost",
    };

    static const uint8_t kRouteTableColumnWidths[] = {
        4,
        8,
        8,
        10,
    };

    OutputTableHeader(kRouteTableTitles, kRouteTableColumnWidths);

    for (uint8_t routerId = 0; routerId <= OT_NETWORK_MAX_ROUTER_ID; routerId++)
    {
        if (!otThreadIsRouterIdAllocated(GetInstancePtr(), routerId))
        {
            continue;
        }

        destRloc16 = routerId;
        destRloc16 <<= kRouterIdOffset;

        otThreadGetPrioritizedNextHopAndPathCost(GetInstancePtr(), destRloc16, &nextHopRloc16, &pathCost);

        OutputFormat("| %2u ", routerId);
        OutputFormat("| 0x%04x ", destRloc16);

        if (nextHopRloc16 != kInvalidRloc16)
        {
            OutputLine("| %8u | %8u |", nextHopRloc16 >> kRouterIdOffset, pathCost);
        }
        else
        {
            OutputLine("| %8s | %8s |", "-", "-");
        }
    }
    return error;
}

} // namespace Cli
} // namespace ot

#endif