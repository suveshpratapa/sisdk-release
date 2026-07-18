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
 * @brief
 *  This file defines the OpenThread APIs for Time Sync Service.
 */

#ifndef OPENTHREAD_TIMESYNCSERVICE_API_H_
#define OPENTHREAD_TIMESYNCSERVICE_API_H_

#include <openthread/ip6.h>

#ifdef __cplusplus
extern "C" {
#endif

#if FEATURE_TIMESYNCSERVICE_ENABLE

/**
 * Sends Time Sync Request message to the server
 *
 * @param[in]  otInstance    Instance pointer
 *
 * @retval None
 */
void otSendTimeSyncMessage(otInstance *aInstance);

/**
 * Retrieves Time Sync Service status
 *
 * @param[in]  otInstance    Instance pointer
 * @param[out]  isClient [ Clinet status], isServer [Server status], isForcedServer [Status if the device is forced to be a server]
 *
 * @retval None
 */
void otGetTimeSyncServiceStatus(otInstance *aInstance, bool *isClient, bool *isServer, bool *isForcedServer);

/**
 * Sets the Time Sync Service config to force a device to be server
 *
 * @param[in]  otInstance    Instance pointer
 *
 * @retval None
 */
void otForceStartAsTimeSyncServer(otInstance *aInstance);

/**
 * Remove the Time Sync Service config to force a device to be server
 *
 * @param[in]  otInstance    Instance pointer
 *
 * @retval None
 */
void otForceStopAsTimeSyncServer(otInstance *aInstance);

/**
 * Retrieves Time Sync Server status
 *
 * @param[in]  otInstance    Instance pointer
 * @param[out]  aServerPort [ Server Port], aConfiguredServerPortForPacketInspection [Port configured to inspact for Time Sync Service Packets]
 *
 * @retval None
 */
void otGetTimeSyncServiceServerStatus(otInstance *aInstance, uint16_t *aServerPort, uint16_t *aConfiguredServerPortForPacketInspection);

/**
 * Retrieves Time Sync Client status
 *
 * @param[in]  otInstance    Instance pointer
 * @param[out]  aServerPort [ Server Port], aClientPort [Client Port], aServerAddr [RLOC IP address of the server], aConfiguredServerPortForPacketInspection [Port configured to inspact for Time Sync Service Packets]
 *
 * @retval None
 */
void otGetTimeSyncServiceClientStatus(otInstance *aInstance, uint16_t *aServerPort, uint16_t *aClientPort, otIp6Address *aServerAddr, uint16_t *aConfiguredServerPortForPacketInspection);

/**
 * Retrieves Time Sync Server history
 *
 * @param[in]  otInstance    Instance pointer
 *
 * @retval History in string format
 */
char *otGetTimeSyncServerHistory(otInstance *aInstance);

/**
 * Retrieves Time Sync Client history
 *
 * @param[in]  otInstance    Instance pointer
 *
 * @retval History in string format
 */
char *otGetTimeSyncClientHistory(otInstance *aInstance);

/**
 * Clears Time Sync Server/Client history
 *
 * @param[in]  otInstance    Instance pointer
 *
 * @retval History in string format
 */
void otTimeSyncClearHistory(otInstance *aInstance);

/**
 * Retrieves Time Sync Server history for a give server history index
 *
 * @param[in]  otInstance    Instance pointer
 * @param[in]  idx    Index of server history
 *
 * @retval History in string format
 */
char *otGetTimeSyncServerHistoryAtIndex(otInstance *aInstance, uint8_t idx);

/**
 * Retrieves Time Sync Client history for a give Client history index
 *
 * @param[in]  otInstance    Instance pointer
 * @param[in]  idx    Index of client history
 *
 * @retval History in string format
 */
char *otGetTimeSyncClientHistoryAtIndex(otInstance *aInstance, uint8_t idx);

/**
 * Retrieves Time Sync Server history size
 *
 * @param[in]  otInstance    Instance pointer
 *
 * @retval History size
 */
uint8_t otGetTimeSyncServerHistorySize(otInstance *aInstance);

/**
 * Retrieves Time Sync Client history size
 *
 * @param[in]  otInstance    Instance pointer
 *
 * @retval History size
 */
uint8_t otGetTimeSyncClientHistorySize(otInstance *aInstance);

#endif // FEATURE_TIMESYNCSERVICE_ENABLE

#ifdef __cplusplus
} // end of extern "C"
#endif

#endif // OPENTHREAD_TIMESYNCSERVICE_API_H_