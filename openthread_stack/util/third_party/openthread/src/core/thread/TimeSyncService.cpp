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
 *   This file implements Time Sync Service.
 */

#include "thread/TimeSyncService.hpp"
#if FEATURE_TIMESYNCSERVICE_ENABLE
#include "thread/TimeSyncPacket.hpp"
#include <openthread/message.h>
#include <openthread/udp.h>
#include <openthread/thread.h>
#include <openthread/ip6.h>
#include "common/code_utils.hpp"
#include <openthread/thread_ftd.h>
#include <openthread/server.h>
#include <openthread/netdata.h>
#include <openthread/border_router.h>
#include "common/encoding.hpp"
#include <string.h>
#include "instance/instance.hpp"
#include "mac/mac_types.hpp"
#include "net/ip6_address.hpp"
#include "thread/lowpan.hpp"

namespace ot {
namespace TimeSyncService {

    RegisterLogModule("TimeSyncSer");

// #define TIMESYNCSERVICE_VERBOSE_LOG

bool TimeSyncService::isTimeSyncServiceStarted = false;
bool TimeSyncService::isTimeSyncServiceClientStarted = false;
bool TimeSyncService::mIsForcedServer = false;
uint16_t TimeSyncService::timeSync_server_port = 0;
otInstance *TimeSyncService::mInstance = NULL;


TimeSyncServerHistory timeSyncServerHistory[TSS_SERVER_HISTORY_ARRAY_SIZE];
uint16_t timeSyncServerHistoryIndex = 0;

TimeSyncClientHistory timeSyncClientHistory[TSS_CLIENT_HISTORY_ARRAY_SIZE];
uint16_t timeSyncClientHistoryIndex = 0;

TimeSyncService::TimeSyncService(Instance &aInstance):InstanceLocator(aInstance)
{
    mInstance = &GetInstance();
    serverSeqNum = 0;
    clientSeqNum = 0;
    memset(reinterpret_cast<void *>(&mSocket), 0, sizeof(mSocket));
    isTimeSyncServiceStarted = false;
    isTimeSyncServiceClientStarted = false;
    mIsForcedServer = false;
    timeSync_server_port = 0;
}

TimeSyncService::~TimeSyncService()
{
    isTimeSyncServiceStarted = false;
    isTimeSyncServiceClientStarted = false;
    stopTimeSyncService();
    ClearHistory();
}

void TimeSyncService::startTimeSyncService(void)
{
    otNetifIdentifier netif = OT_NETIF_THREAD_INTERNAL;
    otSockAddr        sockaddr;
    otError error = OT_ERROR_NONE;
    
    if(isTimeSyncServiceStarted == true || isTimeSyncServiceClientStarted == true)
    {
        LogWarn("TSS startTimeSyncService: Either server (%d) or client (%d) is already started, not starting again", isTimeSyncServiceStarted, isTimeSyncServiceClientStarted);
        return;
    }
    
    VerifyOrExit(mInstance != NULL, error = OT_ERROR_INVALID_STATE);
    VerifyOrExit(mSocket.mSockName.mPort == 0, error = OT_ERROR_INVALID_STATE);
    
    memset(reinterpret_cast<void *>(&sockaddr), 0, sizeof(sockaddr));
    memset(reinterpret_cast<void *>(timeSyncClientHistory), 0, sizeof(TimeSyncClientHistory)*TSS_CLIENT_HISTORY_ARRAY_SIZE);    
    
    VerifyOrExit(!otUdpIsOpen(mInstance, &mSocket), error = OT_ERROR_ALREADY);
    error = otUdpOpen(mInstance, &mSocket, HandleUdpReceive, this);
    
    if(error != OT_ERROR_NONE) {
        LogWarn("TSS startTimeSyncService: Server failed to open UDP with error = %d", error);
        goto exit;
    }
    
    LogInfo("TSS startTimeSyncService: opened UDP - port = %d, error = %d", mSocket.mSockName.mPort, error);
    
    error = otUdpBind(mInstance, &mSocket, &sockaddr, netif);
    
    if(error != OT_ERROR_NONE) {
        LogWarn("TSS startTimeSyncService: failed to bind UDP with error = %d", error);
        goto exit;
    }
    
    LogInfo("TSS startTimeSyncService: binded UDP - port = %d, error = %d", mSocket.mSockName.mPort, error);
    
#if OPENTHREAD_CONFIG_TMF_NETDATA_SERVICE_ENABLE && OPENTHREAD_CONFIG_BORDER_ROUTER_ENABLE
    // Register as Time Sync Service endpoint in Network Data (optional for mesh forwarders)
    otServiceConfig aConfig;
    aConfig.mEnterpriseNumber = 63;
    aConfig.mServiceData[0] = 0x03;
    aConfig.mServiceData[1] = (uint8_t)((mSocket.mSockName.mPort & 0xFF00)>>8);
    aConfig.mServiceData[2] = (uint8_t)(mSocket.mSockName.mPort);
    aConfig.mServerConfig.mStable = true;
    aConfig.mServerConfig.mServerDataLength = 0;
    aConfig.mServiceDataLength = 3;

    error = otServerAddService(mInstance, &aConfig);
    
    if(error != OT_ERROR_NONE) {
        LogWarn("TSS startTimeSyncService: failed to add service with error = %d", error);
        goto exit;
    }
    
    error = otBorderRouterRegister(mInstance);
    
    if(error != OT_ERROR_NONE) {
        LogWarn("TSS startTimeSyncService: failed to register service after add with error = %d", error);
        goto exit;
    }
#endif // OPENTHREAD_CONFIG_TMF_NETDATA_SERVICE_ENABLE && OPENTHREAD_CONFIG_BORDER_ROUTER_ENABLE
    
    isTimeSyncServiceStarted = true;
    
    LogInfo("TSS startTimeSyncService: started TimeSync Server");
    
    exit:
    return;
}

void TimeSyncService::stopTimeSyncService(void)
{
    otError error = OT_ERROR_NONE;
    
    if(isTimeSyncServiceStarted == false)
    {
        LogWarn("TSS stopTimeSyncService: server is already stopped, not stopping again");
        return;
    }
    
    error = otUdpClose(mInstance, &mSocket);
    
    if(error != OT_ERROR_NONE) {
        LogWarn("TSS stopTimeSyncService: failed to close UDP with error = %d", error);
        goto exit;
    }
    
    memset(reinterpret_cast<void *>(&mSocket), 0, sizeof(mSocket));
    
#if OPENTHREAD_CONFIG_TMF_NETDATA_SERVICE_ENABLE && OPENTHREAD_CONFIG_BORDER_ROUTER_ENABLE
    // Unregister Time Sync Service endpoint from Network Data (optional for mesh forwarders)
    otServiceConfig aConfig;
    aConfig.mEnterpriseNumber = 63;
    aConfig.mServiceData[0] = 0x03;
    aConfig.mServiceData[1] = (uint8_t)((mSocket.mSockName.mPort & 0xFF00)>>8);
    aConfig.mServiceData[2] = (uint8_t)(mSocket.mSockName.mPort);
    aConfig.mServiceDataLength = 3;

    error = otServerRemoveService(mInstance, aConfig.mEnterpriseNumber, aConfig.mServiceData, aConfig.mServiceDataLength);
    
    if (error == OT_ERROR_NONE) {
        
        error = otBorderRouterRegister(mInstance);
        
        if(error != OT_ERROR_NONE) {
            LogWarn("TSS stopTimeSyncService: failed to register service after removal with error = %d", error);
        }
    } else {
        LogWarn("TSS stopTimeSyncService: failed to remove service with error = %d", error);
    }
#endif // OPENTHREAD_CONFIG_TMF_NETDATA_SERVICE_ENABLE && OPENTHREAD_CONFIG_BORDER_ROUTER_ENABLE
    
    isTimeSyncServiceStarted = false;
    ClearHistory();
    LogInfo("TSS stopTimeSyncService: stopped TimeSync Server");
    
    exit:
    return;
}

uint16_t TimeSyncService::getTimeSyncServicePort(void)
{
    if (isTimeSyncServiceStarted) {
        return mSocket.mSockName.mPort;
    } else {
        return kTimeSyncInvalidPort;
    }
}

bool TimeSyncService::isForcedServer(void) {
    LogDebg("TSS isForcedServer: %d",mIsForcedServer);
    return mIsForcedServer;
}
void TimeSyncService::setForcedServer(bool aForcedServer) {
    mIsForcedServer = aForcedServer;
    
    LogDebg("TSS setForcedServer: mIsForcedServer = %d, aForcedServer = %d",mIsForcedServer, aForcedServer);
}
bool TimeSyncService::getForcedServer(void) {
    LogDebg("TSS getForcedServer: %d",mIsForcedServer);
    return mIsForcedServer;
}

void TimeSyncService::HandleUdpReceive(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    static_cast<TimeSyncService *>(aContext)->HandleUdpReceive(aMessage, aMessageInfo);
}

void TimeSyncService::HandleTimeSyncPacketRequest(otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    uint8_t receivedBuf[150], responsedBuf[150];
    uint16_t receivedBufLength = 0, responseBufLength = 0;
    uint64_t current_timestamp_at_server{0ULL};
    TimeSyncPacket timeSyncMessage;
    TimeSyncPacketRequest timeSyncRequestMessage;
    
    OT_UNUSED_VARIABLE(receivedBufLength = otMessageRead(aMessage, otMessageGetOffset(aMessage), receivedBuf, sizeof(receivedBuf) - 1));
    
    responseBufLength = sizeof(TimeSyncPacket);
    
    memcpy(&timeSyncRequestMessage, receivedBuf, sizeof(timeSyncRequestMessage));

    LogInfo("TSS HandleTimeSyncPacketRequest: received TIMESYNC_PACKET_REQUEST, client SN : %d", timeSyncRequestMessage.clientSeqNum);
            
    memset(reinterpret_cast<void *>(&timeSyncMessage), 0, sizeof(TimeSyncPacket));
    serverSeqNum++;
    timeSyncMessage.clientSeqNum = timeSyncRequestMessage.clientSeqNum;
    timeSyncMessage.serverSeqNum = serverSeqNum;
    timeSyncMessage.messageType = TIMESYNC_PACKET;
    timeSyncMessage.version = TIMESYNC_PROTOCOL_VERSION;

    current_timestamp_at_server = otPlatRadioGetNow(&GetInstance());
    timeSyncMessage.timeStamp = current_timestamp_at_server;
    
    memcpy(responsedBuf, &timeSyncMessage, responseBufLength);
    
    timeSyncServerHistory[serverSeqNum % TSS_SERVER_HISTORY_ARRAY_SIZE].orig_server_time = current_timestamp_at_server;
        
    LogDebg("TSS HandleTimeSyncPacketRequest: trying to reply TIMESYNC_PACKET, clientSeqNum = %d, serverSeqNum = %d",
        timeSyncMessage.clientSeqNum,
        timeSyncMessage.serverSeqNum);
    LogDebg("TSS HandleTimeSyncPacketRequest: current_timestamp_at_server = " TSS_U64_HEX_FMT,
        TSS_U64_HEX_ARGS(current_timestamp_at_server));
    
    SendTimeSyncPacket(responsedBuf, responseBufLength, aMessageInfo);
}

void TimeSyncService::HandleTimeSyncPacketAck(otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    uint8_t receivedBuf[150];
    uint16_t receivedBufLength = 0;
    int64_t e2e_error = 0;
    TimeSyncPacketAck timeSyncAckMessage;
    uint64_t client_timestamp, current_timestamp_at_client = 0, e2e_delay = 0, current_server_time{0ULL};
    int64_t server_client_time_delta = 0;
    uint16_t dst_rloc16;
    bool final_dirty_bit = 0;
    uint8_t server_history_index = 0xFF;
    uint32_t radio_delay;
    
    receivedBufLength = otMessageRead(aMessage, otMessageGetOffset(aMessage), receivedBuf, sizeof(receivedBuf) - 1);
    memcpy(&timeSyncAckMessage, receivedBuf, sizeof(TimeSyncPacketAck));

    LogInfo("TSS HandleTimeSyncPacketAck: received TIMESYNC_PACKET_ACK, client SN : %d, server SN : %d", 
            timeSyncAckMessage.clientSeqNum, timeSyncAckMessage.serverSeqNum);
        
    if(timeSyncAckMessage.serverSeqNum == serverSeqNum)
    {
        server_history_index = timeSyncAckMessage.serverSeqNum % TSS_SERVER_HISTORY_ARRAY_SIZE;
    }
    
    dst_rloc16 = otThreadGetRloc16(mInstance);
    if((timeSyncAckMessage.dirtyBit == 0x00) && (timeSyncAckMessage.rloc16 == dst_rloc16)) {
        final_dirty_bit = 0;
    } else {
        final_dirty_bit = 1;
    }
    
    if (server_history_index != 0xFF)
    {
        client_timestamp = timeSyncAckMessage.timeStamp;
        CalculateRadioDelay(receivedBuf, receivedBufLength, &AsCoreType(aMessage), radio_delay, current_server_time, "HandleTimeSyncPacketAck");

        timeSyncServerHistory[server_history_index].isValid = true;
        timeSyncServerHistory[server_history_index].clientTimeStamp = client_timestamp;
        timeSyncServerHistory[server_history_index].timeOffset = timeSyncAckMessage.timeOffset;
        timeSyncServerHistory[server_history_index].radioDelay = radio_delay;
        timeSyncServerHistory[server_history_index].numHops = timeSyncAckMessage.numHops;
        timeSyncServerHistory[server_history_index].dirty_bit = final_dirty_bit;
        timeSyncServerHistory[server_history_index].clientSeqNum = timeSyncAckMessage.clientSeqNum;
        timeSyncServerHistory[server_history_index].serverSeqNum = timeSyncAckMessage.serverSeqNum;
        timeSyncServerHistory[server_history_index].tsp_timeOffset = timeSyncAckMessage.tsp_timeOffset;
        timeSyncServerHistory[server_history_index].tsp_radioDelay = timeSyncAckMessage.tsp_radioDelay;
        timeSyncServerHistory[server_history_index].tsp_host_delay = timeSyncAckMessage.tsp_host_delay;
        timeSyncServerHistory[server_history_index].tsp_dirty_bit = timeSyncAckMessage.tsp_dirtyBit;
        timeSyncServerHistory[server_history_index].tsp_numHops = timeSyncAckMessage.tsp_numHops;
        
        otIp6AddressToString(&(aMessageInfo->mPeerAddr), timeSyncServerHistory[server_history_index].clientIpAddressString, OT_IP6_ADDRESS_STRING_SIZE);
    
        if(current_server_time > timeSyncServerHistory[server_history_index].orig_server_time)
        {
            e2e_delay = current_server_time - timeSyncServerHistory[server_history_index].orig_server_time; // e2e_delay: end-to-end delay from timestamp when timesync is requested to ack receipt
        }
    
        current_timestamp_at_client =  client_timestamp + timeSyncAckMessage.timeOffset + radio_delay;
        
        server_client_time_delta = current_server_time - current_timestamp_at_client;
        
        e2e_error = e2e_delay - (timeSyncAckMessage.timeOffset + radio_delay + timeSyncAckMessage.tsp_timeOffset + timeSyncAckMessage.tsp_radioDelay + timeSyncAckMessage.tsp_host_delay); // e2e_error: difference between measured end-to-end delay and calculated delay components
        
        timeSyncServerHistory[server_history_index].current_server_time = current_server_time;
        timeSyncServerHistory[server_history_index].host_delay = 0;
        timeSyncServerHistory[server_history_index].e2e_delay = e2e_delay;
        timeSyncServerHistory[server_history_index].server_client_time_delta = server_client_time_delta;
        timeSyncServerHistory[server_history_index].calculated_client_time = current_timestamp_at_client;
        timeSyncServerHistory[server_history_index].e2e_error = e2e_error;
        timeSyncServerHistory[server_history_index].client_drift_in_ppm = timeSyncAckMessage.client_drift_in_ppm;
    
        LogDebg("TSS HandleTimeSyncPacketAck: client_ts=" TSS_U64_HEX_FMT ", offset=%ld, radioDelay=%lu",
            TSS_U64_HEX_ARGS(client_timestamp), (long)timeSyncAckMessage.timeOffset, (unsigned long)radio_delay);
        LogDebg("TSS HandleTimeSyncPacketAck: tsp_time_offset = %ld, tsp_radio_delay = %lu",
            (long)timeSyncAckMessage.tsp_timeOffset,
            (unsigned long)timeSyncAckMessage.tsp_radioDelay);

        LogDebg("TSS HandleTimeSyncPacketAck: tsp_host_delay = %ld, RLOC16 = 0x%x, RLOC16 in packet = 0x%x",
            (long)timeSyncAckMessage.tsp_host_delay,
            dst_rloc16,
            timeSyncAckMessage.rloc16);
        LogDebg("TSS HandleTimeSyncPacketAck: numHops = %d, client_drift_in_ppm = %d",
            timeSyncAckMessage.numHops,
            timeSyncAckMessage.client_drift_in_ppm);

        LogDebg("TSS HandleTimeSyncPacketAck: current_timestamp_at_client = " TSS_U64_HEX_FMT ", e2e_delay = " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(current_timestamp_at_client),
            TSS_U64_HEX_ARGS(e2e_delay));
        LogDebg("TSS HandleTimeSyncPacketAck: e2e_error = " TSS_I64_HEX_FMT ", current_server_time = " TSS_U64_HEX_FMT,
            TSS_I64_HEX_ARGS(e2e_error),
            TSS_U64_HEX_ARGS(current_server_time));
        LogDebg("TSS HandleTimeSyncPacketAck: prev timestamp = " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(timeSyncServerHistory[timeSyncAckMessage.serverSeqNum % TSS_SERVER_HISTORY_ARRAY_SIZE].orig_server_time));

        LogDebg("TSS HandleTimeSyncPacketAck: server_client_time_delta = " TSS_I64_HEX_FMT ", final_dirty_bit = %d",
            TSS_I64_HEX_ARGS(server_client_time_delta),
            final_dirty_bit);
        LogDebg("TSS HandleTimeSyncPacketAck: clientIpAddressString = %s",
            timeSyncServerHistory[server_history_index].clientIpAddressString);
    }
}

void TimeSyncService::HandleTimeSyncPacket(otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    uint8_t receivedBuf[150], responsedBuf[150];
    uint16_t receivedBufLength = 0, responseBufLength = 0;
    uint64_t server_timestamp = 0, current_timestamp_at_server = 0;
    int64_t client_server_time_delta = 0;
    uint32_t radio_delay;
    uint16_t dst_rloc16;
    bool final_dirty_bit = 0, calc_drift = false;
    uint8_t client_history_index = 0;
    int16_t client_drift_in_ppm = 0;
    TimeSyncPacketAck timeSyncAck;
    TimeSyncPacket timeSyncMessage;
    uint64_t current_client_time{0ULL};
    
    receivedBufLength = otMessageRead(aMessage, otMessageGetOffset(aMessage), receivedBuf, sizeof(receivedBuf) - 1);
        
    responseBufLength = sizeof(TimeSyncPacketAck);
    
    memcpy(&timeSyncMessage, receivedBuf, sizeof(TimeSyncPacket));
    
    LogInfo("TSS HandleTimeSyncPacket: - received TIMESYNC_PACKET, client SN : %d, server SN : %d", 
            timeSyncMessage.clientSeqNum, timeSyncMessage.serverSeqNum);

    server_timestamp = timeSyncMessage.timeStamp;
    client_history_index = timeSyncMessage.clientSeqNum % TSS_CLIENT_HISTORY_ARRAY_SIZE;
    if(timeSyncMessage.clientSeqNum != clientSeqNum)
    {
        LogWarn("TSS HandleTimeSyncPacket: out of sync TIMESYNC_PACKET, expected %d received %d",
            clientSeqNum, timeSyncMessage.clientSeqNum);
    }
    
    dst_rloc16 = otThreadGetRloc16(mInstance);
    if((timeSyncMessage.dirtyBit == 0x00) && (timeSyncMessage.rloc16 == dst_rloc16)) {
        final_dirty_bit = 0;
    } else {
        final_dirty_bit = 1;
    }
    
    calc_drift = (last_timestamp_at_server == 0x00)?false:true;
    
    memset(reinterpret_cast<void *>(&timeSyncAck), 0, sizeof(TimeSyncPacketAck));
    timeSyncAck.clientSeqNum = timeSyncMessage.clientSeqNum;
    timeSyncAck.serverSeqNum = timeSyncMessage.serverSeqNum;
    timeSyncAck.messageType = TIMESYNC_PACKET_ACK;
    timeSyncAck.version = TIMESYNC_PROTOCOL_VERSION;
    timeSyncAck.tsp_timeOffset = timeSyncMessage.timeOffset;
    timeSyncAck.tsp_dirtyBit = final_dirty_bit;
    timeSyncAck.tsp_numHops = timeSyncMessage.numHops;
    
    timeSyncClientHistory[client_history_index].isValid = true;
    timeSyncClientHistory[client_history_index].serverTimeStamp = server_timestamp;
    timeSyncClientHistory[client_history_index].timeOffset = timeSyncMessage.timeOffset;
    timeSyncClientHistory[client_history_index].numHops = timeSyncMessage.numHops;
    timeSyncClientHistory[client_history_index].dirty_bit = final_dirty_bit;
    timeSyncClientHistory[client_history_index].clientSeqNum = timeSyncMessage.clientSeqNum;
    timeSyncClientHistory[client_history_index].serverSeqNum = timeSyncMessage.serverSeqNum;
    
    otIp6AddressToString(&(aMessageInfo->mPeerAddr), timeSyncClientHistory[client_history_index].serverIpAddressString, OT_IP6_ADDRESS_STRING_SIZE);   
    
    CalculateRadioDelay(receivedBuf, receivedBufLength, &AsCoreType(aMessage), radio_delay, current_client_time, "HandleTimeSyncPacket");
    
    timeSyncAck.tsp_radioDelay = radio_delay;
    timeSyncClientHistory[client_history_index].radioDelay = radio_delay;

    current_timestamp_at_server =  server_timestamp + timeSyncMessage.timeOffset + radio_delay;
    
    if(calc_drift) {
        int64_t client_time_delta_us = (current_client_time - last_timestamp_at_client) - (current_timestamp_at_server - last_timestamp_at_server);
        uint64_t server_time_delta_sec = (current_timestamp_at_server > last_timestamp_at_server)? (current_timestamp_at_server - last_timestamp_at_server)/(1000*1000): 0;
        
        client_drift_in_ppm = (server_time_delta_sec)? (int16_t)(client_time_delta_us/server_time_delta_sec): 0;
        
        LogDebg("TSS HandleTimeSyncPacket: Drift Calc Base Values - current_client_time: " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(current_client_time));
        LogDebg("TSS HandleTimeSyncPacket: Drift Calc Base Values - current_timestamp_at_server: " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(current_timestamp_at_server));

        LogDebg("TSS HandleTimeSyncPacket: Drift Calc Base Values - last_timestamp_at_client: " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(last_timestamp_at_client));
        LogDebg("TSS HandleTimeSyncPacket: Drift Calc Base Values - last_timestamp_at_server: " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(last_timestamp_at_server));
        
        LogDebg("TSS HandleTimeSyncPacket: Drift Calc - client_time_delta_us: " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(client_time_delta_us));
        LogDebg("TSS HandleTimeSyncPacket: Drift Calc - server_time_delta_sec: " TSS_U64_HEX_FMT ", client_drift_in_ppm: %d",
            TSS_U64_HEX_ARGS(server_time_delta_sec),
            (int)client_drift_in_ppm);
    }
    last_timestamp_at_server = current_timestamp_at_server;
    last_timestamp_at_client = current_client_time;
    
    client_server_time_delta = current_client_time - current_timestamp_at_server;
    
    timeSyncAck.tsp_host_delay = 0;
    timeSyncAck.client_drift_in_ppm = client_drift_in_ppm;
    timeSyncClientHistory[client_history_index].host_delay = 0;
    timeSyncClientHistory[client_history_index].client_server_time_delta = client_server_time_delta;
    timeSyncClientHistory[client_history_index].current_client_time = current_client_time;
    timeSyncClientHistory[client_history_index].calculated_server_time = current_timestamp_at_server;
    
    LogDebg("TSS HandleTimeSyncPacket: server_timestamp = " TSS_U64_HEX_FMT ", timeOffset = %ld, RxRadioDelay = %lu",
        TSS_U64_HEX_ARGS(server_timestamp),
        (long)timeSyncMessage.timeOffset,
        (unsigned long)radio_delay);
    LogDebg("TSS HandleTimeSyncPacket: RLOC16 = 0x%x, RLOC16 in packet = 0x%x",
        dst_rloc16,
        timeSyncMessage.rloc16);
        
    LogDebg("TSS HandleTimeSyncPacket: current_timestamp_at_server = " TSS_U64_HEX_FMT ", final_dirty_bit = %d",
        TSS_U64_HEX_ARGS(current_timestamp_at_server),
        final_dirty_bit);
    LogDebg("TSS HandleTimeSyncPacket: client_server_time_delta = " TSS_I64_HEX_FMT ", numHops = %d",
        TSS_I64_HEX_ARGS(client_server_time_delta),
        timeSyncMessage.numHops);
    LogDebg("TSS HandleTimeSyncPacket: serverIpAddressString = %s",
        timeSyncClientHistory[client_history_index].serverIpAddressString);
    
    timeSyncAck.timeStamp = current_client_time;
    memcpy(responsedBuf, &timeSyncAck, responseBufLength);

    LogInfo("TSS HandleTimeSyncPacket: trying to reply TIMESYNC_PACKET_ACK, clientSeqNum = %d, serverSeqNum = %d",
        timeSyncAck.clientSeqNum, timeSyncAck.serverSeqNum);
    LogDebg("TSS HandleTimeSyncPacket: current_client_time = " TSS_U64_HEX_FMT, TSS_U64_HEX_ARGS(current_client_time));
    
    SendTimeSyncPacket(responsedBuf, responseBufLength, aMessageInfo);
}

void TimeSyncService::SendTimeSyncPacket(const uint8_t *responsedBuf, uint16_t responseBufLength, const otMessageInfo *aMessageInfo)
{
    otError           error   = OT_ERROR_NONE;
    otMessage *message = nullptr;
    otMessageSettings messageSettings = {true, OT_MESSAGE_PRIORITY_NORMAL};

    VerifyOrExit(otUdpIsOpen(mInstance, &mSocket), error = OT_ERROR_INVALID_STATE);
    
    message = otUdpNewMessage(mInstance, &messageSettings);
    VerifyOrExit(message != nullptr, error = OT_ERROR_NO_BUFS);
    
    error = otMessageAppend(message, responsedBuf, responseBufLength);
    
    if(error != OT_ERROR_NONE) {
        LogWarn("TSS SendTimeSyncPacket: failed to append message with error = %d", error);
        goto exit;
    }
    
    error = otUdpSend(mInstance, &mSocket, message, aMessageInfo);
    
    if(error != OT_ERROR_NONE) {
        LogWarn("TSS SendTimeSyncPacket: failed to UDP send with error = %d", error);
        goto exit;
    }
    
    message = nullptr;
    
exit:
    if (message != nullptr)
    {
        otMessageFree(message);
        LogWarn("TSS SendTimeSyncPacket: send message failed, error = %d", error);
    }

    LogInfo("TSS SendTimeSyncPacket: sent message");

    return;
}

void TimeSyncService::HandleUdpReceive(otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    uint8_t receivedBuf[150];
    uint16_t receivedBufLength = 0;
    uint8_t messageType = 0x00, messageVersion = 0;
    TimeSyncPacketMessageType *messageTypeStruct = nullptr;
    
    if(!(isTimeSyncServiceStarted == true || isTimeSyncServiceClientStarted == true))
    {
        LogWarn("TSS HandleUdpReceive: Neither server (%d) nor client (%d) is started", isTimeSyncServiceStarted, isTimeSyncServiceClientStarted);
        return;
    }
        
    receivedBufLength = otMessageRead(aMessage, otMessageGetOffset(aMessage), receivedBuf, sizeof(receivedBuf) - 1);
    
#ifdef TIMESYNCSERVICE_VERBOSE_LOG
    for(uint16_t i = 0; i < receivedBufLength; i++)
    {
        LogDebg("TSS HandleUdpReceive: Received %u bytes, byte[%u] = 0x%x:", receivedBufLength, i, receivedBuf[i]);
    }
#endif
    
    // Validate buffer length
    if (receivedBufLength < TIMESYNC_MSG_TYPE_OFFSET || receivedBufLength >= sizeof(receivedBuf)) {
        LogWarn("TSS HandleUdpReceive: Invalid buffer length %u, valid range [%d, %lu)",
                     receivedBufLength, TIMESYNC_MSG_TYPE_OFFSET, (unsigned long)sizeof(receivedBuf));
        return;
    }
    
    // Extract the message type and version from the bitfield
    messageTypeStruct = reinterpret_cast<TimeSyncPacketMessageType *>(&receivedBuf[receivedBufLength - TIMESYNC_MSG_TYPE_OFFSET]);
    messageType = messageTypeStruct->messageType;
    messageVersion = messageTypeStruct->version;
    
    // Validate message type is within expected range
    if (messageType != TIMESYNC_PACKET_REQUEST && 
        messageType != TIMESYNC_PACKET_ACK && 
        messageType != TIMESYNC_PACKET) {
        LogWarn("TSS HandleUdpReceive: Invalid message type 0x%x received", messageType);
        return;
    }
    
    if(messageVersion != TIMESYNC_PROTOCOL_VERSION)
    {
        LogWarn("TSS HandleUdpReceive: Invalid version %d received, expecting %d",
            messageVersion, TIMESYNC_PROTOCOL_VERSION);
        return;
    }
    
    char peeripAddressString[OT_IP6_ADDRESS_STRING_SIZE];
    otIp6AddressToString(&(aMessageInfo->mPeerAddr), peeripAddressString, sizeof(peeripAddressString));
    
    char srcipAddressString[OT_IP6_ADDRESS_STRING_SIZE];
    otIp6AddressToString(&(aMessageInfo->mSockAddr), srcipAddressString, sizeof(srcipAddressString));
    
    if(messageType == TIMESYNC_PACKET_REQUEST) {
        
        if(receivedBufLength != sizeof(TimeSyncPacketRequest))
        {
            LogWarn("TSS HandleUdpReceive: wrong message length (%d) for TIMESYNC_PACKET_REQUEST (%lu)",
                receivedBufLength, (unsigned long)sizeof(TimeSyncPacketRequest));
            return;
        }

        LogInfo("TSS HandleUdpReceive: received TIMESYNC_PACKET_REQUEST messageType: 0x%x", messageType);
        LogInfo("TSS HandleUdpReceive: SRC %s:%d, DST %s:%d",
            peeripAddressString, aMessageInfo->mPeerPort,
            srcipAddressString, aMessageInfo->mSockPort);

        HandleTimeSyncPacketRequest(aMessage, aMessageInfo);
        
    } else if (messageType == TIMESYNC_PACKET_ACK) {
        
        if(receivedBufLength != sizeof(TimeSyncPacketAck))
        {
            LogInfo("TSS HandleUdpReceive: wrong message length (%d) for TIMESYNC_PACKET_ACK (%lu)",
                receivedBufLength, (unsigned long)sizeof(TimeSyncPacketAck));
            return;
        }

        LogInfo("TSS HandleUdpReceive: received TIMESYNC_PACKET_ACK messageType: 0x%x", messageType);
        LogInfo("TSS HandleUdpReceive: SRC %s:%d, DST %s:%d",
            peeripAddressString, aMessageInfo->mPeerPort,
            srcipAddressString, aMessageInfo->mSockPort);

        HandleTimeSyncPacketAck(aMessage, aMessageInfo);
    } else if (messageType == TIMESYNC_PACKET) {
        
        if(receivedBufLength != sizeof(TimeSyncPacket))
        {
            LogInfo("TSS HandleUdpReceive: wrong message length (%d) for TIMESYNC_PACKET (%lu)",
                receivedBufLength, (unsigned long)sizeof(TimeSyncPacket));
            return;
        }

        LogInfo("TSS HandleUdpReceive: received TIMESYNC_PACKET messageType: 0x%x", messageType);
        LogInfo("TSS HandleUdpReceive: SRC %s:%d, DST %s:%d",
            peeripAddressString, aMessageInfo->mPeerPort,
            srcipAddressString, aMessageInfo->mSockPort);

        HandleTimeSyncPacket(aMessage, aMessageInfo);
    }
    else {
        LogWarn("TSS HandleUdpReceive: wrong message type received 0x%x", messageType);
        return;
    }
}

TimeSyncServerHistory * TimeSyncService::getServerHistory(int &size)
{
    size = TSS_SERVER_HISTORY_ARRAY_SIZE;
    return timeSyncServerHistory;
}

TimeSyncClientHistory * TimeSyncService::getClientHistory(int &size)
{
    size = TSS_CLIENT_HISTORY_ARRAY_SIZE;
    return timeSyncClientHistory;
}

uint8_t TimeSyncService::GetTimeSyncServerHistorySize(void) {
    return TSS_SERVER_HISTORY_ARRAY_SIZE;
}

uint8_t TimeSyncService::GetTimeSyncClientHistorySize(void) {
    return TSS_CLIENT_HISTORY_ARRAY_SIZE;
}

void TimeSyncService::ClearHistory(void) {
    
    memset(reinterpret_cast<void *>(&timeSyncClientHistory), 0, sizeof(TimeSyncClientHistory)*TSS_CLIENT_HISTORY_ARRAY_SIZE);
    
    memset(reinterpret_cast<void *>(&timeSyncServerHistory), 0, sizeof(TimeSyncServerHistory)*TSS_SERVER_HISTORY_ARRAY_SIZE);
}

void TimeSyncService::startTimeSyncServiceClient(void)
{
    otNetifIdentifier netif = OT_NETIF_THREAD_INTERNAL;
    otSockAddr        sockaddr;
    otError error;
    
    if(isTimeSyncServiceStarted == true || isTimeSyncServiceClientStarted == true)
    {
        LogWarn("TSS startTimeSyncServiceClient: Either server (%d) or client (%d) is already started not starting again",
            isTimeSyncServiceStarted, isTimeSyncServiceClientStarted);
        return;
    }
    
    last_timestamp_at_server = 0x00;
    VerifyOrExit(mInstance != NULL, error = OT_ERROR_INVALID_STATE);
    VerifyOrExit(mSocket.mSockName.mPort == 0, error = OT_ERROR_INVALID_STATE);
    
    memset(reinterpret_cast<void *>(&sockaddr), 0, sizeof(sockaddr));
    memset(reinterpret_cast<void *>(timeSyncServerHistory), 0, sizeof(TimeSyncServerHistory)*TSS_SERVER_HISTORY_ARRAY_SIZE);

    VerifyOrExit(!otUdpIsOpen(mInstance, &mSocket), error = OT_ERROR_ALREADY);
    error = otUdpOpen(mInstance, &mSocket, HandleUdpReceive, this);
    
    if(error != OT_ERROR_NONE) {
        LogWarn("TSS startTimeSyncServiceClient: failed to open UDP with error = %d", error);
        goto exit;
    }
    
    LogInfo("TSS startTimeSyncServiceClient: opened UDP - port = %d, error = %d", mSocket.mSockName.mPort, error);
    
    error = otUdpBind(mInstance, &mSocket, &sockaddr, netif);
    
    if(error != OT_ERROR_NONE) {
        LogWarn("TSS startTimeSyncServiceClient: failed to bind UDP with error = %d", error);
        goto exit;
    }
    
    LogInfo("TSS startTimeSyncServiceClient: binded UDP - port = %d, error = %d", mSocket.mSockName.mPort, error);
    
    isTimeSyncServiceClientStarted = true;
    
    LogInfo("TSS startTimeSyncServiceClient: started TimeSync Client");
    
    exit:
    return;
}

void TimeSyncService::stopTimeSyncServiceClient(void)
{
    if(isTimeSyncServiceClientStarted == false)
    {
        LogWarn("TSS stopTimeSyncServiceClient is already stopped, not stopping again");
        return;
    }
    
    IgnoreReturnValue(otUdpClose(mInstance, &mSocket));
    memset(reinterpret_cast<void *>(&mSocket), 0, sizeof(mSocket));
    
    isTimeSyncServiceClientStarted = false;
    ClearHistory();
    LogInfo("TSS stopTimeSyncServiceClient: stopped TimeSync Client");
}

uint16_t TimeSyncService::getTimeSyncServiceClientPort(void)
{
    if (isTimeSyncServiceClientStarted) {
        return mSocket.mSockName.mPort;
    } else {
        return 0;
    }
}

void TimeSyncService::setServerConfig(otIp6Address aServerAddr, uint16_t aServerPort)
{
    mServerAddr = aServerAddr;
    mServerPort = aServerPort;
}

void TimeSyncService::getServerConfig(otIp6Address &aServerAddr, uint16_t &aServerPort)
{
    aServerAddr = mServerAddr;
    aServerPort = mServerPort;
}

void TimeSyncService::sendMessageToServer(void)
{
    uint8_t Buf[150];
    uint16_t BufLength = 0;
    uint8_t messageType = TIMESYNC_PACKET_REQUEST;
    TimeSyncPacketRequest timeSyncRequestMessage;
    otMessageInfo messageInfo;
    
    if(isTimeSyncServiceClientStarted == false)
    {
        LogWarn("TSS sendMessageToServer: Client is not yet started %d", isTimeSyncServiceClientStarted);
        return;
    }
    
    memset(reinterpret_cast<void *>(&messageInfo), 0, sizeof(otMessageInfo));
        
    char serveripAddressString[OT_IP6_ADDRESS_STRING_SIZE];
    otIp6AddressToString(&(mServerAddr), serveripAddressString, sizeof(serveripAddressString));
    
    memset(reinterpret_cast<void *>(&timeSyncRequestMessage), 0, sizeof(TimeSyncPacketRequest));
    clientSeqNum++;
    timeSyncRequestMessage.clientSeqNum = clientSeqNum;
    timeSyncRequestMessage.messageType = messageType;
    timeSyncRequestMessage.version = TIMESYNC_PROTOCOL_VERSION;
    
    BufLength = sizeof(TimeSyncPacketRequest);
    memcpy(Buf, &timeSyncRequestMessage, sizeof(TimeSyncPacketRequest));
    
    messageInfo.mPeerAddr = mServerAddr;
    messageInfo.mPeerPort = mServerPort;
    
    char srcIpAddressString[OT_IP6_ADDRESS_STRING_SIZE];
    otIp6AddressToString(&(messageInfo.mPeerAddr), srcIpAddressString, sizeof(srcIpAddressString));
    
    char dstServerIpAddressString[OT_IP6_ADDRESS_STRING_SIZE];
    otIp6AddressToString(&(messageInfo.mSockAddr), dstServerIpAddressString, sizeof(dstServerIpAddressString));
    
    LogInfo("TSS sendMessageToServer: trying to send TIMESYNC_PACKET_REQUEST");
    LogInfo("TSS sendMessageToServer: SRC %s:%d, DST %s:%d",
        srcIpAddressString, messageInfo.mSockPort, dstServerIpAddressString, messageInfo.mSockPort);
    
    SendTimeSyncPacket(Buf, BufLength, &messageInfo);
}

bool TimeSyncService::isTimeSyncPacket(const Message &aMessage)
{
    Ip6::Headers aHeaders;
    Error error = kErrorParse;

    if(timeSync_server_port)
    {
        if (aMessage.GetType() == Message::kTypeIp6)
        {
            SuccessOrExit(aHeaders.ParseFrom(aMessage));
            error = kErrorNone;
        }
        else if (aMessage.GetType() == Message::kType6lowpan)
        {
            uint16_t               offset;
            uint16_t               headerLength;
            Mac::Addresses         meshAddrs;
            Lowpan::MeshHeader     meshHeader;
            Lowpan::FragmentHeader fragmentHeader;
            
            SuccessOrExit(meshHeader.ParseFrom(aMessage, headerLength));
            meshAddrs.mSource.SetShort(meshHeader.GetSource());
            meshAddrs.mDestination.SetShort(meshHeader.GetDestination());
            offset = headerLength;
            
            if (fragmentHeader.ParseFrom(aMessage, offset, headerLength) == kErrorNone)
            {
                offset += headerLength;
                if (fragmentHeader.GetDatagramOffset() == 0)
                {
                    SuccessOrExit(aHeaders.DecompressFrom(aMessage, offset, meshAddrs));
                }
            }
            else
            {
                SuccessOrExit(aHeaders.DecompressFrom(aMessage, offset, meshAddrs));
            }
            error = kErrorNone;
        } else {
            LogWarn("TSS IsTimeSyncPacket: not a IPv6 or 6LoWPAN packet");
            return false;
        }

        LogDebg("TSS IsTimeSyncPacket: source port = %d, destination port = %d", 
                aHeaders.GetSourcePort(), aHeaders.GetDestinationPort());

        if(aHeaders.GetSourcePort() == timeSync_server_port || aHeaders.GetDestinationPort() == timeSync_server_port)
        {
            TimeSyncPacketMessageType MessageType;
            uint8_t value = 0xFF;
            uint16_t read_length = aMessage.ReadBytes(aMessage.GetLength() - TIMESYNC_MSG_TYPE_OFFSET, &value, 1);
            memcpy(&MessageType, &value, 1);

            OT_UNUSED_VARIABLE(read_length);

            LogDebg("TSS IsTimeSyncPacket: Message Type = %x, read_length = %d",
                MessageType.messageType, read_length);
            LogDebg("TSS IsTimeSyncPacket: message Length = %d, value = 0x%x",
                aMessage.GetLength(), value);
            
            if(MessageType.messageType == TIMESYNC_PACKET || MessageType.messageType == TIMESYNC_PACKET_ACK) {
                LogDebg("TSS IsTimeSyncPacket: Is a Time Sync Packet");
                return true;
            } else {
                LogDebg("TSS IsTimeSyncPacket: Is not a TimeSyncPacket, type = %d", MessageType.messageType);
                return false;
            }
        }
        LogDebg("TSS IsTimeSyncPacket: does not match time sync server port (%d)", timeSync_server_port);
        return false;
    }
    LogDebg("TSS IsTimeSyncPacket: no valid port (%d) configured", timeSync_server_port);
    return false;

exit:
    if (error && (aMessage.GetType() == Message::kTypeIp6 || aMessage.GetType() == Message::kType6lowpan))
    {
        LogWarn("Error while parsing IP6 headers from the message");
        return false;
    }
    return false;
}

void TimeSyncService::setConfiguredTimeSyncServerPortForPacketInspection(uint16_t aPort)
{
    timeSync_server_port = aPort;
}

uint16_t TimeSyncService::getConfiguredTimeSyncServerPortForPacketInspection(void)
{
    return timeSync_server_port;
}

void TimeSyncService::HandleNotifierEvents(Events aEvents)
{
    if(aEvents.ContainsAny(kEventThreadNetdataChanged) ||
    aEvents.ContainsAny(kEventThreadRoleChanged) || 
    aEvents.ContainsAny(kEventThreadRlocAdded) || 
    aEvents.ContainsAny(kEventThreadRlocRemoved))
    {
        LogDebg("TSS HandleNotifierEvents: handling event 0x%lx", (unsigned long)aEvents.GetAsFlags());
        checkAndBecomeTimeSyncServerorClient();
    }
}

void TimeSyncService::CalculateRadioDelay(const uint8_t * aFrame, uint16_t aFrameLen, Message *aMessage, uint32_t &radio_delay, uint64_t &time_now, const char *aLogContext)
{
    const uint8_t *        initialFrame    = aFrame;
    uint16_t               initialFrameLen = aFrameLen;

    #ifdef CONFIG_APPLE_DEBUG_VERBOSE
    for(int i=0; i<initialFrameLen; i++)
    {
        LogDebg("TSS CalculateRadioDelay %s: len = %d, payload[%d] = %x",
            aLogContext, initialFrameLen, i, initialFrame[i]);
    }
    #else
    OT_UNUSED_VARIABLE(aLogContext);
    #endif // CONFIG_APPLE_DEBUG_VERBOSE

    TimeSyncOTATimeStamp timeSyncOTATimeStamp;

    if(initialFrameLen < sizeof(TimeSyncOTATimeStamp))
    {
        LogDebg("TSS:initialFrameLen had value lesser than size of TimeSyncOTATimeStamp");
    }
    else
    {
        memcpy(&timeSyncOTATimeStamp, &initialFrame[initialFrameLen - sizeof(TimeSyncOTATimeStamp)], sizeof(TimeSyncOTATimeStamp));
    }
    uint64_t ota_delay = 0;
    
    if(timeSyncOTATimeStamp.f2ota_timeStamp > timeSyncOTATimeStamp.h2f_timeStamp)
    {
        ota_delay = timeSyncOTATimeStamp.f2ota_timeStamp - timeSyncOTATimeStamp.h2f_timeStamp;
        
        LogDebg("TSS CalculateRadioDelay %s: OTA Delay = " TSS_U64_HEX_FMT ", f2ota_timeStamp = %lu",
            aLogContext, TSS_U64_HEX_ARGS(ota_delay), (unsigned long)timeSyncOTATimeStamp.f2ota_timeStamp);
        LogDebg("TSS CalculateRadioDelay %s: h2f_timeStamp = %lu, timeOffset = %ld",
            aLogContext, (unsigned long)timeSyncOTATimeStamp.h2f_timeStamp, (long)timeSyncOTATimeStamp.timeOffset);
    }
    else {
        LogDebg("TSS CalculateRadioDelay %s: f2ota_timeStamp = %lu smaller than h2f_timeStamp = %lu",
            aLogContext, (unsigned long)timeSyncOTATimeStamp.f2ota_timeStamp, (unsigned long)timeSyncOTATimeStamp.h2f_timeStamp);
        LogDebg("TSS CalculateRadioDelay %s: OTA Delay = " TSS_U64_HEX_FMT, aLogContext, TSS_U64_HEX_ARGS(ota_delay));
    }

    uint64_t frame_rx_time;
    uint32_t f2h_radio_delay = 0;
    frame_rx_time = aMessage->GetRadioTime();
    time_now = otPlatRadioGetNow(mInstance);
    if(time_now > frame_rx_time) {
        f2h_radio_delay = (uint32_t)(time_now - frame_rx_time);}

    radio_delay = f2h_radio_delay + (uint32_t)ota_delay;

    LogDebg("TSS CalculateRadioDelay %s: f2h_radio_delay: %ld, ota_delay = " TSS_U64_HEX_FMT,
        aLogContext, (long)f2h_radio_delay, TSS_U64_HEX_ARGS(ota_delay));
    LogDebg("TSS CalculateRadioDelay %s: radio_delay = %ld", aLogContext, (long)radio_delay);
}

void TimeSyncService::HandleTimeSyncFrame(ot::Mac::TxFrame &aFrame)
{
    uint8_t * payload = NULL;
    uint16_t len = 0;
    payload = aFrame.GetPayload();
    len = aFrame.GetPayloadLength();
    ot::Mac::Address     srcAddr;
    ot::Mac::Address     dstAddr;
    uint16_t current_rloc16_in_payload = 0x0000, src_rloc16, dst_rloc16;
    uint8_t current_dirty_bit = 0, new_dirty_bit = 0, numHops = 0;
    uint64_t radio_time_now = 0, frame_rx_time = 0, timeStamp = 0, h2f_timeStamp = 0;
    uint64_t packet_f2ota_timeStamp = 0, packet_h2f_timeStamp = 0;
    uint32_t radio_delay = 0, delay = 0, host_delay = 0, scheduling_delay = 0, ota_delay = 0;
    TimeSyncPacketType packetType = static_cast<TimeSyncPacketType>(0);
    TimeSyncPacket timeSyncPacket;
    TimeSyncPacketAck timeSyncPacketAck;
    IgnoreError(aFrame.GetSrcAddr(srcAddr));
    IgnoreError(aFrame.GetDstAddr(dstAddr));

    LogDebg("TSS HandleTimeSyncFrame: handling time sync packet");

    #ifdef TIMESYNCSERVICE_VERBOSE_LOG
    for(int i=0; i<len; i++)
    {
        LogDebg("TSS HandleTimeSyncFrame: MAC Payload Before: len = %d, payload[%d] = %x",len, i, payload[i]);
    }
    #endif

    if(srcAddr.IsExtended() || dstAddr.IsExtended())
    {
        LogWarn("TSS HandleTimeSyncFrame: MAC Payload has extended address");
    }

    src_rloc16 = srcAddr.GetShort();
    dst_rloc16 = dstAddr.GetShort();

    // Validate payload and length before parsing message type
    if (payload == nullptr || len < TIMESYNC_MSG_TYPE_OFFSET) {
        LogWarn("TSS HandleTimeSyncFrame: Invalid payload (null=%s, len=%d, required=%d)", 
                payload ? "no" : "yes", len, TIMESYNC_MSG_TYPE_OFFSET);
        return;
    }

    TimeSyncPacketMessageType message_type = {};

    if(len < sizeof(TimeSyncPacket))
    {
        LogDebg("TSS:payload length had value lesser than size of TimeSyncPacket");
    }
    else
    {
        memcpy(&message_type, &payload[len - TIMESYNC_MSG_TYPE_OFFSET], sizeof(message_type));
    }

    packetType = static_cast<TimeSyncPacketType>(message_type.messageType);

    // Error condition: Validate message type is within expected range
    VerifyOrExit(packetType == TIMESYNC_PACKET || packetType == TIMESYNC_PACKET_ACK,
    LogDebg("TSS HandleTimeSyncFrame: Invalid message type (%d)", packetType));

    if(packetType == TIMESYNC_PACKET)
    {
        memcpy(&timeSyncPacket, &(payload[len-sizeof(TimeSyncPacket)]), sizeof(TimeSyncPacket));
        current_rloc16_in_payload = timeSyncPacket.rloc16;
        current_dirty_bit = timeSyncPacket.dirtyBit;
        delay = timeSyncPacket.timeOffset;
        timeStamp = timeSyncPacket.timeStamp;
        numHops = timeSyncPacket.numHops;
        packet_f2ota_timeStamp = timeSyncPacket.f2ota_timeStamp;
        packet_h2f_timeStamp = timeSyncPacket.h2f_timeStamp;
    } else if(packetType == TIMESYNC_PACKET_ACK) {
        memcpy(&timeSyncPacketAck, &(payload[len-sizeof(timeSyncPacketAck)]), sizeof(TimeSyncPacketAck));
        current_rloc16_in_payload = timeSyncPacketAck.rloc16;
        current_dirty_bit = timeSyncPacketAck.dirtyBit;
        delay = timeSyncPacketAck.timeOffset;
        timeStamp = timeSyncPacketAck.timeStamp;
        numHops = timeSyncPacketAck.numHops;
        packet_f2ota_timeStamp = timeSyncPacketAck.f2ota_timeStamp;
        packet_h2f_timeStamp = timeSyncPacketAck.h2f_timeStamp;
    }

    LogDebg("TSS HandleTimeSyncFrame: MAC Payload: packetType = %d, timeStamp = " TSS_U64_HEX_FMT,
        packetType, TSS_U64_HEX_ARGS(timeStamp));
    LogDebg("TSS HandleTimeSyncFrame: MAC Payload: is Dirty = %d, current Time Offset = %ld",
        current_dirty_bit, (long)delay);
        
    LogDebg("TSS HandleTimeSyncFrame: MAC Payload: src_addr = 0x%x, dst_addr = 0x%x",
        src_rloc16, dst_rloc16);
    LogDebg("TSS HandleTimeSyncFrame: MAC Payload: rloc16_in_payload = 0x%x, radio_time_stamp = " TSS_U64_HEX_FMT,
        current_rloc16_in_payload, TSS_U64_HEX_ARGS(aFrame.GetRxRadioTimestampForForwardingPacket()));

    //Dirty Bit
    if((current_rloc16_in_payload == 0x0000) || ((current_dirty_bit == 0x00) && (current_rloc16_in_payload == src_rloc16)))
    {
        new_dirty_bit = 0x00;
    }else
    {
        new_dirty_bit = 0x01;
    }

    //Number of traversed hops, radio delay in hopping scenarios
    radio_time_now = otPlatRadioGetNow(mInstance);
    if(current_rloc16_in_payload != 0x0000)
    {
        numHops += 1;
        
        frame_rx_time = aFrame.GetRxRadioTimestampForForwardingPacket();
        h2f_timeStamp = radio_time_now;
        radio_delay = 0;
        
        //Radio Delay
        if(radio_time_now > frame_rx_time)
        {
            radio_delay = (uint32_t)(radio_time_now - frame_rx_time);
        }
        else {
            LogDebg("TSS HandleTimeSyncFrame: Radio delay < 0, frame_rx_time = " TSS_U64_HEX_FMT, TSS_U64_HEX_ARGS(frame_rx_time));
            LogDebg("TSS HandleTimeSyncFrame: radio_time_now = " TSS_U64_HEX_FMT, TSS_U64_HEX_ARGS(radio_time_now));
        }
        
        //OTA Delay
        if(packet_f2ota_timeStamp > packet_h2f_timeStamp)
        {
            ota_delay = (uint32_t)(packet_f2ota_timeStamp - packet_h2f_timeStamp);
        }
        else {
            LogDebg("TSS HandleTimeSyncFrame: OTA delay < 0, packet_f2ota_timeStamp = " TSS_U64_HEX_FMT,
                TSS_U64_HEX_ARGS(packet_f2ota_timeStamp));
            LogDebg("TSS HandleTimeSyncFrame: packet_h2f_timeStamp = " TSS_U64_HEX_FMT, TSS_U64_HEX_ARGS(packet_h2f_timeStamp));
        }
        
        delay = delay + radio_delay + ota_delay;
        
        LogDebg("TSS HandleTimeSyncFrame: OTA delay + Hops, frame_rx_time = " TSS_U64_HEX_FMT ", radio_time_now = " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(frame_rx_time), TSS_U64_HEX_ARGS(radio_time_now));
        LogDebg("TSS HandleTimeSyncFrame: OTA+Hops, h2f_ts=" TSS_U64_HEX_FMT ", f2ota_ts=" TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(h2f_timeStamp), TSS_U64_HEX_ARGS(packet_f2ota_timeStamp));

        LogDebg("TSS HandleTimeSyncFrame: OTA delay + Hops, packet_h2f_timeStamp = " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(packet_h2f_timeStamp));
        LogDebg("TSS HandleTimeSyncFrame: OTA delay + Hops, radio_delay = %ld, OTA Delay = %lu",
            (long)radio_delay, (unsigned long)ota_delay);
        LogDebg("TSS HandleTimeSyncFrame: OTA delay + Hops, delay = %ld, numHops = %d",
            (long)delay, numHops);
    }

    //Host Delay
    if(current_rloc16_in_payload == 0x0000)
    {
        uint64_t current_host_time{0ULL};
        h2f_timeStamp = radio_time_now;

        current_host_time = radio_time_now;
        
        if (current_host_time > timeStamp)
        {
            host_delay = (uint32_t)(current_host_time - timeStamp);
        }
        
        delay += host_delay;
        
        LogDebg("TSS HandleTimeSyncFrame: After applying host delay, original host time = " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(timeStamp));
        LogDebg("TSS HandleTimeSyncFrame: After applying host delay, current host time = " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(current_host_time));

        LogDebg("TSS HandleTimeSyncFrame: After applying host delay, radio_time_now = " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(radio_time_now));
        LogDebg("TSS HandleTimeSyncFrame: After applying host delay, h2f_timeStamp = " TSS_U64_HEX_FMT,
            TSS_U64_HEX_ARGS(h2f_timeStamp));

        LogDebg("TSS HandleTimeSyncFrame: After applying host delay, host_delay = %ld, delay = %ld",
            (long)host_delay, (long)delay);
    }

    delay += scheduling_delay;


    if(packetType == TIMESYNC_PACKET)
    {
        timeSyncPacket.timeOffset = delay;
        timeSyncPacket.rloc16 = dst_rloc16;
        timeSyncPacket.dirtyBit = new_dirty_bit;
        timeSyncPacket.numHops = numHops;
        timeSyncPacket.h2f_timeStamp = (uint32_t)h2f_timeStamp;
        timeSyncPacket.f2ota_timeStamp = 0ULL;
        
        memcpy(&(payload[len-sizeof(TimeSyncPacket)]), &timeSyncPacket, sizeof(TimeSyncPacket));
    } else if (packetType == TIMESYNC_PACKET_ACK){
        timeSyncPacketAck.timeOffset = delay;
        timeSyncPacketAck.rloc16 = dst_rloc16;
        timeSyncPacketAck.dirtyBit = new_dirty_bit;
        timeSyncPacketAck.numHops = numHops;
        timeSyncPacketAck.h2f_timeStamp = (uint32_t)h2f_timeStamp;
        timeSyncPacketAck.f2ota_timeStamp = 0ULL;
        
        memcpy(&(payload[len-sizeof(timeSyncPacketAck)]), &timeSyncPacketAck, sizeof(TimeSyncPacketAck));
    }

    #ifdef TIMESYNCSERVICE_VERBOSE_LOG
    for(int i=0; i<len; i++)
    {
        LogDebg("TSS HandleTimeSyncFrame: MAC Payload After: len = %d, payload[%d] = %x",len, i, payload[i]);
    }
    #endif
    
    exit:
    return;
}

void TimeSyncService::SendTimeSyncMessage(void) {
    if(isClientStarted())
        sendMessageToServer();
    
    return;
}

void TimeSyncService::GetTimeSyncServiceStatus(bool &isClient, bool &isServer, bool &aisForcedServer) {
    
    isClient = isClientStarted();
    isServer = isServerStarted();
    aisForcedServer = isForcedServer();
    return;
}

void TimeSyncService::checkAndBecomeTimeSyncServerorClient() {

    otNetworkDataIterator   iterator = OT_NETWORK_DATA_ITERATOR_INIT;
    otServiceConfig         service_config;
    uint16_t                total_timeSync_anycast_services = 0;
    uint16_t timeSync_server_port_in_network = kTimeSyncInvalidPort;
    bool timeSyncServeravailableInNetwork = false;
    bool isThisDeviceTimeSyncServer = false;
    uint16_t my_rloc16 = otThreadGetRloc16(mInstance), timeSyncServerRloc16 = 0xFFFF;
    const otMeshLocalPrefix *my_mlp = otThreadGetMeshLocalPrefix(mInstance);
    otIp6Address serverRloc;
    bool forceServer = getForcedServer();
    otDeviceRole aRole = otThreadGetDeviceRole(mInstance);
    
    LogInfo("TSS checkAndBecome: Role = %d, forceServer = %d", aRole, forceServer);
    
    if(aRole == OT_DEVICE_ROLE_DISABLED || aRole == OT_DEVICE_ROLE_DETACHED)
    {
        if(isServerStarted()) {
            stopTimeSyncService();
            setConfiguredTimeSyncServerPortForPacketInspection(kTimeSyncInvalidPort);
            
            LogInfo("TSS checkAndBecome server section: stopped as server due to device Role : %d", aRole);

        } else if (isClientStarted()) {
            stopTimeSyncServiceClient();
            setConfiguredTimeSyncServerPortForPacketInspection(kTimeSyncInvalidPort);
            
            LogInfo("TSS checkAndBecome client section: stopped as client due to device Role : %d", aRole);
        }
        goto exit;
    }
    
    while (otNetDataGetNextService(mInstance, &iterator, &service_config) == OT_ERROR_NONE)
    {        
        if((service_config.mServiceDataLength == 3) && (service_config.mServiceData[0] == 0x03) && (service_config.mEnterpriseNumber == 63)) {
            total_timeSync_anycast_services++;
            timeSyncServeravailableInNetwork = true;
            
            if(timeSync_server_port_in_network == kTimeSyncInvalidPort)
            {
                timeSync_server_port_in_network = ((service_config.mServiceData[1] << 8) & 0xFF00) | ((service_config.mServiceData[2]) & 0x00FF);
                
                timeSyncServerRloc16  = service_config.mServerConfig.mRloc16;
            }
            
            if (my_rloc16 == service_config.mServerConfig.mRloc16)
            {
                isThisDeviceTimeSyncServer = true;
            }
        }
    }
    
    OT_UNUSED_VARIABLE(total_timeSync_anycast_services);

    LogInfo("TSS checkAndBecome common section: isServer = %d, isClient = %d",
        isServerStarted(), isClientStarted());
    LogInfo("TSS checkAndBecome common section: total_timeSync_anycast_services = %d",
        total_timeSync_anycast_services);

    LogInfo("TSS checkAndBecome common section: isThisDeviceTimeSyncServer = %d",
         isThisDeviceTimeSyncServer);
    LogInfo("TSS checkAndBecome common section: timeSyncServeravailableInNetwork = %d, server port in NW= %d",
        timeSyncServeravailableInNetwork, timeSync_server_port_in_network);

    LogInfo("TSS checkAndBecome common section: client/server port = %d", getTimeSyncServicePort());
    LogInfo("TSS checkAndBecome common section: configured server port for packet inspection = %d",
            getConfiguredTimeSyncServerPortForPacketInspection());
    
    if((!isServerStarted()) && (forceServer && (!timeSyncServeravailableInNetwork || isThisDeviceTimeSyncServer)))
    {
        if (isClientStarted()) {
            stopTimeSyncServiceClient();
        }
        
        startTimeSyncService();
        setConfiguredTimeSyncServerPortForPacketInspection(getTimeSyncServicePort());

        LogInfo("TSS checkAndBecome server section: started as server");
        
    } else if (isServerStarted() && (!forceServer)) {
        stopTimeSyncService();
        setConfiguredTimeSyncServerPortForPacketInspection(kTimeSyncInvalidPort);
        
        LogInfo("TSS checkAndBecome server section: stopped as server as device is no more forced to be server");

    } else if (isServerStarted() && forceServer && !isThisDeviceTimeSyncServer) {
        stopTimeSyncService();
        setConfiguredTimeSyncServerPortForPacketInspection(kTimeSyncInvalidPort);
        
        LogInfo("TSS checkAndBecome server section: stopped as server as service is removed in net data");
        LogInfo("TSS checkAndBecome server section: isServerStarted = %d, isClientStarted = %d",
            isServerStarted(), isClientStarted());
            
        LogInfo("TSS checkAndBecome server section: server port = %d", getTimeSyncServicePort());
        LogInfo("TSS checkAndBecome server section: configured server port for packet inspection = %d",
            getConfiguredTimeSyncServerPortForPacketInspection());
        LogInfo("TSS checkAndBecome server section: isThisDeviceTimeSyncServer = %d",
            isThisDeviceTimeSyncServer);
        
        startTimeSyncService();
        setConfiguredTimeSyncServerPortForPacketInspection(getTimeSyncServicePort());
        
        LogInfo("TSS checkAndBecome server section: re started as server as service is removed in net data");
    }

    LogInfo("TSS checkAndBecome end of server section: isServerStarted = %d, isClientStarted = %d",
        isServerStarted(), isClientStarted());

    LogInfo("TSS checkAndBecome end of server section: server port = %d", getTimeSyncServicePort());
    LogInfo("TSS checkAndBecome end of server section: configured server port for packet inspection = %d",
             getConfiguredTimeSyncServerPortForPacketInspection());
    
    if((!isClientStarted()) && (!isServerStarted()) && timeSyncServeravailableInNetwork && timeSync_server_port_in_network != kTimeSyncInvalidPort)
    {
        startTimeSyncServiceClient();
        
        serverRloc.mFields.mComponents.mIid.mFields.m32[0] = ot::BigEndian::HostSwap32(0x000000ff);
        serverRloc.mFields.mComponents.mIid.mFields.m16[2] = ot::BigEndian::HostSwap16(0xfe00);
        serverRloc.mFields.mComponents.mIid.mFields.m16[3] = ot::BigEndian::HostSwap16(timeSyncServerRloc16);
        serverRloc.mFields.mComponents.mNetworkPrefix = (*my_mlp);
        setServerConfig(serverRloc, timeSync_server_port_in_network);
        
        setConfiguredTimeSyncServerPortForPacketInspection(timeSync_server_port_in_network);
        
        char serveripAddressString[OT_IP6_ADDRESS_STRING_SIZE];
        otIp6AddressToString(&(serverRloc), serveripAddressString, sizeof(serveripAddressString));
        
        LogInfo("TSS checkAndBecome client section: started as client, server %s:%d",
            serveripAddressString, timeSync_server_port_in_network);
        
    } else if(isClientStarted() && (!timeSyncServeravailableInNetwork || timeSync_server_port_in_network == kTimeSyncInvalidPort)) {
        stopTimeSyncServiceClient();
        setConfiguredTimeSyncServerPortForPacketInspection(kTimeSyncInvalidPort);
        
        LogInfo("TSS checkAndBecome client section: stopped as client");
    }
    
    if(isClientStarted() && timeSyncServeravailableInNetwork && timeSync_server_port_in_network != kTimeSyncInvalidPort)
    {
        setConfiguredTimeSyncServerPortForPacketInspection(timeSync_server_port_in_network);
        
        serverRloc.mFields.mComponents.mIid.mFields.m32[0] = ot::BigEndian::HostSwap32(0x000000ff);
        serverRloc.mFields.mComponents.mIid.mFields.m16[2] = ot::BigEndian::HostSwap16(0xfe00);
        serverRloc.mFields.mComponents.mIid.mFields.m16[3] = ot::BigEndian::HostSwap16(timeSyncServerRloc16);
        serverRloc.mFields.mComponents.mNetworkPrefix = (*my_mlp);
        setServerConfig(serverRloc, timeSync_server_port_in_network);
        
        LogInfo("TSS checkAndBecome: set server valid port for client");

    } else if (!isClientStarted() && !isServerStarted()) {
        
        setConfiguredTimeSyncServerPortForPacketInspection(kTimeSyncInvalidPort);
        
        LogInfo("TSS checkAndBecome: set invalid server port as DUT is no more client/server");
    }
    
    exit:
        LogInfo("TSS checkAndBecome: Final status Role=%d, server=%d, client=%d, netAvail=%d",
            aRole, isServerStarted(), isClientStarted(), timeSyncServeravailableInNetwork);
            
        LogInfo("TSS checkAndBecome: Final ports NW=%d, inspect=%d, actual=%d",
            timeSync_server_port_in_network, getConfiguredTimeSyncServerPortForPacketInspection(),
            getTimeSyncServicePort());
}

void TimeSyncService::forceStartAsTimeSyncServer(void) {
    LogDebg("TSS forceStartAsTimeSyncServer");
    setForcedServer(true);
    checkAndBecomeTimeSyncServerorClient();
}

void TimeSyncService::forceStopAsTimeSyncServer(void) {
    LogDebg("TSS forceStopAsTimeSyncServer");
    setForcedServer(false);
    checkAndBecomeTimeSyncServerorClient();
}

void TimeSyncService::GetTimeSyncServiceServerStatus(uint16_t &aServerPort, uint16_t &aConfiguredServerPortForPacketInspection) {
    
    if(isServerStarted()) {
        aServerPort = getTimeSyncServicePort();
        aConfiguredServerPortForPacketInspection = timeSync_server_port;
    }
    return;
}

void TimeSyncService::GetTimeSyncServiceClientStatus(uint16_t &aServerPort, uint16_t &aClientPort, otIp6Address &aServerAddr, uint16_t &aConfiguredServerPortForPacketInspection) {
    
    if(isClientStarted()) {
        aClientPort = getTimeSyncServiceClientPort();
        getServerConfig(aServerAddr, aServerPort);
        aConfiguredServerPortForPacketInspection = timeSync_server_port;
    }
    return;
}

char  tss_history[TSS_MAX_HISTOGRAM_STRING_LENGTH];
char *TimeSyncService::GetTimeSyncServerHistory(void) {
    uint16_t strCnt = 0;
    int size = 0;
    memset(tss_history, 0, TSS_MAX_HISTOGRAM_STRING_LENGTH);
    TimeSyncServerHistory *tss_ServerHistory = getServerHistory(size);
    
    for (uint8_t idx = 0; idx < size; idx++)
    {
        if(tss_ServerHistory[idx].isValid)
        {
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "[Server History Item : %d START\n", idx);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  isValid: %d, Client SN: %u, Server SN: %u\n", tss_ServerHistory[idx].isValid, tss_ServerHistory[idx].clientSeqNum, tss_ServerHistory[idx].serverSeqNum);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  numHops: %u\n", tss_ServerHistory[idx].numHops);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  dirty_bit: %d\n", tss_ServerHistory[idx].dirty_bit);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  timeOffset: %lu us\n", (unsigned long)tss_ServerHistory[idx].timeOffset);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  radioDelay: %lu us\n", (unsigned long)tss_ServerHistory[idx].radioDelay);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  host_delay: %lu us\n", (unsigned long)tss_ServerHistory[idx].host_delay);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  TSP timeOffset: %lu us\n", (unsigned long)tss_ServerHistory[idx].tsp_timeOffset);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  TSP radioDelay: %lu us\n", (unsigned long)tss_ServerHistory[idx].tsp_radioDelay);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  TSP host_delay: %lu us\n", (unsigned long)tss_ServerHistory[idx].tsp_host_delay);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  TSP dirty_bit: %u us\n", tss_ServerHistory[idx].tsp_dirty_bit);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  TSP numHops: %u us\n", tss_ServerHistory[idx].tsp_numHops);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  E2E Delay: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ServerHistory[idx].e2e_delay));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  E2E Time Sync Error: " TSS_I64_HEX_FMT " us\n", TSS_I64_HEX_ARGS(tss_ServerHistory[idx].e2e_error));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  clientTimeStamp: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ServerHistory[idx].clientTimeStamp));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  server_client_time_delta: " TSS_I64_HEX_FMT " us\n", TSS_I64_HEX_ARGS(tss_ServerHistory[idx].server_client_time_delta));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  orig_server_time: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ServerHistory[idx].orig_server_time));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  current_server_time: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ServerHistory[idx].current_server_time));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  calculated_client_time: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ServerHistory[idx].calculated_client_time));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  client_drift_in_ppm: %d ppm\n", (int)tss_ServerHistory[idx].client_drift_in_ppm);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  clientIpAddress: %s\n", tss_ServerHistory[idx].clientIpAddressString);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "Server History Item : %d END]\n", idx);
        }
    }
    
    return tss_history;
}

char *TimeSyncService::GetTimeSyncServerHistory(uint8_t idx) {
    uint16_t strCnt = 0;
    int size = 0;
    memset(tss_history, 0, TSS_MAX_HISTOGRAM_STRING_LENGTH);
    TimeSyncServerHistory *tss_ServerHistory = getServerHistory(size);
    
    if(tss_ServerHistory[idx].isValid)
    {
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "[Server History Item : %d START\n", idx);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  isValid: %d, Client SN: %u, Server SN: %u\n", tss_ServerHistory[idx].isValid, tss_ServerHistory[idx].clientSeqNum, tss_ServerHistory[idx].serverSeqNum);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  numHops: %u\n", tss_ServerHistory[idx].numHops);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  dirty_bit: %d\n", tss_ServerHistory[idx].dirty_bit);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  timeOffset: %lu us\n", (unsigned long)tss_ServerHistory[idx].timeOffset);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  radioDelay: %lu us\n", (unsigned long)tss_ServerHistory[idx].radioDelay);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  host_delay: %lu us\n", (unsigned long)tss_ServerHistory[idx].host_delay);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  TSP timeOffset: %lu us\n", (unsigned long)tss_ServerHistory[idx].tsp_timeOffset);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  TSP radioDelay: %lu us\n", (unsigned long)tss_ServerHistory[idx].tsp_radioDelay);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  TSP host_delay: %lu us\n", (unsigned long)tss_ServerHistory[idx].tsp_host_delay);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  TSP dirty_bit: %u us\n", tss_ServerHistory[idx].tsp_dirty_bit);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  TSP numHops: %u us\n", tss_ServerHistory[idx].tsp_numHops);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  E2E Delay: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ServerHistory[idx].e2e_delay));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  E2E Time Sync Error: " TSS_I64_HEX_FMT " us\n", TSS_I64_HEX_ARGS(tss_ServerHistory[idx].e2e_error));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  clientTimeStamp: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ServerHistory[idx].clientTimeStamp));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  server_client_time_delta: " TSS_I64_HEX_FMT " us\n", TSS_I64_HEX_ARGS(tss_ServerHistory[idx].server_client_time_delta));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  orig_server_time: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ServerHistory[idx].orig_server_time));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  current_server_time: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ServerHistory[idx].current_server_time));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  calculated_client_time: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ServerHistory[idx].calculated_client_time));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  client_drift_in_ppm: %d ppm\n", (int)tss_ServerHistory[idx].client_drift_in_ppm);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  clientIpAddress: %s\n", tss_ServerHistory[idx].clientIpAddressString);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "Server History Item : %d END]\n", idx);
    }
    
    return tss_history;
}

char *TimeSyncService::getTimeSyncClientHistory(void) {
    uint16_t strCnt = 0;
    int size = 0;
    memset(tss_history, 0, TSS_MAX_HISTOGRAM_STRING_LENGTH);
    TimeSyncClientHistory *tss_ClientHistory = getClientHistory(size);
    
    for (uint8_t idx = 0; idx < size; idx++)
    {
        if(tss_ClientHistory[idx].isValid)
        {
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "[Client History Item : %d START\n", idx);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  isValid: %d, Client SN: %u, Server SN: %u\n", tss_ClientHistory[idx].isValid, tss_ClientHistory[idx].clientSeqNum, tss_ClientHistory[idx].serverSeqNum);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  numHops: %u\n", tss_ClientHistory[idx].numHops);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  dirty_bit: %d\n", tss_ClientHistory[idx].dirty_bit);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  timeOffset: %lu us\n", (unsigned long)tss_ClientHistory[idx].timeOffset);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  radioDelay: %lu us\n", (unsigned long)tss_ClientHistory[idx].radioDelay);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  host_delay: %lu us\n", (unsigned long)tss_ClientHistory[idx].host_delay);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  serverTimeStamp: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ClientHistory[idx].serverTimeStamp));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  client_server_time_delta: " TSS_I64_HEX_FMT " us\n", TSS_I64_HEX_ARGS(tss_ClientHistory[idx].client_server_time_delta));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  current_client_time: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ClientHistory[idx].current_client_time));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  calculated_server_time: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ClientHistory[idx].calculated_server_time));
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  serverIpAddress: %s\n", tss_ClientHistory[idx].serverIpAddressString);
            strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "Client History Item : %d END]\n", idx);
        }
    }
    
    return tss_history;
}

char *TimeSyncService::getTimeSyncClientHistory(uint8_t idx) {
    uint16_t strCnt = 0;
    int size = 0;
    memset(tss_history, 0, TSS_MAX_HISTOGRAM_STRING_LENGTH);
    TimeSyncClientHistory *tss_ClientHistory = getClientHistory(size);
    
    if(tss_ClientHistory[idx].isValid)
    {
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "[Client History Item : %d START\n", idx);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  isValid: %d, Client SN: %u, Server SN: %u\n", tss_ClientHistory[idx].isValid, tss_ClientHistory[idx].clientSeqNum, tss_ClientHistory[idx].serverSeqNum);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  numHops: %u\n", tss_ClientHistory[idx].numHops);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  dirty_bit: %d\n", tss_ClientHistory[idx].dirty_bit);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  timeOffset: %lu us\n", (unsigned long)tss_ClientHistory[idx].timeOffset);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  radioDelay: %lu us\n", (unsigned long)tss_ClientHistory[idx].radioDelay);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  host_delay: %lu us\n", (unsigned long)tss_ClientHistory[idx].host_delay);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  serverTimeStamp: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ClientHistory[idx].serverTimeStamp));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  client_server_time_delta: " TSS_I64_HEX_FMT " us\n", TSS_I64_HEX_ARGS(tss_ClientHistory[idx].client_server_time_delta));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  current_client_time: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ClientHistory[idx].current_client_time));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  calculated_server_time: " TSS_U64_HEX_FMT " us\n", TSS_U64_HEX_ARGS(tss_ClientHistory[idx].calculated_server_time));
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "  serverIpAddress: %s\n", tss_ClientHistory[idx].serverIpAddressString);
        strCnt += snprintf(tss_history + strCnt, sizeof(tss_history) - strCnt, "Client History Item : %d END]\n", idx);
    }
    
    return tss_history;
}

void TimeSyncService::TimeSyncClearHistory(void) {
    
    ClearHistory();
}

}
}
#endif