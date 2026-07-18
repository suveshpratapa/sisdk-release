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

/**
 * @file
 *   This file implements the OpenThread Prioritized Routing API.
 */

#include "openthread-core-config.h"

#if PRIORITIZED_ROUTING_ENABLE

#include <openthread/prioritized_routing.h>

#include "common/as_core_type.hpp"
#include "instance/instance.hpp"
#include "common/code_utils.hpp"
#include "thread/router_table.hpp"

using namespace ot;

otError otThreadGetPrioritizedRoutingEnabled(otInstance *aInstance, bool *aCapable)
{
    Error error = kErrorNone;
    VerifyOrExit(aCapable != nullptr, error = kErrorInvalidArgs);
    *aCapable = AsCoreType(aInstance).Get<RouterTable>().IsPrioritizedRoutingEnabled();
    
exit:
    return error;
}

otError otThreadSetPrioritizedRoutingEnabled(otInstance *aInstance, bool aCapable)
{
    Error error = kErrorNone;
    AsCoreType(aInstance).Get<RouterTable>().SetPrioritizedRoutingEnabled(aCapable);
    return error;
}

uint8_t otThreadGetPrioritizedRouteCostThreshold(otInstance *aInstance)
{
    return AsCoreType(aInstance).Get<RouterTable>().GetPrioritizedRouteCostThreshold();
}

void otThreadSetPrioritizedRouteCostThreshold(otInstance *aInstance, uint8_t aThreshold)
{
    AsCoreType(aInstance).Get<RouterTable>().SetPrioritizedRouteCostThreshold(aThreshold);
}

void otThreadGetPrioritizedNextHopAndPathCost(otInstance *aInstance,
                                            uint16_t    aDestRloc16,
                                            uint16_t   *aNextHopRloc16,
                                            uint8_t    *aPathCost)
{
    uint16_t nextHopRloc16;
    uint8_t  pathCost;

    AsCoreType(aInstance).Get<RouterTable>().GetPrioritizedNextHopAndPathCost(aDestRloc16, nextHopRloc16, pathCost);

    if (aNextHopRloc16 != nullptr)
    {
        *aNextHopRloc16 = nextHopRloc16;
    }

    if (aPathCost != nullptr)
    {
        *aPathCost = pathCost;
    }
}

#endif // PRIORITIZED_ROUTING_ENABLE