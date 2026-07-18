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
 *   This file implements MLE prioritized routing router-specific functionality.
 */

#include "mle.hpp"

#include <openthread/platform/radio.h>
#include <openthread/platform/time.h>

#include "common/array.hpp"
#include "common/as_core_type.hpp"
#include "common/code_utils.hpp"
#include "common/debug.hpp"
#include "common/encoding.hpp"
#include "common/locator.hpp"
#include "common/num_utils.hpp"
#include "common/numeric_limits.hpp"
#include "common/random.hpp"
#include "common/serial_number.hpp"
#include "common/settings.hpp"
#include "instance/instance.hpp"
#include "meshcop/meshcop.hpp"
#include "meshcop/meshcop_tlvs.hpp"
#include "net/netif.hpp"
#include "net/udp6.hpp"
#include "thread/address_resolver.hpp"
#include "thread/key_manager.hpp"
#include "thread/link_metrics.hpp"
#include "thread/thread_netif.hpp"
#include "thread/time_sync_service.hpp"
#include "thread/version.hpp"

#if PRIORITIZED_ROUTING_ENABLE

using ot::BigEndian::HostSwap16;

namespace ot {
namespace Mle {

RegisterLogModule("Mle");

Error Mle::HandlePrioritizedAdvertisement(RxInfo &aRxInfo, uint16_t aSourceAddress, const LeaderData &aLeaderData)
{
    // This method processes a received MLE Prioritized Advertisement message on
    // an FTD device. It is called from `Mle::HandlePrioritizedAdvertisement()`
    // only when device is attached (in child, router, or leader roles)
    // and `IsFullThreadDevice()`.
    //
    // - `aSourceAddress` is the read value from `SourceAddressTlv`.
    // - `aLeaderData` is the read value from `LeaderDataTlv`.

    Error error = kErrorNone;

    uint8_t  linkMargin = Get<Mac::Mac>().ComputeLinkMargin(aRxInfo.mMessage.GetAverageRss());
    RouteTlv routeTlv;
    Router  *router;
    uint8_t  routerId;

    OT_UNUSED_VARIABLE(linkMargin);

    if (IsChild())
    {
        LogInfo("HandlePrioritizedAdvertisement: Cannot handle prioritized advertisement in REED state");
        ExitNow(error = kErrorDrop);
    }

    switch (aRxInfo.mMessage.ReadRouteTlv(routeTlv))
    {
    case kErrorNone:
        break;
    case kErrorNotFound:
        routeTlv.SetLength(0); // Mark that a Route TLV was not included.
        break;
    default:
        ExitNow(error = kErrorParse);
    }

    //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // Handle Partition ID mismatch (Do nothing here, to be determined based on regular advertisement instead of prioritized
    // advertisement
    if (aLeaderData.GetPartitionId() != mLeaderData.GetPartitionId())
    {
        LogNote("HandlePrioritizedAdvertisement: Different partition (peer:%lu, local:%lu), linkMargin:%d, "
                "partitionMergeLinkMargin:%d, routeTlvIsValid:%d, mPrevPartIdTimeout:%d, leaderDataPartId:%lu, "
                "prevPartId:%lu, routerIdSeq:%d, prevPartRouterIdSeq:%d, routeTlvIsSingleton:%d isSingleTon:%d",
                ToUlong(aLeaderData.GetPartitionId()), ToUlong(mLeaderData.GetPartitionId()), linkMargin,
                kPartitionMergeMinMargin, routeTlv.IsValid(), mPreviousPartitionIdTimeout, (unsigned long)aLeaderData.GetPartitionId(),
                (unsigned long)mPreviousPartitionId, routeTlv.GetRouterIdSequence(), mPreviousPartitionRouterIdSequence,
                routeTlv.IsSingleton(), IsSingleton());
        ExitNow(error = kErrorDrop);
    }

    //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // Handle Leader Router ID mismatch

    if (aLeaderData.GetLeaderRouterId() != GetLeaderId())
    {
        VerifyOrExit(aRxInfo.IsNeighborStateValid());

        if (!IsChild())
        {
            LogInfo("HandlePrioritizedAdvertisement: Leader ID mismatch leaderId=%d, leaderData.leaderRouterId=%d",
                    GetLeaderId(), aLeaderData.GetLeaderRouterId());
            error = kErrorDrop;
        }
        ExitNow();
    }

    VerifyOrExit(IsRouterRloc16(aSourceAddress) && routeTlv.IsValid());
    routerId = RouterIdFromRloc16(aSourceAddress);

    LogInfo("MLE Prioritized Advertisement received from router (0x%04x), aleaderDataVersion:[full:%d stable:%d] "
            "myLeaderDataVer:[full:%d, stable:%d] ,routeTlvIdSeqNum(%d), isRouteTlvIdSeqNumMoreRecent(%d) "
            "isRouteTlvIdSeqNumSame(%d)",
            Rloc16FromRouterId(routerId), aLeaderData.GetDataVersion(NetworkData::kFullSet),
            aLeaderData.GetDataVersion(NetworkData::kStableSubset), mLeaderData.GetDataVersion(NetworkData::kFullSet),
            mLeaderData.GetDataVersion(NetworkData::kStableSubset), routeTlv.GetRouterIdSequence(),
            mRouterTable.IsRouteTlvIdSequenceMoreRecent(routeTlv), mRouterTable.IsRouteTlvIdSequenceSame(routeTlv));

    //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // Process `RouteTlv` if the route sequence id is same as in router table. This assumes that the regular
    // advertisement is received first, if not
    if (aRxInfo.IsNeighborStateValid() && mRouterTable.IsRouteTlvIdSequenceSame(routeTlv))
    {
        SuccessOrExit(error = ProcessPrioritizedRouteTlv(routeTlv, aRxInfo));
    }

    router = mRouterTable.FindRouterById(routerId);
    VerifyOrExit(router != nullptr);

    router->SetLastHeard(TimerMilli::GetNow());
    router->SetDevicePrioritizedRoutingCapable(true);

    if (router->IsStateValid())
    {
        mRouterTable.UpdatePrioritizedRoutes(routeTlv, routerId);
    }
exit:
    return error;
}

void Mle::SendPrioritizedAdvertisement(void)
{
    Error        error = kErrorNone;
    Ip6::Address destination;
    TxMessage   *message = nullptr;

    // Suppress MLE Advertisements when trying to attach to a better
    // partition. Without this, a candidate parent might incorrectly
    // interpret this advertisement (Source Address TLV containing an
    // RLOC16 indicating device is acting as router) and reject the
    // attaching device.

    VerifyOrExit(!IsAttaching());

    // Send Prioritized Advertisement only in Router role
    VerifyOrExit(IsRouter() || IsLeader());

    VerifyOrExit(mRouterTable.IsPrioritizedRoutingEnabled());

    // Suppress MLE Advertisements when attempting to transition to
    // router role. Advertisements as a REED while attaching to a new
    // partition can cause existing children to detach
    // unnecessarily.

    VerifyOrExit(!mAddressSolicitPending);

    VerifyOrExit((message = NewMleMessage(kCommandPrioritizedAdvertisement)) != nullptr, error = kErrorNoBufs);
    SuccessOrExit(error = message->AppendSourceAddressTlv());
    SuccessOrExit(error = message->AppendLeaderDataTlv());

    switch (mRole)
    {
    case kRoleChild:
        break;

    case kRoleRouter:
    case kRoleLeader:
        SuccessOrExit(error = message->AppendPrioritizedRouteTlv());
        break;

    case kRoleDisabled:
    case kRoleDetached:
        OT_ASSERT(false);
    }

    destination.SetToLinkLocalAllNodesMulticast();
    SuccessOrExit(error = message->SendTo(destination));

    Log(kMessageSend, kTypePrioritizedAdvertisement, destination);

exit:
    FreeMessageOnError(message, error);
    LogSendError(kTypePrioritizedAdvertisement, error);
}

Error Mle::ProcessPrioritizedRouteTlv(const RouteTlv &aRouteTlv, RxInfo &aRxInfo)
{
    Error    error          = kErrorNone;
    uint16_t neighborRloc16 = kInvalidRloc16;

    if ((aRxInfo.mNeighbor != nullptr) && Get<RouterTable>().Contains(*aRxInfo.mNeighbor))
    {
        neighborRloc16 = aRxInfo.mNeighbor->GetRloc16();
    }

    if (mRouterTable.GetRouterIdSequence() != aRouteTlv.GetRouterIdSequence())
    {
        LogWarn("ProcessPrioritizedRouteTlv: Received router ID: %d does not match router table entry:%d",
                aRouteTlv.GetRouterIdSequence(), mRouterTable.GetRouterIdSequence());
        error = kErrorDrop;
        return error;
    }

    if (IsRouter() && !mRouterTable.IsAllocated(mRouterId))
    {
        LogWarn("ProcessPrioritizedRouteTlv: Error while processing Route TLV - Router ID: %d", mRouterId);
        error = kErrorNoRoute;
    }

    mRouterTable.UpdatePrioritizedRouterIdSet(aRouteTlv.GetRouterIdSequence(), aRouteTlv.GetRouterIdMask());

    if (neighborRloc16 != kInvalidRloc16)
    {
        aRxInfo.mNeighbor = Get<NeighborTable>().FindNeighbor(neighborRloc16);
    }
    return error;
}
} // namespace Mle
} // namespace ot

#endif