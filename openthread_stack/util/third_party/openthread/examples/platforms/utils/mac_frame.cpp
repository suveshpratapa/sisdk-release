/*
 *  Copyright (c) 2019, The OpenThread Authors.
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

#include "mac_frame.h"

#include <assert.h>

#include <openthread/platform/radio.h>

#include "common/code_utils.hpp"
#include "common/encoding.hpp"
#include "common/frame_builder.hpp"
#include "common/ltvs.hpp"
#include "mac/mac_frame.hpp"
#include "mac/mac_header_ltv.hpp"

using namespace ot;

bool otMacFrameDoesAddrMatch(const otRadioFrame *aFrame,
                             otPanId             aPanId,
                             otShortAddress      aShortAddress,
                             const otExtAddress *aExtAddress)
{
    return otMacFrameDoesAddrMatchAny(aFrame, aPanId, aShortAddress, Mac::kShortAddrInvalid, aExtAddress);
}

bool otMacFrameDoesAddrMatchAny(const otRadioFrame *aFrame,
                                otPanId             aPanId,
                                otShortAddress      aShortAddress,
                                otShortAddress      aAltShortAddress,
                                const otExtAddress *aExtAddress)
{
    const Mac::Frame &frame = *static_cast<const Mac::Frame *>(aFrame);
    bool              rval  = true;
    Mac::Address      dst;
    Mac::PanId        panid;

    VerifyOrExit(frame.GetDstAddr(dst) == kErrorNone, rval = false);

    switch (dst.GetType())
    {
    case Mac::Address::kTypeShort:
        VerifyOrExit(dst.GetShort() == Mac::kShortAddrBroadcast || dst.GetShort() == aShortAddress ||
                         (aAltShortAddress != Mac::kShortAddrInvalid && dst.GetShort() == aAltShortAddress),
                     rval = false);
        break;

    case Mac::Address::kTypeExtended:
        VerifyOrExit(dst.GetExtended() == *static_cast<const Mac::ExtAddress *>(aExtAddress), rval = false);
        break;

    case Mac::Address::kTypeNone:
        break;
    }

    SuccessOrExit(frame.GetDstPanId(panid));
    VerifyOrExit(panid == Mac::kPanIdBroadcast || panid == aPanId, rval = false);

exit:
    return rval;
}

bool otMacFrameIsAck(const otRadioFrame *aFrame)
{
    return static_cast<const Mac::Frame *>(aFrame)->GetType() == Mac::Frame::kTypeAck;
}

bool otMacFrameIsData(const otRadioFrame *aFrame)
{
    return static_cast<const Mac::Frame *>(aFrame)->GetType() == Mac::Frame::kTypeData;
}

bool otMacFrameIsCommand(const otRadioFrame *aFrame)
{
    return static_cast<const Mac::Frame *>(aFrame)->GetType() == Mac::Frame::kTypeMacCmd;
}

bool otMacFrameIsDataRequest(const otRadioFrame *aFrame)
{
    return static_cast<const Mac::Frame *>(aFrame)->IsDataRequestCommand();
}

bool otMacFrameIsAckRequested(const otRadioFrame *aFrame)
{
    return static_cast<const Mac::Frame *>(aFrame)->GetAckRequest();
}

static void GetOtMacAddress(const Mac::Address &aInAddress, otMacAddress *aOutAddress)
{
    switch (aInAddress.GetType())
    {
    case Mac::Address::kTypeNone:
        aOutAddress->mType = OT_MAC_ADDRESS_TYPE_NONE;
        break;

    case Mac::Address::kTypeShort:
        aOutAddress->mType                  = OT_MAC_ADDRESS_TYPE_SHORT;
        aOutAddress->mAddress.mShortAddress = aInAddress.GetShort();
        break;

    case Mac::Address::kTypeExtended:
        aOutAddress->mType                = OT_MAC_ADDRESS_TYPE_EXTENDED;
        aOutAddress->mAddress.mExtAddress = aInAddress.GetExtended();
        break;
    }
}

otError otMacFrameGetSrcAddr(const otRadioFrame *aFrame, otMacAddress *aMacAddress)
{
    otError      error;
    Mac::Address address;

    error = static_cast<const Mac::Frame *>(aFrame)->GetSrcAddr(address);
    SuccessOrExit(error);

    GetOtMacAddress(address, aMacAddress);

exit:
    return error;
}

otError otMacFrameGetDstAddr(const otRadioFrame *aFrame, otMacAddress *aMacAddress)
{
    otError      error;
    Mac::Address address;

    error = static_cast<const Mac::Frame *>(aFrame)->GetDstAddr(address);
    SuccessOrExit(error);

    GetOtMacAddress(address, aMacAddress);

exit:
    return error;
}

otError otMacFrameGetSequence(const otRadioFrame *aFrame, uint8_t *aSequence)
{
    otError error;

    if (static_cast<const Mac::Frame *>(aFrame)->IsSequencePresent())
    {
        *aSequence = static_cast<const Mac::Frame *>(aFrame)->GetSequence();
        error      = kErrorNone;
    }
    else
    {
        error = kErrorParse;
    }

    return error;
}

void otMacFrameProcessTransmitAesCcm(otRadioFrame *aFrame, const otExtAddress *aExtAddress)
{
    static_cast<Mac::TxFrame *>(aFrame)->ProcessTransmitAesCcm(*static_cast<const Mac::ExtAddress *>(aExtAddress));
}

bool otMacFrameIsVersion2015(const otRadioFrame *aFrame)
{
    return static_cast<const Mac::Frame *>(aFrame)->IsVersion2015();
}

void otMacFrameGenerateImmAck(const otRadioFrame *aFrame, bool aIsFramePending, otRadioFrame *aAckFrame)
{
    assert(aFrame != nullptr && aAckFrame != nullptr);

    static_cast<Mac::TxFrame *>(aAckFrame)->GenerateImmAck(*static_cast<const Mac::RxFrame *>(aFrame), aIsFramePending);
}

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
otError otMacFrameGenerateEnhAck(const otRadioFrame *aFrame,
                                 bool                aIsFramePending,
                                 const uint8_t      *aIeData,
                                 uint8_t             aIeLength,
                                 otRadioFrame       *aAckFrame)
{
    assert(aFrame != nullptr && aAckFrame != nullptr);

    return static_cast<Mac::TxFrame *>(aAckFrame)->GenerateEnhAck(*static_cast<const Mac::RxFrame *>(aFrame),
                                                                  aIsFramePending, aIeData, aIeLength);
}
#endif

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
void otMacFrameSetCslIe(otRadioFrame *aFrame, uint16_t aCslPeriod, uint16_t aCslPhase)
{
    static_cast<Mac::Frame *>(aFrame)->SetCslIe(aCslPeriod, aCslPhase);
}
#endif // OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE

bool otMacFrameIsSecurityEnabled(otRadioFrame *aFrame)
{
    return static_cast<const Mac::Frame *>(aFrame)->GetSecurityEnabled();
}

bool otMacFrameIsKeyIdMode1(otRadioFrame *aFrame)
{
    uint8_t keyIdMode;
    otError error;

    error = static_cast<const Mac::Frame *>(aFrame)->GetKeyIdMode(keyIdMode);

    return (error == OT_ERROR_NONE) ? (keyIdMode == Mac::Frame::kKeyIdMode1) : false;
}

bool otMacFrameIsKeyIdMode2(otRadioFrame *aFrame)
{
    uint8_t keyIdMode;
    otError error;

    error = static_cast<const Mac::Frame *>(aFrame)->GetKeyIdMode(keyIdMode);

    return (error == OT_ERROR_NONE) ? (keyIdMode == Mac::Frame::kKeyIdMode2) : false;
}

uint8_t otMacFrameGetKeyId(otRadioFrame *aFrame)
{
    uint8_t keyId = 0;

    IgnoreError(static_cast<const Mac::Frame *>(aFrame)->GetKeyId(keyId));

    return keyId;
}

void otMacFrameSetKeyId(otRadioFrame *aFrame, uint8_t aKeyId) { static_cast<Mac::Frame *>(aFrame)->SetKeyId(aKeyId); }

uint32_t otMacFrameGetFrameCounter(otRadioFrame *aFrame)
{
    uint32_t frameCounter = UINT32_MAX;

    IgnoreError(static_cast<Mac::Frame *>(aFrame)->GetFrameCounter(frameCounter));

    return frameCounter;
}

void otMacFrameSetFrameCounter(otRadioFrame *aFrame, uint32_t aFrameCounter)
{
    static_cast<Mac::Frame *>(aFrame)->SetFrameCounter(aFrameCounter);
}

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
uint8_t otMacFrameGenerateCslIeTemplate(uint8_t *aDest)
{
    assert(aDest != nullptr);

    reinterpret_cast<Mac::HeaderIe *>(aDest)->SetId(Mac::CslIe::kHeaderIeId);
    reinterpret_cast<Mac::HeaderIe *>(aDest)->SetLength(sizeof(Mac::CslIe));

    return sizeof(Mac::HeaderIe) + sizeof(Mac::CslIe);
}
#endif

#if OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE
uint8_t otMacFrameGenerateEnhAckProbingIe(uint8_t *aDest, const uint8_t *aIeData, uint8_t aIeDataLength)
{
    uint8_t len = sizeof(Mac::VendorIeHeader) + aIeDataLength;

    assert(aDest != nullptr);

    reinterpret_cast<Mac::HeaderIe *>(aDest)->SetId(Mac::ThreadIe::kHeaderIeId);
    reinterpret_cast<Mac::HeaderIe *>(aDest)->SetLength(len);

    aDest += sizeof(Mac::HeaderIe);

    reinterpret_cast<Mac::VendorIeHeader *>(aDest)->SetVendorOui(Mac::ThreadIe::kVendorOuiThreadCompanyId);
    reinterpret_cast<Mac::VendorIeHeader *>(aDest)->SetSubType(Mac::ThreadIe::kEnhAckProbingIe);

    if (aIeData != nullptr)
    {
        aDest += sizeof(Mac::VendorIeHeader);
        memcpy(aDest, aIeData, aIeDataLength);
    }

    return sizeof(Mac::HeaderIe) + len;
}

void otMacFrameSetEnhAckProbingIe(otRadioFrame *aFrame, const uint8_t *aData, uint8_t aDataLen)
{
    assert(aFrame != nullptr && aData != nullptr);

    reinterpret_cast<Mac::Frame *>(aFrame)->SetEnhAckProbingIe(aData, aDataLen);
}
#endif // OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
static uint16_t ComputeCslPhase(uint32_t aRadioTime, otRadioContext *aRadioContext)
{
    return (aRadioContext->mCslSampleTime - aRadioTime) % (aRadioContext->mCslPeriod * OT_US_PER_TEN_SYMBOLS) /
           OT_US_PER_TEN_SYMBOLS;
}
#endif

#if (OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE) && \
    (OPENTHREAD_FTD || OPENTHREAD_MTD)

constexpr uint32_t kDefaultSlwSlotDurationUs = 625u;

static uint16_t ComputeSlwPhase(uint32_t aRadioTime, const otRadioContext *aRadioContext)
{
    // SLW start time = frame TX time + RAM offset + SLW phase (all in Slot Duration units).
    // Adjust the reference by subtracting the RAM offset before computing the phase.
    uint32_t slotDurationUs =
        (aRadioContext->mSlwSlotDurationUs != 0) ? aRadioContext->mSlwSlotDurationUs : kDefaultSlwSlotDurationUs;
    uint32_t adjustedUs =
        aRadioContext->mSlwSampleTime - aRadioTime - static_cast<uint32_t>(aRadioContext->mRamOffsetUs);

    return static_cast<uint16_t>(adjustedUs % (static_cast<uint32_t>(aRadioContext->mSlwPeriod) * slotDurationUs) /
                                 slotDurationUs);
}

void otMacFrameSetThreadDirectScaLtv(otRadioFrame *aFrame,
                                     uint16_t      aSlwPeriod,
                                     uint16_t      aSlwPhase,
                                     int16_t       aRamOffsetUs)
{
    using namespace ot;
    using namespace ot::Mac;
    using namespace ot::LittleEndian;

    enum : uint16_t
    {
        kScaRamOffsetShift    = 2,
        kScaRamOffsetMask     = 0x07FFu,
        kScaRamAvailableShift = 13,
    };

    assert(aFrame != nullptr);
    assert(aRamOffsetUs >= ScaParams::kRamOffsetUsMin && aRamOffsetUs <= ScaParams::kRamOffsetUsMax);

    Frame   &frame    = *static_cast<Frame *>(aFrame);
    uint8_t *threadIe = frame.GetHeaderIe(ThreadHeaderIe::kElementId);

    if (threadIe == nullptr)
    {
        return;
    }

    uint8_t  ieLen   = reinterpret_cast<const HeaderIe *>(threadIe)->GetLength();
    uint8_t *content = threadIe + sizeof(HeaderIe);

    // Scan the packed LTV stream to find the SCA LTV and write RAM offset field
    // plus the SLW period and phase assuming RAM available bit is false.
    PackedLtvStream::Iterator iter;
    iter.Init(content, ieLen);

    while (!iter.IsDone())
    {
        // SCA LTV payload: [2B fixed-hdr][2-12 bits RAM offset][2B SLW period][2B SLW phase]
        // Consider the RAM available field to be 0 for phase-1 to compute period and phase offsets.
        if (iter.GetType() == ThreadHeaderIe::kTypeSca && iter.GetLength() >= 6)
        {
            uint8_t *value      = content + iter.GetValueOffset();
            uint16_t fixedHdr   = ReadUint16(value);
            bool     hasRamBits = ((fixedHdr >> kScaRamAvailableShift) & 0x01u) != 0;

            fixedHdr = static_cast<uint16_t>(
                (fixedHdr & ~(kScaRamOffsetMask << kScaRamOffsetShift)) |
                ((static_cast<uint16_t>(aRamOffsetUs) & kScaRamOffsetMask) << kScaRamOffsetShift));
            WriteUint16(fixedHdr, value);

            // TODO: Handle the RAM-available layout when implementation starts
            // advertising RAM Duration and RAM Bits in the SCA payload to calculate
            // the period and phase offsets. OR
            // Remove this function and modify otMacFrameSetScaLtvPhase to take period and ramoffset.
            if (hasRamBits)
            {
                assert(false); // RAM available is not supported in phase-1.
            }

            WriteUint16(aSlwPeriod, value + 2U);
            WriteUint16(aSlwPhase, value + 4u);
            return;
        }

        IgnoreError(iter.Advance());
    }
}

bool otMacFrameHasThreadDirectScaLtv(const otRadioFrame *aFrame)
{
    using namespace ot;
    using namespace ot::Mac;

    assert(aFrame != nullptr);

    const Frame   &frame    = *static_cast<const Frame *>(aFrame);
    const uint8_t *threadIe = frame.GetHeaderIe(ThreadHeaderIe::kElementId);
    bool           found    = false;

    if (threadIe == nullptr)
    {
        return false;
    }

    uint8_t ieLen = reinterpret_cast<const HeaderIe *>(threadIe)->GetLength();

    PackedLtvStream::Iterator iter;
    iter.Init(threadIe + sizeof(HeaderIe), ieLen);

    while (!iter.IsDone())
    {
        if (iter.GetType() == ThreadHeaderIe::kTypeSca && iter.GetLength() >= 6)
        {
            found = true;
            break;
        }

        IgnoreError(iter.Advance());
    }

    return found;
}
#endif // (OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || WAKE_LISTENER_ENABLE) && (OPENTHREAD_FTD ||
       // OPENTHREAD_MTD)

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
bool otMacFrameCalculateSlwPhaseAndRamOffset(uint32_t  aNextSampleTimeUs,
                                             uint32_t  aMacHeaderTxTime,
                                             uint16_t  aPeriodSlots,
                                             uint32_t  aSlotDurationUs,
                                             uint16_t *aPhaseSlots,
                                             int16_t  *aRamOffsetUs)
{
    bool     success = false;
    uint32_t periodUs;
    uint32_t deltaUs;
    uint32_t phaseSlots;
    int32_t  ramOffsetUs;

    assert(aPhaseSlots != nullptr && aRamOffsetUs != nullptr);

    VerifyOrExit((aPeriodSlots != 0) && (aSlotDurationUs != 0));

    // Convert the advertised period slots into absolute time.
    periodUs = static_cast<uint32_t>(aPeriodSlots) * aSlotDurationUs;

    // Like CSL, compare both timestamps within one SLW period and get the
    // forward delta from the frame anchor to the next sample point.
    deltaUs = ((aNextSampleTimeUs % periodUs) - (aMacHeaderTxTime % periodUs) + periodUs) % periodUs;

    // Round the forward delta to the nearest slot index.
    phaseSlots = (deltaUs + (aSlotDurationUs / 2)) / aSlotDurationUs;

    // Use the remaining time delta as the signed RAM offset.
    ramOffsetUs = static_cast<int32_t>(deltaUs) - static_cast<int32_t>(phaseSlots * aSlotDurationUs);

    VerifyOrExit(phaseSlots <= aPeriodSlots);
    VerifyOrExit((ramOffsetUs >= -1024) && (ramOffsetUs <= 1023));

    *aPhaseSlots  = static_cast<uint16_t>(phaseSlots);
    *aRamOffsetUs = static_cast<int16_t>(ramOffsetUs);
    success       = true;

exit:
    return success;
}
#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE

otError otMacFrameProcessTransmitSecurity(otRadioFrame *aFrame, otRadioContext *aRadioContext)
{
    otError error = OT_ERROR_NONE;
#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    otMacKeyMaterial *key = nullptr;
    uint8_t           keyId;
    uint32_t          frameCounter;
    bool              processKeyId;

    processKeyId = otMacFrameIsKeyIdMode1(aFrame);

    VerifyOrExit(otMacFrameIsSecurityEnabled(aFrame) && processKeyId && !aFrame->mInfo.mTxInfo.mIsSecurityProcessed);

    if (otMacFrameIsAck(aFrame))
    {
        keyId = otMacFrameGetKeyId(aFrame);

        VerifyOrExit(keyId != 0, error = OT_ERROR_FAILED);

        if (keyId == aRadioContext->mKeyId)
        {
            key          = &aRadioContext->mCurrKey;
            frameCounter = aRadioContext->mMacFrameCounter++;
        }
        else if (keyId == aRadioContext->mKeyId - 1)
        {
            key          = &aRadioContext->mPrevKey;
            frameCounter = aRadioContext->mPrevMacFrameCounter++;
        }
        else if (keyId == aRadioContext->mKeyId + 1)
        {
            key          = &aRadioContext->mNextKey;
            frameCounter = 0;
        }
        else
        {
            ExitNow(error = OT_ERROR_SECURITY);
        }
    }
    else if (!aFrame->mInfo.mTxInfo.mIsHeaderUpdated)
    {
        key          = &aRadioContext->mCurrKey;
        keyId        = aRadioContext->mKeyId;
        frameCounter = aRadioContext->mMacFrameCounter++;
    }

    if (key != nullptr)
    {
        aFrame->mInfo.mTxInfo.mAesKey = key;

        otMacFrameSetKeyId(aFrame, keyId);
        otMacFrameSetFrameCounter(aFrame, frameCounter);
        aFrame->mInfo.mTxInfo.mIsHeaderUpdated = true;
    }
#else
    VerifyOrExit(!aFrame->mInfo.mTxInfo.mIsSecurityProcessed);
#endif // OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2

    otMacFrameProcessTransmitAesCcm(aFrame, &aRadioContext->mExtAddress);

exit:
    return error;
}

#if OPENTHREAD_CONFIG_TIME_SYNC_ENABLE
void otMacFrameUpdateTimeIe(otRadioFrame *aFrame, uint64_t aRadioTime, otRadioContext *aRadioContext)
{
    uint8_t *timeIe;
    uint64_t time;

    OT_UNUSED_VARIABLE(aRadioContext);
    VerifyOrExit((aFrame->mInfo.mTxInfo.mIeInfo != nullptr) && (aFrame->mInfo.mTxInfo.mIeInfo->mTimeIeOffset != 0));

    timeIe  = aFrame->mPsdu + aFrame->mInfo.mTxInfo.mIeInfo->mTimeIeOffset;
    time    = aRadioTime + aFrame->mInfo.mTxInfo.mIeInfo->mNetworkTimeOffset;
    *timeIe = aFrame->mInfo.mTxInfo.mIeInfo->mTimeSyncSeq;

    *(++timeIe) = static_cast<uint8_t>(time & 0xff);
    for (uint8_t i = 1; i < sizeof(uint64_t); i++)
    {
        time        = time >> 8;
        *(++timeIe) = static_cast<uint8_t>(time & 0xff);
    }

exit:
    return;
}
#endif // OPENTHREAD_CONFIG_TIME_SYNC_ENABLE

otError otMacFrameProcessTxSfd(otRadioFrame *aFrame, uint64_t aRadioTime, otRadioContext *aRadioContext)
{
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
    if (aRadioContext->mCslPresent) // CSL IE should be filled for every transmit attempt
    {
        otMacFrameSetCslIe(aFrame, aRadioContext->mCslPeriod,
                           ComputeCslPhase(static_cast<uint32_t>(aRadioTime), aRadioContext));
    }
#endif
#if (OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE) && \
    (OPENTHREAD_FTD || OPENTHREAD_MTD)
    if (aRadioContext->mSlwPresent)
    {
        otMacFrameSetThreadDirectScaLtv(aFrame, aRadioContext->mSlwPeriod,
                                        ComputeSlwPhase(static_cast<uint32_t>(aRadioTime), aRadioContext),
                                        aRadioContext->mRamOffsetUs);
    }
#endif
#if OPENTHREAD_CONFIG_TIME_SYNC_ENABLE
    otMacFrameUpdateTimeIe(aFrame, aRadioTime, aRadioContext);
#endif
    aFrame->mInfo.mTxInfo.mTimestamp = aRadioTime;
    return otMacFrameProcessTransmitSecurity(aFrame, aRadioContext);
}

bool otMacFrameSrcAddrMatchCslReceiverPeer(const otRadioFrame *aFrame, const otRadioContext *aRadioContext)
{
    const Mac::Frame &frame   = *static_cast<const Mac::Frame *>(aFrame);
    bool              matches = false;
    Mac::Address      src;

    VerifyOrExit(frame.GetSrcAddr(src) == kErrorNone);

    switch (src.GetType())
    {
    case Mac::Address::kTypeShort:
        VerifyOrExit(aRadioContext->mCslShortAddress != Mac::kShortAddrBroadcast &&
                     aRadioContext->mCslShortAddress != Mac::kShortAddrInvalid);
        VerifyOrExit(src.GetShort() == aRadioContext->mCslShortAddress);
        matches = true;
        break;

    case Mac::Address::kTypeExtended:
        VerifyOrExit(src.GetExtended() == *static_cast<const Mac::ExtAddress *>(&aRadioContext->mCslExtAddress));
        matches = true;
        break;

    case Mac::Address::kTypeNone:
        matches = false;
        break;
    }

exit:
    return matches;
}

#if (OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE) && \
    (OPENTHREAD_FTD || OPENTHREAD_MTD)

bool otMacFrameIsTdLinkCommand(const otRadioFrame *aFrame)
{
    const Mac::Frame &frame = *static_cast<const Mac::Frame *>(aFrame);

    return frame.IsThreadDirectLinkCommand();
}
#endif

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
void otMacFrameSetScaLtvPhase(otRadioFrame *aFrame, uint16_t aPhase)
{
    static_cast<Mac::TxFrame *>(aFrame)->SetScaLtvPhase(aPhase);
}
#endif

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE && (OPENTHREAD_FTD || OPENTHREAD_MTD)

bool otMacFrameIsTdWakeCommand(otRadioFrame *aFrame)
{
    uint8_t keyId;

    if (!otMacFrameIsCommand(aFrame) || otMacFrameIsAckRequested(aFrame) || !otMacFrameIsSecurityEnabled(aFrame) ||
        !otMacFrameIsKeyIdMode1(aFrame))
    {
        return false;
    }

    keyId = otMacFrameGetKeyId(aFrame);

    return keyId == OT_MAC_FRAME_WAKE_KEY_INDEX ||
           (keyId >= OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MIN && keyId <= OT_MAC_FRAME_GUEST_WAKE_KEY_INDEX_MAX);
}

#endif // OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE && (OPENTHREAD_FTD || OPENTHREAD_MTD)

uint8_t otMacFrameGenerateThreadDirectEnhAckIe(const otRadioFrame *aFrame, uint8_t *aDest, uint8_t aDestLen)
{
    using namespace ot;
    using namespace ot::Mac;

    assert(aFrame != nullptr && aDest != nullptr);

    const Frame   &rxFrame  = *static_cast<const Frame *>(aFrame);
    const uint8_t *threadIe = rxFrame.GetHeaderIe(ThreadHeaderIe::kElementId);
    uint8_t        written  = 0;

    if (threadIe == nullptr)
    {
        return 0;
    }

    uint8_t ieLen = reinterpret_cast<const HeaderIe *>(threadIe)->GetLength();

    // Scan the incoming frame's packed LTV stream directly — no scratch buffer needed.
    ChallengeLtv              challenge;
    bool                      found = false;
    PackedLtvStream::Iterator iter;

    iter.Init(threadIe + sizeof(HeaderIe), ieLen);

    while (!iter.IsDone())
    {
        if (iter.GetType() == ChallengeLtvInfo::kType && iter.GetLength() == sizeof(ChallengeLtv))
        {
            memcpy(&challenge, iter.GetValue(), sizeof(ChallengeLtv));
            found = true;
            break;
        }

        IgnoreError(iter.Advance());
    }

    if (!found)
    {
        return 0;
    }

    uint8_t      plainOut[Ltv::kHeaderSize + sizeof(ChallengeLtv)];
    uint8_t      packed[1 + sizeof(ChallengeLtv)];
    uint8_t      ieBuf[sizeof(HeaderIe) + 1 + sizeof(ChallengeLtv)];
    FrameBuilder plainBuilder;
    FrameBuilder ieBuilder;

    plainBuilder.Init(plainOut, sizeof(plainOut));
    IgnoreError(AppendChallengeLtv(plainBuilder, challenge));

    uint8_t packedLen =
        PackedLtvStream::Encode(plainOut, static_cast<uint8_t>(plainBuilder.GetLength()), packed, sizeof(packed));

    ieBuilder.Init(ieBuf, sizeof(ieBuf));
    IgnoreError(AppendThreadHeaderIe(ieBuilder, packed, packedLen));

    written = static_cast<uint8_t>(ieBuilder.GetLength());

    if (written > aDestLen)
    {
        return 0;
    }

    memcpy(aDest, ieBuf, written);

    return written;
}
