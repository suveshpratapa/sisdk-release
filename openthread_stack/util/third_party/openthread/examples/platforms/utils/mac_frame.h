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

/**
 * @file
 * @brief
 *   This file defines the mac frame interface for OpenThread platform radio drivers.
 */

#ifndef OPENTHREAD_UTILS_MAC_FRAME_H
#define OPENTHREAD_UTILS_MAC_FRAME_H

#include <openthread/platform/radio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Specifies the IEEE 802.15.4 Address type.
 */
typedef enum
{
    OT_MAC_ADDRESS_TYPE_NONE,     ///< No address.
    OT_MAC_ADDRESS_TYPE_SHORT,    ///< IEEE 802.15.4 Short Address.
    OT_MAC_ADDRESS_TYPE_EXTENDED, ///< IEEE 802.15.4 Extended Address.
} otMacAddressType;

/**
 * Represents an IEEE 802.15.4 short or extended Address.
 */
typedef struct otMacAddress
{
    union
    {
        otShortAddress mShortAddress; ///< The IEEE 802.15.4 Short Address.
        otExtAddress   mExtAddress;   ///< The IEEE 802.15.4 Extended Address.
    } mAddress;

    otMacAddressType mType; ///< The address type (short, extended, or none).
} otMacAddress;

/**
 * Check if @p aFrame is an Ack frame.
 *
 * @param[in]   aFrame          A pointer to the frame.
 *
 * @retval  true    It is an ACK frame.
 * @retval  false   It is not an ACK frame.
 */
bool otMacFrameIsAck(const otRadioFrame *aFrame);

/**
 * Check if @p aFrame is a Data frame.
 *
 * @param[in]   aFrame          A pointer to the frame.
 *
 * @retval  true    It is a Data frame.
 * @retval  false   It is not a Data frame.
 */
bool otMacFrameIsData(const otRadioFrame *aFrame);

/**
 * Check if @p aFrame is a Command frame.
 *
 * @param[in]   aFrame          A pointer to the frame.
 *
 * @retval  true    It is a Command frame.
 * @retval  false   It is not a Command frame.
 */
bool otMacFrameIsCommand(const otRadioFrame *aFrame);

/**
 * Check if @p aFrame is a Data Request Command.
 *
 * @param[in]   aFrame          A pointer to the frame. For 802.15.4-2015 and above frame,
 *                              the frame should be already decrypted.
 *
 * @retval  true    It is a Data Request Command frame.
 * @retval  false   It is not a Data Request Command frame.
 */
bool otMacFrameIsDataRequest(const otRadioFrame *aFrame);

/**
 * Check if @p aFrame requests ACK.
 *
 * @param[in]   aFrame          A pointer to the frame.
 *
 * @retval  true    It requests ACK.
 * @retval  false   It does not request ACK.
 */
bool otMacFrameIsAckRequested(const otRadioFrame *aFrame);

/**
 * Check if @p aFrame matches the @p aPandId and @p aShortAddress or @p aExtAddress.
 *
 * @param[in]   aFrame          A pointer to the frame.
 * @param[in]   aPanId          The PAN id to match with.
 * @param[in]   aShortAddress   The short address to match with.
 * @param[in]   aExtAddress     The extended address to match with.
 *
 * @retval  true    It is a broadcast or matches with the PAN id and one of the addresses.
 * @retval  false   It doesn't match.
 */
bool otMacFrameDoesAddrMatch(const otRadioFrame *aFrame,
                             otPanId             aPanId,
                             otShortAddress      aShortAddress,
                             const otExtAddress *aExtAddress);

/**
 * Check if @p aFrame matches the @p aPandId and @p aShortAddress, or @p aAltShortAddress or @p aExtAddress.
 *
 * @param[in]   aFrame            A pointer to the frame.
 * @param[in]   aPanId            The PAN id to match with.
 * @param[in]   aShortAddress     The short address to match with.
 * @param[in]   aAltShortAddress  The alternate short address to match with. Can be `OT_RADIO_INVALID_SHORT_ADDR` if
 *                                there is no alternate address.
 * @param[in]   aExtAddress       The extended address to match with.
 *
 * @retval  true    It is a broadcast or matches with the PAN id and one of the addresses.
 * @retval  false   It doesn't match.
 */
bool otMacFrameDoesAddrMatchAny(const otRadioFrame *aFrame,
                                otPanId             aPanId,
                                otShortAddress      aShortAddress,
                                otShortAddress      aAltShortAddress,
                                const otExtAddress *aExtAddress);

/**
 * Get source MAC address.
 *
 * @param[in]   aFrame          A pointer to the frame.
 * @param[out]  aMacAddress     A pointer to MAC address.
 *
 * @retval  OT_ERROR_NONE   Successfully got the source MAC address.
 * @retval  OT_ERROR_PARSE  Failed to parse the source MAC address.
 */
otError otMacFrameGetSrcAddr(const otRadioFrame *aFrame, otMacAddress *aMacAddress);

/**
 * Get destination MAC address.
 *
 * @param[in]   aFrame          A pointer to the frame.
 * @param[out]  aMacAddress     A pointer to MAC address.
 *
 * @retval  OT_ERROR_NONE   Successfully got the destination MAC address.
 * @retval  OT_ERROR_PARSE  Failed to parse the destination MAC address.
 */
otError otMacFrameGetDstAddr(const otRadioFrame *aFrame, otMacAddress *aMacAddress);

/**
 * Get the sequence of @p aFrame.
 *
 * @param[in]   aFrame          A pointer to the frame.
 * @param[out]  aSequence       A pointer to the sequence.
 *
 * @retval  OT_ERROR_NONE   Successfully got the sequence.
 * @retval  OT_ERROR_PARSE  Failed to parse the sequence.
 */
otError otMacFrameGetSequence(const otRadioFrame *aFrame, uint8_t *aSequence);

/**
 * Performs AES CCM on the frame which is going to be sent.
 *
 * @param[in]  aFrame       A pointer to the MAC frame buffer that is going to be sent.
 * @param[in]  aExtAddress  A pointer to the extended address, which will be used to generate nonce
 *                          for AES CCM computation.
 */
void otMacFrameProcessTransmitAesCcm(otRadioFrame *aFrame, const otExtAddress *aExtAddress);

/**
 * Tell if the version of @p aFrame is 2015.
 *
 * @param[in]   aFrame          A pointer to the frame.
 *
 * @retval  true    It is a version 2015 frame.
 * @retval  false   It is not a version 2015 frame.
 */
bool otMacFrameIsVersion2015(const otRadioFrame *aFrame);

/**
 * Generate Imm-Ack for @p aFrame.
 *
 * @param[in]    aFrame             A pointer to the frame.
 * @param[in]    aIsFramePending    Value of the ACK's frame pending bit.
 * @param[out]   aAckFrame          A pointer to the ack frame to be generated.
 */
void otMacFrameGenerateImmAck(const otRadioFrame *aFrame, bool aIsFramePending, otRadioFrame *aAckFrame);

/**
 * Generate Enh-Ack for @p aFrame.
 *
 * @param[in]    aFrame             A pointer to the frame.
 * @param[in]    aIsFramePending    Value of the ACK's frame pending bit.
 * @param[in]    aIeData            A pointer to the IE data portion of the ACK to be sent.
 * @param[in]    aIeLength          The length of IE data portion of the ACK to be sent.
 * @param[out]   aAckFrame          A pointer to the ack frame to be generated.
 *
 * @retval  OT_ERROR_NONE           Successfully generated Enh Ack in @p aAckFrame.
 * @retval  OT_ERROR_PARSE          @p aFrame has incorrect format.
 */
otError otMacFrameGenerateEnhAck(const otRadioFrame *aFrame,
                                 bool                aIsFramePending,
                                 const uint8_t      *aIeData,
                                 uint8_t             aIeLength,
                                 otRadioFrame       *aAckFrame);

/**
 * Set CSL IE content into the frame.
 *
 * @param[in,out]   aFrame         A pointer to the frame to be modified.
 * @param[in]       aCslPeriod     CSL Period in CSL IE.
 * @param[in]       aCslPhase      CSL Phase in CSL IE.
 */
void otMacFrameSetCslIe(otRadioFrame *aFrame, uint16_t aCslPeriod, uint16_t aCslPhase);

/**
 * Tell if the security of @p aFrame is enabled.
 *
 * @param[in]   aFrame          A pointer to the frame.
 *
 * @retval  true    The frame has security enabled.
 * @retval  false   The frame does not have security enabled.
 */
bool otMacFrameIsSecurityEnabled(otRadioFrame *aFrame);

/**
 * Tell if the key ID mode of @p aFrame is 1.
 *
 * @param[in]   aFrame          A pointer to the frame.
 *
 * @retval  true    The frame key ID mode is 1.
 * @retval  false   The frame security is not enabled or key ID mode is not 1.
 */
bool otMacFrameIsKeyIdMode1(otRadioFrame *aFrame);

/**
 * Tell if the key ID mode of @p aFrame is 2.
 *
 * @param[in]   aFrame          A pointer to the frame.
 *
 * @retval  true    The frame key ID mode is 2.
 * @retval  false   The frame security is not enabled or key ID mode is not 2.
 */
bool otMacFrameIsKeyIdMode2(otRadioFrame *aFrame);

/**
 * Get the key ID of @p aFrame.
 *
 * @param[in]   aFrame          A pointer to the frame.
 *
 * @returns The key ID of the frame with key ID mode 1. Returns 0 if failed.
 */
uint8_t otMacFrameGetKeyId(otRadioFrame *aFrame);

/**
 * Set key ID to @p aFrame with key ID mode 1.
 *
 * @param[in,out]   aFrame     A pointer to the frame to be modified.
 * @param[in]       aKeyId     Key ID to be set to the frame.
 */
void otMacFrameSetKeyId(otRadioFrame *aFrame, uint8_t aKeyId);

/**
 * Get the frame counter of @p aFrame.
 *
 * @param[in]   aFrame          A pointer to the frame.
 *
 * @returns The frame counter of the frame. Returns UINT32_MAX if failed.
 */
uint32_t otMacFrameGetFrameCounter(otRadioFrame *aFrame);

/**
 * Set frame counter to @p aFrame.
 *
 * @param[in,out]   aFrame         A pointer to the frame to be modified.
 * @param[in]       aFrameCounter  Frame counter to be set to the frame.
 */
void otMacFrameSetFrameCounter(otRadioFrame *aFrame, uint32_t aFrameCounter);

/**
 * Write CSL IE to a buffer (without setting IE value).
 *
 * @param[out]  aDest    A pointer to the output buffer.
 *
 * @returns  The total count of bytes (total length of CSL IE) written to the buffer.
 */
uint8_t otMacFrameGenerateCslIeTemplate(uint8_t *aDest);

/**
 * Write Enh-ACK Probing IE (Vendor IE with THREAD OUI) to a buffer.
 *
 * @p aIeData could be `NULL`. If @p aIeData is `NULL`, this method generates the IE with the data unset. This allows
 * users to generate the pattern first and update value later. (For example, using `otMacFrameSetEnhAckProbingIe`)
 *
 * @param[out]  aDest          A pointer to the output buffer.
 * @param[in]   aIeData        A pointer to the Link Metrics data.
 * @param[in]   aIeDataLength  The length of Link Metrics data value. Should be `1` or `2`. (Per spec 4.11.3.4.4.6)
 *
 * @returns  The total count of bytes (total length of the Vendor IE) written to the buffer.
 */
uint8_t otMacFrameGenerateEnhAckProbingIe(uint8_t *aDest, const uint8_t *aIeData, uint8_t aIeDataLength);

/**
 * Sets the data value of Enh-ACK Probing IE (Vendor IE with THREAD OUI) in a frame.
 *
 * If no Enh-ACK Probing IE is found in @p aFrame, nothing would be done.
 *
 * @param[in]  aFrame    The target frame that contains the IE. MUST NOT be `NULL`.
 * @param[in]  aData     A pointer to the data value. MUST NOT be `NULL`.
 * @param[in]  aDataLen  The length of @p aData.
 */
void otMacFrameSetEnhAckProbingIe(otRadioFrame *aFrame, const uint8_t *aData, uint8_t aDataLen);

/**
 * Represents the context for radio layer.
 */
typedef struct otRadioContext
{
    otExtAddress   mExtAddress; ///< In little-endian byte order.
    uint32_t       mMacFrameCounter;
    uint32_t       mPrevMacFrameCounter;
    uint32_t       mCslSampleTime;   ///< The sample time based on the microsecond timer.
    uint16_t       mCslPeriod;       ///< In unit of 10 symbols.
    otShortAddress mCslShortAddress; ///< The short address of the CSL receiver's peer.
    otExtAddress   mCslExtAddress;   ///< The extended address of the CSL receiver's peer.
    bool           mCslPresent : 1;  ///< Indicates whether the CSL header IE is present.
#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
    uint32_t mSlwSampleTime; ///< Reference timestamp (us) for SLW phase computation.
    uint16_t mSlwPeriod; ///< SLW period in units of advertised Slot Duration (0 = clear schedule / rx-on-when-idle).
    uint32_t mSlwSlotDurationUs; ///< Advertised SLW Slot Duration, in microseconds (0 defaults to 625 us).
    int16_t  mRamOffsetUs;       ///< RAM offset [-1024, 1023] us; shifts the SLW phase reference.
    bool     mSlwPresent : 1;    ///< True if the SCA LTV should be written on each transmit.
#endif
    otShortAddress   mShortAddress;
    otShortAddress   mAlternateShortAddress;
    otRadioKeyType   mKeyType;
    uint8_t          mKeyId;
    otMacKeyMaterial mPrevKey;
    otMacKeyMaterial mCurrKey;
    otMacKeyMaterial mNextKey;
} otRadioContext;

/**
 * Perform processing of SFD callback from ISR.
 *
 * This function may do multiple tasks as follows.
 *
 *  - CSL IE will be populated (if present)
 *  - Time IE will be populated (if present)
 *  - Tx timestamp will be populated
 *  - Tx security will be performed (including assignment of security frame counter and key id if not assigned)
 *
 * @param[in,out]   aFrame          The target frame. MUST NOT be `NULL`.
 * @param[in]       aRadioTime      The radio time when the SFD was at the antenna.
 * @param[in,out]   aRadioContext   The radio context accessible in ISR.
 *
 * @returns the error processing the callback. The caller should abort transmission on failures.
 */
otError otMacFrameProcessTxSfd(otRadioFrame *aFrame, uint64_t aRadioTime, otRadioContext *aRadioContext);

/**
 * Process frame tx security.
 *
 * @param[in,out]   aFrame          The target frame. MUST NOT be `NULL`.
 * @param[in,out]   aRadioContext   The radio context accessible in ISR.
 *
 * @retval OT_ERROR_NONE     Successfully processed security.
 * @retval OT_ERROR_FAILED   Failed to processed security.
 * @retval OT_ERROR_SECURITY Failed to processed security for missing key.
 */
otError otMacFrameProcessTransmitSecurity(otRadioFrame *aFrame, otRadioContext *aRadioContext);

/**
 * Indicates whether the 15.4 frame's source address matches the short or extended address of the CSL receiver's peer.
 *
 * @param[in]   aFrame          The target 15.4 frame. MUST NOT be `NULL`.
 * @param[in]   aRadioContext   The radio context accessible in ISR.
 *
 * @retval  TRUE    The source address of the frame matches the short or extended address of the CSL receiver's peer.
 * @retval  FALSE   The source address of the frame does not match the short or extended address of the CSL receiver's
 *                  peer.
 */
bool otMacFrameSrcAddrMatchCslReceiverPeer(const otRadioFrame *aFrame, const otRadioContext *aRadioContext);

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
/**
 * Tell if @p aFrame is a Thread Direct Wake Command frame.
 *
 * A TD Wake Command is a MAC Command with no ACK request and wake key index 129 or 130-192
 * in the Auxiliary Security Header (stamped by the stack before transmit).
 *
 * @param[in] aFrame  A pointer to the frame.
 *
 * @retval true   The frame is a TD Wake Command.
 * @retval false  The frame is not a TD Wake Command.
 */
bool otMacFrameIsTdWakeCommand(otRadioFrame *aFrame);
#endif

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
/**
 * Tells if @p aFrame is a Thread Direct Link Command frame (MAC Cmd 0x54 / Thread Cmd 0x02).
 *
 * @param[in] aFrame  A pointer to the frame.
 *
 * @retval true   The frame is a TD Link Command.
 * @retval false  The frame is not a TD Link Command.
 */
bool otMacFrameIsTdLinkCommand(const otRadioFrame *aFrame);
#endif

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE
/**
 * Patches the SLW phase field of the SCA LTV in the Thread Header IE of @p aFrame.
 *
 * Locates the Thread Header IE (element 0x2d) in the 802.15.4 Header IE list, walks
 * the LTV stream to find the SCA LTV (type 0x02), and overwrites its 2-byte SLW Phase
 * field with @p aPhase.  A no-op if the frame contains no Thread Header IE or the SCA
 * LTV does not carry SLW fields.
 *
 * @param[in] aFrame  A pointer to the frame to patch.
 * @param[in] aPhase  SLW phase in slot-duration units.
 */
void otMacFrameSetScaLtvPhase(otRadioFrame *aFrame, uint16_t aPhase);
#endif

/**
 * Thread Direct SCA fields included in an Enh-ACK Thread Header IE.
 */
typedef struct otMacFrameThreadDirectSca
{
    uint16_t mSlwPeriod;     ///< SLW period in slot-duration units.
    uint16_t mSlwPhase;      ///< SLW phase in slot-duration units.
    int16_t  mRamOffsetUs;   ///< RAM offset in microseconds.
    uint8_t  mClockAccuracy; ///< Clock accuracy in ppm.
    uint8_t  mUncertainty;   ///< Clock uncertainty in 10 us units.
    bool     mHasSlw;        ///< True if local SLW fields should be included.
} otMacFrameThreadDirectSca;

/**
 * Builds the Thread Header IE bytes for an Enh-ACK response to a Thread Direct frame.
 *
 * Echoes the Challenge LTV from @p aFrame when present, and appends an SCA LTV when
 * @p aSca is non-null and @p aSca->mHasSlw is true. The output is ready to copy into
 * the Enh-ACK IE region at the ACK turnaround deadline.
 *
 * @param[in]  aFrame    Received Thread Direct frame.
 * @param[out] aDest     Output buffer for the Thread Header IE bytes.
 * @param[in]  aDestLen  Capacity of @p aDest in bytes.
 * @param[in]  aSca      Local SCA to advertise, or NULL to omit the SCA LTV.
 *
 * @returns  Number of bytes written to @p aDest, or 0 if there is nothing to emit
 *           or @p aDest is too small.
 */
uint8_t otMacFrameGenerateThreadDirectEnhAckIe(const otRadioFrame              *aFrame,
                                               uint8_t                         *aDest,
                                               uint8_t                          aDestLen,
                                               const otMacFrameThreadDirectSca *aSca);

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
/**
 * Writes an SCA LTV into the Thread Header IE of @p aFrame.
 *
 * The frame must already contain a pre-allocated Thread Header IE of sufficient
 * size (at least 10 bytes: 2-byte IE header + 8-byte SCA LTV with SLW fields).
 * Only the SCA LTV bytes are overwritten; other LTVs in the same IE are preserved
 * if they precede the SCA LTV.  If no Thread Header IE is present, this is a no-op.
 *
 * Intended for use in time-critical transmit callbacks (e.g. SFD hook) where the
 * SCA LTV phase must be stamped with the current radio time.
 *
 * The SLW start time is: frame TX time + @p aRamOffsetUs + SLW phase (in slots).
 *
 * @param[in] aFrame      Frame to update.
 * @param[in] aSlwPeriod  SLW period in units of advertised Slot Duration
 *                        (0 = clear schedule / rx-on-when-idle).
 * @param[in] aSlwPhase   Time from (TX time + @p aRamOffsetUs) to the next SLW window, in slots.
 * @param[in] aRamOffsetUs  RAM offset in [-1024, 1023] us.
 */
void otMacFrameSetThreadDirectScaLtv(otRadioFrame *aFrame,
                                     uint16_t      aSlwPeriod,
                                     uint16_t      aSlwPhase,
                                     int16_t       aRamOffsetUs);

/**
 * Tells if @p aFrame carries an SCA LTV with SLW fields in its Thread Header IE.
 *
 * @param[in] aFrame  A pointer to the frame.
 *
 * @retval true   The frame carries an SCA LTV with a non-zero-length SLW schedule.
 * @retval false  The frame carries no Thread Header IE, or its SCA LTV has no SLW fields
 *                (e.g. the zero-length SCA LTV in a TD Teardown frame).
 */
bool otMacFrameHasThreadDirectScaLtv(const otRadioFrame *aFrame);
#endif

#if OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_INITIATOR_ENABLE || OPENTHREAD_CONFIG_THREAD_DIRECT_WAKE_LISTENER_ENABLE
/**
 * Computes how many whole slots away the next SLW sample point is from a frame's MAC
 * header TX time, plus the RAM offset covering the leftover microseconds.
 *
 * @param[in]  aNextSampleTimeUs  Next expected SLW sample time, in radio microseconds.
 * @param[in]  aMacHeaderTxTime   MAC header TX time, in radio microseconds.
 * @param[in]  aPeriodSlots       SLW period in units of @p aSlotDurationUs.
 * @param[in]  aSlotDurationUs    SLW slot duration, in microseconds.
 * @param[out] aPhaseSlots        Computed SLW phase, in slots.
 * @param[out] aRamOffsetUs       Computed RAM offset, in microseconds.
 *
 * @retval true   The phase and RAM offset were computed successfully.
 * @retval false  Inputs were invalid (zero period/slot duration) or the computed
 *                phase/RAM offset fell outside representable bounds.
 */
bool otMacFrameCalculateSlwPhaseAndRamOffset(uint32_t  aNextSampleTimeUs,
                                             uint32_t  aMacHeaderTxTime,
                                             uint16_t  aPeriodSlots,
                                             uint32_t  aSlotDurationUs,
                                             uint16_t *aPhaseSlots,
                                             int16_t  *aRamOffsetUs);
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif // OPENTHREAD_UTILS_MAC_FRAME_H
