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
 *   This file implements packet data structures for Time Sync Service.
 */

#ifndef TimeSyncPacket_h
#define TimeSyncPacket_h

#include "openthread-core-config.h"

#if FEATURE_TIMESYNCSERVICE_ENABLE
 #include <stdio.h>
 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>
#include <openthread/udp.h>

namespace ot {
namespace TimeSyncService {

#define TIME_SYNC_SERVICE_FORCED_SERVER "ForcedServer"
#define TIMESYNC_MSG_TYPE_OFFSET 5
#define TIMESYNC_PROTOCOL_VERSION 0

constexpr uint16_t kTimeSyncInvalidPort = 0;

typedef enum TimeSyncPacketType {
    TIMESYNC_PACKET_REQUEST = 1,
    TIMESYNC_PACKET = 2,
    TIMESYNC_PACKET_ACK = 3,
} TimeSyncPacketType;

typedef struct __attribute__((__packed__)) TimeSyncPacketMessageType{
    uint8_t messageType:3;
    uint8_t version:3;
    uint8_t reserved:2;
} TimeSyncPacketMessageType;

typedef struct __attribute__((__packed__)) TimeSyncOTATimeStamp{
    uint32_t timeOffset;
    uint32_t h2f_timeStamp; //Host timestmap when frame is given to FW
    uint32_t reserved;
    uint32_t f2ota_timeStamp; //FW time stamp when frame is actually Txed
    //16
} TimeSyncOTATimeStamp;

typedef struct __attribute__((__packed__)) TimeSyncPacketRequest{
    uint8_t messageType:3;
    uint8_t version:3;
    uint8_t reserved1:2;
    uint16_t clientSeqNum;
    uint16_t reserved2;
    //5
} TimeSyncPacketRequest;

typedef struct __attribute__((__packed__)) TimeSyncPacket{
    uint16_t serverSeqNum;
    uint16_t clientSeqNum;
    uint64_t timeStamp;
    uint32_t timeOffset;
    uint16_t tz_offset_min; //Tize Zone in referecne to GMT in minutes
    uint32_t h2f_timeStamp; //Host timestmap when frame is given to FW
    uint16_t rloc16;
    uint8_t numHops:4;
    uint8_t reserved2:4;
    uint8_t messageType:3;
    uint8_t version:3;
    uint8_t dirtyBit:1;
    uint8_t reserved1:1;
    uint32_t f2ota_timeStamp; //FW time stamp when frame is actually Txed
    //30
} TimeSyncPacket;

typedef struct __attribute__((__packed__)) TimeSyncPacketAck{
    uint16_t serverSeqNum;
    uint16_t clientSeqNum;
    int16_t client_drift_in_ppm;
    uint32_t tsp_timeOffset;
    uint32_t tsp_radioDelay;
    uint32_t tsp_host_delay;
    uint64_t timeStamp;
    uint32_t timeOffset;
    uint32_t h2f_timeStamp; //Host timestmap when frame is given to FW
    uint16_t rloc16;
    uint8_t tsp_numHops:4;
    uint8_t numHops:4;
    uint8_t messageType:3;
    uint8_t version:3;
    uint8_t dirtyBit:1;
    uint8_t tsp_dirtyBit:1;
    uint32_t f2ota_timeStamp; //FW time stamp when frame is actually Txed
    //42
} TimeSyncPacketAck;

/* HISTORY TRACKER */

typedef struct TimeSyncServerHistory { /* this is based on Server receiving TIMESYNC_PACKET_ACK */
    bool isValid;
    uint16_t serverSeqNum;
    uint16_t clientSeqNum;
    uint64_t clientTimeStamp;
    uint32_t timeOffset;
    uint32_t radioDelay;
    uint32_t host_delay;
    uint8_t numHops;
    uint8_t dirty_bit;
    uint64_t e2e_delay; //only valid if ACK is also going through time adjustment
    int64_t e2e_error;
    int64_t server_client_time_delta;
    uint64_t orig_server_time;
    uint64_t current_server_time;
    uint64_t calculated_client_time;
    uint32_t tsp_timeOffset;
    uint32_t tsp_radioDelay;
    uint32_t tsp_host_delay;
    uint8_t tsp_dirty_bit;
    uint8_t tsp_numHops;
    int16_t client_drift_in_ppm;
    char clientIpAddressString[OT_IP6_ADDRESS_STRING_SIZE];
} TimeSyncServerHistory;

typedef struct TimeSyncClientHistory { /* this is based on Client receiving TIMESYNC_PACKET */
    bool isValid;
    uint16_t serverSeqNum;
    uint16_t clientSeqNum;
    uint64_t serverTimeStamp;
    uint32_t timeOffset;
    uint32_t radioDelay;
    uint32_t host_delay;
    uint8_t numHops;
    uint8_t dirty_bit;
    int64_t client_server_time_delta;
    uint64_t current_client_time;
    uint64_t calculated_server_time;
    char serverIpAddressString[OT_IP6_ADDRESS_STRING_SIZE];
} TimeSyncClientHistory;

}
}
#endif
#endif /* TimeSyncPacket_h */
