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
 *   This file includes definitions for Thread time sync service.
 */

#ifndef TimeSyncService_hpp
#define TimeSyncService_hpp

#include "openthread-core-config.h"

#if FEATURE_TIMESYNCSERVICE_ENABLE
#include "common/locator.hpp"
#include "common/non_copyable.hpp"
#include "common/notifier.hpp"
#include "common/message.hpp"
#include <stdio.h>
#include <openthread/udp.h>
#include "thread/TimeSyncPacket.hpp"
#include "mac/mac_frame.hpp"

namespace ot {
namespace TimeSyncService {

class TimeSyncService : public InstanceLocator, private NonCopyable
{
public:
    /**
     * Initializes the object.
     *
     * @param[in]  aInstance     A reference to the OpenThread instance.
     */
    explicit TimeSyncService(Instance &aInstance);

    ~TimeSyncService();

    /**
     * Notifies the Time Sync Service of events from  OpenThread `Notifier`.
     *
     * @param[in] aEvents   The list of events emitted by `Notifier`.
     */
    void HandleNotifierEvents(Events aEvents);

    /**
     * Checks if a message is a time sync packet.
     *
     * @param[in] aMessage   The message to check.
     *
     * @returns True if it's a time sync packet, false otherwise.
     */
    static bool isTimeSyncPacket(const Message &aMessage);

    /**
     * If the MAC frame is specific to Time Sync Service, then this function will be called to process the frame.
     *
     * @param[in] aFrame   The frame to check.
     *
     * @returns None.
     */
    static void HandleTimeSyncFrame(ot::Mac::TxFrame &aFrame);

    //API calls 
    void SendTimeSyncMessage(void);
    void GetTimeSyncServiceStatus(bool &isClient, bool &isServer, bool &isForcedServer);
    void forceStartAsTimeSyncServer(void);
    void forceStopAsTimeSyncServer(void);
    void GetTimeSyncServiceServerStatus(uint16_t &aServerPort, uint16_t &aConfiguredServerPortForPacketInspection);
    void GetTimeSyncServiceClientStatus(uint16_t &aServerPort, uint16_t &aClientPort, otIp6Address &aServerAddr, uint16_t &aConfiguredServerPortForPacketInspection);
    char *GetTimeSyncServerHistory(void);
    char *getTimeSyncClientHistory(void);
    void TimeSyncClearHistory(void);
    uint8_t GetTimeSyncServerHistorySize(void);
    uint8_t GetTimeSyncClientHistorySize(void);
    char *GetTimeSyncServerHistory(uint8_t idx);
    char *getTimeSyncClientHistory(uint8_t idx);

private:
    static void HandleUdpReceive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo);
    void HandleUdpReceive(otMessage *aMessage, const otMessageInfo *aMessageInfo);

    void CalculateRadioDelay(const uint8_t * aFrame, uint16_t aFrameLen, Message *aMessage, uint32_t &radio_delay, uint64_t &time_now, const char *aLogContext);
    void HandleTimeSyncPacketRequest(otMessage *aMessage, const otMessageInfo *aMessageInfo);
    void HandleTimeSyncPacketAck(otMessage *aMessage, const otMessageInfo *aMessageInfo);
    void HandleTimeSyncPacket(otMessage *aMessage, const otMessageInfo *aMessageInfo);
    void SendTimeSyncPacket(const uint8_t *aBuf, uint16_t aBufLength, const otMessageInfo *aMessageInfo);

    void setConfiguredTimeSyncServerPortForPacketInspection(uint16_t aPort);
    uint16_t getConfiguredTimeSyncServerPortForPacketInspection(void);
    void checkAndBecomeTimeSyncServerorClient();

    //Server APIs
    void startTimeSyncService(void);
    void stopTimeSyncService(void);
    uint16_t getTimeSyncServicePort(void);
    bool isServerStarted(void) {return isTimeSyncServiceStarted;}
    bool isForcedServer(void);
    void setForcedServer(bool aForcedServer);
    bool getForcedServer(void);
    TimeSyncServerHistory * getServerHistory(int &size);
    
    //Client APIs
    void startTimeSyncServiceClient(void);
    void stopTimeSyncServiceClient(void);
    uint16_t getTimeSyncServiceClientPort(void);
    void setServerConfig(otIp6Address aServerAddr, uint16_t aServerPort);
    void getServerConfig(otIp6Address &aServerAddr, uint16_t &aServerPort);
    bool isClientStarted(void) {return isTimeSyncServiceClientStarted;}
    void sendMessageToServer(void);
    TimeSyncClientHistory * getClientHistory(int &size);
    
    void ClearHistory(void);

    otUdpSocket mSocket;
    static otInstance *mInstance;
    uint16_t serverSeqNum;
    static bool isTimeSyncServiceStarted;
    static bool mIsForcedServer; //just for testing
    
    uint16_t clientSeqNum;
    static bool isTimeSyncServiceClientStarted;
    otIp6Address mServerAddr;
    uint16_t mServerPort;
    
    uint64_t last_timestamp_at_server;
    uint64_t last_timestamp_at_client;
    
    static uint16_t timeSync_server_port;
};

#define TSS_SERVER_HISTORY_ARRAY_SIZE 3
#define TSS_CLIENT_HISTORY_ARRAY_SIZE 3
#define TSS_MAX_HISTOGRAM_STRING_LENGTH 5000

}
}
#endif

#endif /* TimeSyncService_hpp */

