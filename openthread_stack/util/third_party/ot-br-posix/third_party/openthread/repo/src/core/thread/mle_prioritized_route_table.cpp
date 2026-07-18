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
 *   This file implements MLE prioritized route table functionality.
 */

#include "router_table.hpp"
#include "common/code_utils.hpp"
#include "common/locator.hpp"
#include "common/log.hpp"
#include "common/timer.hpp"
#include "instance/instance.hpp"
#include "thread/mle.hpp"
#include "thread/network_data_leader.hpp"
#include "thread/thread_netif.hpp"
#include "thread/prioritized_routing_defs.hpp"

#if PRIORITIZED_ROUTING_ENABLE
namespace ot {

RegisterLogModule("RouterTable");

bool RouterTable::IsPrioritizedRouter(const Router &router) const
{
    // Return false if prioritized routing is not enabled
    if (!mIsPrioritizedRoutingEnabled)
    {
        return false;
    }

    // If it is current device, return true as it is prioritized router
    if (Get<Mle::Mle>().HasRloc16(router.GetRloc16()))
    {
        return true;
    }

    // If it is neighbor device, check if prioritized routing capable
    if (router.IsDevicePrioritizedRoutingCapable())
    {
        return true;
    }

    return false;
}

#if OT_SHOULD_LOG_AT(OT_LOG_LEVEL_INFO)
void RouterTable::LogPrioritizedRouteTable(void) const
{
    static constexpr uint16_t kStringSize = 128;

    VerifyOrExit(IsPrioritizedRoutingEnabled());

    LogInfo("Prioritized Route table");

    for (const Router &router : mRouters)
    {
        if (!IsPrioritizedRouter(router))
        {
            continue;
        }
        String<kStringSize> string;

        string.Append("    %2d 0x%04x", router.GetRouterId(), router.GetRloc16());

        if (Get<Mle::Mle>().HasRloc16(router.GetRloc16()))
        {
            string.Append(" - me");
        }
        else if (Get<Mle::Mle>().IsChild() && (router.GetRloc16() == Get<Mle::Mle>().GetParent().GetRloc16()))
        {
            string.Append(" - parent");
        }
        else
        {
            if (router.IsStateValid())
            {
                string.Append(" - nbr{lq[i/o]:%d/%d cost:%d}", router.GetLinkQualityIn(), router.GetLinkQualityOut(),
                              GetLinkCost(router));
            }

            if (router.GetPrioritizedNextHop() != Mle::kInvalidRouterId)
            {
                string.Append(" - nexthop{%d cost:%d}", router.GetPrioritizedNextHop(), router.GetPrioritizedCost());
            }
        }

        if (router.GetRouterId() == Get<Mle::Mle>().GetLeaderId())
        {
            string.Append(" - leader");
        }

        LogInfo("%s", string.AsCString());
    }
    LogInfo("Prioritized Route table dump end");
exit:
    return;
}
#endif

uint16_t RouterTable::GetPrioritizedNextHop(uint16_t aDestRloc16) const
{
    uint8_t  pathCost;
    uint16_t nextHopRloc16;

    GetPrioritizedNextHopAndPathCost(aDestRloc16, nextHopRloc16, pathCost);

    return nextHopRloc16;
}

uint8_t RouterTable::GetPrioritizedPathCost(uint16_t aDestRloc16) const
{
    uint8_t  pathCost;
    uint16_t nextHopRloc16;

    GetPrioritizedNextHopAndPathCost(aDestRloc16, nextHopRloc16, pathCost);

    return pathCost;
}

uint8_t RouterTable::GetPrioritizedPathCostToLeader(void) const
{
    return GetPrioritizedPathCost(Get<Mle::Mle>().GetLeaderRloc16());
}

const Router *RouterTable::FindPrioritizedNextHopOf(const Router &aRouter) const
{
    return FindRouterById(aRouter.GetPrioritizedNextHop());
}

void RouterTable::GetPrioritizedNextHopAndPathCost(uint16_t aDestRloc16, uint16_t &aNextHopRloc16, uint8_t &aPathCost) const
{
    const Router *router;
    const Router *nextHop;

    aPathCost      = Mle::kMaxRouteCost;
    aNextHopRloc16 = Mle::kInvalidRloc16;

    VerifyOrExit(Get<Mle::Mle>().IsAttached());

    if (Get<Mle::Mle>().HasRloc16(aDestRloc16))
    {
        // Destination is this device, return cost as zero if the current device is prioritized routing enabled
        if (mIsPrioritizedRoutingEnabled)
        {
            aPathCost      = 0;
            aNextHopRloc16 = aDestRloc16;
        }
        ExitNow();
    }
    VerifyOrExit(Get<Mle::Mle>().IsRouter() || Get<Mle::Mle>().IsLeader());

    router = FindRouterById(Mle::RouterIdFromRloc16(aDestRloc16));

    VerifyOrExit((router != nullptr) && IsPrioritizedRouter(*router));

    nextHop = (router != nullptr) ? FindPrioritizedNextHopOf(*router) : nullptr;

    if (Get<Mle::Mle>().HasMatchingRouterIdWith(aDestRloc16))
    {
        // Destination is a one of our children.

        const Child *child = Get<ChildTable>().FindChild(aDestRloc16, Child::kInStateAnyExceptInvalid);

        VerifyOrExit(child != nullptr);
        aNextHopRloc16 = aDestRloc16;
        aPathCost      = CostForLinkQuality(child->GetLinkQualityIn());

        ExitNow();
    }

    VerifyOrExit(router != nullptr);

    aPathCost = GetLinkCost(*router);

    if (aPathCost < Mle::kMaxRouteCost)
    {
        aNextHopRloc16 = router->GetRloc16();
    }

    if (nextHop != nullptr)
    {
        // Determine whether direct link or forwarding hop link
        // through `nextHop` has a lower path cost.

        uint8_t nextHopPathCost = router->GetPrioritizedCost() + GetLinkCost(*nextHop);

        if (nextHopPathCost < aPathCost)
        {
            aPathCost      = nextHopPathCost;
            aNextHopRloc16 = nextHop->GetRloc16();
        }
    }

    if (Mle::IsChildRloc16(aDestRloc16))
    {
        // Destination is a child. we assume best link quality
        // between destination and its parent router.

        aPathCost += kCostForLinkQuality3;
    }

exit:
    if (aNextHopRloc16 != Mle::kInvalidRloc16)
    {
        if (aPathCost > mPrioritizedRouteCostThreshold)
        {
            LogWarn("GetPrioritizedNextHopAndPathCost: Path Cost:%d to RLOC16:0x%x exceeds threshold:%d", aPathCost,
                    aNextHopRloc16, mPrioritizedRouteCostThreshold);
            aNextHopRloc16 = Mle::kInvalidRloc16;
            aPathCost      = Mle::kMaxRouteCost;
        }
    }
}

void RouterTable::UpdatePrioritizedRoutes(const Mle::RouteTlv &aRouteTlv, uint8_t aNeighborId)
{
    Router          *neighbor;
    Mle::RouterIdSet finitePathCostIdSet;
    uint8_t          linkCostToNeighbor;

    neighbor = FindRouterById(aNeighborId);
    VerifyOrExit(neighbor != nullptr);
    // Make sure neighbor is a prioritized routing capable
    VerifyOrExit(neighbor->IsDevicePrioritizedRoutingCapable());

    // Before updating the routes, we track which routers have finite
    // path cost. After the update we check again to see if any path
    // cost changed from finite to infinite or vice versa to decide
    // whether to reset the MLE Advertisement interval.

    finitePathCostIdSet.Clear();

    for (uint8_t routerId = 0; routerId <= Mle::kMaxRouterId; routerId++)
    {
        if (GetPrioritizedPathCost(Mle::Rloc16FromRouterId(routerId)) < Mle::kMaxRouteCost)
        {
            finitePathCostIdSet.Add(routerId);
        }
    }

    linkCostToNeighbor = GetLinkCost(*neighbor);

    for (uint8_t routerId = 0, index = 0; routerId <= Mle::kMaxRouterId;
         index += aRouteTlv.IsRouterIdSet(routerId) ? 1 : 0, routerId++)
    {
        Router *router;
        Router *nextHop;
        uint8_t cost;

        if (!aRouteTlv.IsRouterIdSet(routerId))
        {
            continue;
        }

        router = FindRouterById(routerId);

        if (router == nullptr || Get<Mle::Mle>().HasRloc16(router->GetRloc16()) || router == neighbor)
        {
            continue;
        }

        nextHop = FindPrioritizedNextHopOf(*router);

        cost = aRouteTlv.GetRouteCost(index);
        cost = (cost == 0) ? Mle::kMaxRouteCost : cost;

        if ((nextHop == nullptr) || (nextHop == neighbor))
        {
            // `router` has no next hop or next hop is neighbor (sender)

            if (cost + linkCostToNeighbor < Mle::kMaxRouteCost)
            {
                if (router->SetPrioritizedNextHopAndCost(aNeighborId, cost))
                {
                    SignalTableChanged();
                }
            }
            else if (nextHop == neighbor)
            {
                router->SetPrioritizedNextHopToInvalid();
                router->SetLastHeard(TimerMilli::GetNow());
                SignalTableChanged();
            }
        }
        else
        {
            uint8_t curCost = router->GetPrioritizedCost() + GetLinkCost(*nextHop);
            uint8_t newCost = cost + linkCostToNeighbor;

            if (newCost < curCost)
            {
                router->SetPrioritizedNextHopAndCost(aNeighborId, cost);
                SignalTableChanged();
            }
        }
    }

exit:
    return;
}

void RouterTable::UpdatePrioritizedRouterIdSet(uint8_t aRouterIdSequence, const Mle::RouterIdSet &aRouterIdSet)
{
    VerifyOrExit(aRouterIdSequence == GetRouterIdSequence());
    for (uint8_t routerId = 0; routerId <= Mle::kMaxRouterId; routerId++)
    {
        if (IsAllocated(routerId) && aRouterIdSet.Contains(routerId))
        {
            Router *router = FindRouterById(routerId);

            OT_ASSERT(router != nullptr);
            // TODO: log can be removed later
            LogDebg("UpdatePrioritizedRouterIdSet: routerIndex:%d routerRloc16:%x lqIn:%d lqOut:%d numAllocatedIds:%d",
                    routerId, router->GetRloc16(), router->GetLinkQualityIn(), router->GetLinkQualityOut(),
                    aRouterIdSet.GetNumberOfAllocatedIds());
            router->SetDevicePrioritizedRoutingCapable(true);
        }
    }
exit:
    return;
}

bool RouterTable::IsRouteTlvIdSequenceSame(const Mle::RouteTlv &aRouteTlv) const
{
    return (aRouteTlv.GetRouterIdSequence() == GetRouterIdSequence());
}

void RouterTable::FillPrioritizedRouteTlv(Mle::RouteTlv &aRouteTlv, const Neighbor *aNeighbor) const
{
    uint8_t          routerIdSequence = mRouterIdSequence;
    Mle::RouterIdSet routerIdSet;
    uint8_t          routerIndex;
    OT_UNUSED_VARIABLE(aNeighbor);

    GetAsPrioritizedRouterIdSet(routerIdSet);

    aRouteTlv.SetRouterIdSequence(routerIdSequence);
    aRouteTlv.SetRouterIdMask(routerIdSet);

    routerIndex = 0;

    for (uint8_t routerId = 0; routerId <= Mle::kMaxRouterId; routerId++)
    {
        uint16_t routerRloc16;

        if (!routerIdSet.Contains(routerId))
        {
            continue;
        }

        routerRloc16 = Mle::Rloc16FromRouterId(routerId);

        if (Get<Mle::Mle>().HasRloc16(routerRloc16))
        {
            aRouteTlv.SetRouteData(routerIndex, kLinkQuality0, kLinkQuality0, 1);
        }
        else
        {
            const Router *router = FindRouterById(routerId);
            uint8_t       pathCost;

            OT_ASSERT(router != nullptr);

            pathCost = GetPrioritizedPathCost(routerRloc16);

            if (pathCost >= Mle::kMaxRouteCost)
            {
                pathCost = 0;
            }

            LogInfo("FillPrioritizedRouteTlv: routerIndex:%d routerRloc16:%x lqIn:0 lqOut:0 pathCost:%d", routerIndex,
                    router->GetRloc16(), pathCost);

            // Set link qualities to 0,
            aRouteTlv.SetRouteData(routerIndex, kLinkQuality0, kLinkQuality0, pathCost);
        }
        routerIndex++;
    }
    aRouteTlv.SetRouteDataLength(routerIndex);
}

void RouterTable::GetAsPrioritizedRouterIdSet(Mle::RouterIdSet &aRouterIdSet) const
{
    aRouterIdSet.Clear();

    for (uint8_t routerId = 0; routerId <= Mle::kMaxRouterId; routerId++)
    {
        if (mRouterIdMap.IsAllocated(routerId))
        {
            const Router *router = FindRouterById(routerId);
            if (router)
            {
                if ((IsPrioritizedRouter(*router)))
                {
                    aRouterIdSet.Add(routerId);
                }
            }
        }
    }
}

bool RouterTable::IsPrioritizedRoutingEnabled(void) const { return mIsPrioritizedRoutingEnabled; }

} // namespace ot
#endif // PRIORITIZED_ROUTING_ENABLE