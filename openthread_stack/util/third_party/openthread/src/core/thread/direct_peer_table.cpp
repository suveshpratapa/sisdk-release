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
 *   This file implements the Thread Direct peer table.
 */

#include "direct_peer_table.hpp"

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

#include "instance/instance.hpp"

namespace ot {

DirectPeerTable::Iterator::Iterator(Instance &aInstance, DirectPeer::StateFilter aFilter)
    : InstanceLocator(aInstance)
    , ItemPtrIterator(nullptr)
    , mFilter(aFilter)
{
    Reset();
}

void DirectPeerTable::Iterator::Reset(void)
{
    mItem = &Get<DirectPeerTable>().mPeers[0];

    if (!mItem->MatchesFilter(mFilter))
    {
        Advance();
    }
}

void DirectPeerTable::Iterator::Advance(void)
{
    VerifyOrExit(mItem != nullptr);

    do
    {
        mItem++;
        VerifyOrExit(mItem < &Get<DirectPeerTable>().mPeers[Get<DirectPeerTable>().kMaxPeers], mItem = nullptr);
    } while (!mItem->MatchesFilter(mFilter));

exit:
    return;
}

DirectPeerTable::DirectPeerTable(Instance &aInstance)
    : InstanceLocator(aInstance)
{
    for (DirectPeer &peer : mPeers)
    {
        peer.Init(aInstance);
        peer.Clear();
    }
}

void DirectPeerTable::Clear(void)
{
    for (DirectPeer &peer : mPeers)
    {
        peer.Clear();
    }
}

DirectPeer *DirectPeerTable::GetNewPeer(void)
{
    DirectPeer *peer = FindPeer(DirectPeer::AddressMatcher(DirectPeer::kInStateInvalid));

    VerifyOrExit(peer != nullptr);
    peer->Clear();

exit:
    return peer;
}

const DirectPeer *DirectPeerTable::FindPeer(const DirectPeer::AddressMatcher &aMatcher) const
{
    const DirectPeer *peer = mPeers;

    for (uint16_t num = kMaxPeers; num != 0; num--, peer++)
    {
        if (peer->Matches(aMatcher))
        {
            ExitNow();
        }
    }

    peer = nullptr;

exit:
    return peer;
}

DirectPeer *DirectPeerTable::FindPeer(const Mac::ExtAddress &aExtAddress, DirectPeer::StateFilter aFilter)
{
    return FindPeer(DirectPeer::AddressMatcher(aExtAddress, aFilter));
}

DirectPeer *DirectPeerTable::FindPeer(const Mac::Address &aMacAddress, DirectPeer::StateFilter aFilter)
{
    return FindPeer(DirectPeer::AddressMatcher(aMacAddress, aFilter));
}

Error DirectPeerTable::UpdateThreadDirectPeerSca(const Mac::ExtAddress &aExtAddress,
                                                 const Mac::ScaParams  &aSca,
                                                 uint64_t               aRxTimestamp)
{
    Error       error = kErrorNone;
    DirectPeer *peer;

    peer = FindPeer(aExtAddress, DirectPeer::kInStateValid);
    VerifyOrExit(peer != nullptr, error = kErrorNotFound);

    peer->UpdateSca(aSca, aRxTimestamp);

exit:
    return error;
}

Error DirectPeerTable::UpdateThreadDirectPeerSlwAccuracy(const Mac::ExtAddress  &aExtAddress,
                                                         const Mac::CslAccuracy &aAccuracy)
{
    Error       error = kErrorNone;
    DirectPeer *peer;

    peer = FindPeer(aExtAddress, DirectPeer::kInStateValid);
    VerifyOrExit(peer != nullptr, error = kErrorNotFound);

    peer->UpdateSlwAccuracy(aAccuracy);

exit:
    return error;
}

bool DirectPeerTable::GetWorstCaseThreadDirectPeerSlwAccuracy(Mac::CslAccuracy &aAccuracy) const
{
    bool hasPeerAccuracy = false;

    for (const DirectPeer &peer : mPeers)
    {
        const Mac::CslAccuracy &peerAccuracy = peer.GetSlwAccuracy();

        if (!peer.MatchesFilter(DirectPeer::kInStateValid) || !peer.HasSlwSchedule())
        {
            continue;
        }

        if (!hasPeerAccuracy)
        {
            aAccuracy       = peerAccuracy;
            hasPeerAccuracy = true;
            continue;
        }

        if (peerAccuracy.GetClockAccuracy() > aAccuracy.GetClockAccuracy())
        {
            aAccuracy.SetClockAccuracy(peerAccuracy.GetClockAccuracy());
        }

        if (peerAccuracy.GetUncertainty() > aAccuracy.GetUncertainty())
        {
            aAccuracy.SetUncertainty(peerAccuracy.GetUncertainty());
        }
    }

    return hasPeerAccuracy;
}

bool DirectPeerTable::HasPeers(DirectPeer::StateFilter aFilter) const
{
    return (FindPeer(DirectPeer::AddressMatcher(aFilter)) != nullptr);
}

uint16_t DirectPeerTable::GetPeerCount(DirectPeer::StateFilter aFilter) const
{
    uint16_t count = 0;

    for (DirectPeer &peer : AsNonConst(this)->Iterate(aFilter))
    {
        OT_UNUSED_VARIABLE(peer);
        count++;
    }

    return count;
}

bool DirectPeerTable::IsFull(void) const
{
    return FindPeer(DirectPeer::AddressMatcher(DirectPeer::kInStateInvalid)) == nullptr;
}

} // namespace ot

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
