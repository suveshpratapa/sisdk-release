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
 *   This file implements a simple CLI for the Time Sync Service.
 */

#include "cli/cli_timeSyncService.hpp"

#if FEATURE_TIMESYNCSERVICE_ENABLE
#include "cli/cli.hpp"
#include "common/encoding.hpp"
#include <openthread/timeSyncService_api.h>

namespace ot {
namespace Cli {

TimeSyncService::TimeSyncService(otInstance *aInstance, OutputImplementer &aOutputImplementer)
    : Utils(aInstance, aOutputImplementer)
{
}

template <> otError TimeSyncService::Process<Cmd("status")>(Arg aArgs[])
{
    if (aArgs[0].IsEmpty()){
        bool isClient = false, isServer = false, isForcedServer = false;
        otGetTimeSyncServiceStatus(GetInstancePtr(), &isClient, &isServer, &isForcedServer);
        OutputLine("Started as server : %d, or client %d, forcedServer = %d \n", isServer, isClient, isForcedServer);
        
        if(isClient) {
            uint16_t aServerPort = 0x0, aClientPort = 0x0, aConfiguredServerPortForPacketInspection = 0x0;
            otIp6Address aServerAddr;
            otGetTimeSyncServiceClientStatus(GetInstancePtr(), &aServerPort, &aClientPort, &aServerAddr, &aConfiguredServerPortForPacketInspection);
            
            char serveripAddressString[OT_IP6_ADDRESS_STRING_SIZE];
            otIp6AddressToString(&(aServerAddr), serveripAddressString, sizeof(serveripAddressString));
            
            OutputLine("Client Status \n  client port = %d \n  server port = %d \n  Configured App Port for Server = %d \n  Server Addr = %s \n", aClientPort, aServerPort, aConfiguredServerPortForPacketInspection, serveripAddressString);
        } else if (isServer) {
            uint16_t aServerPort = 0x0, aConfiguredServerPortForPacketInspection = 0x0;
            otGetTimeSyncServiceServerStatus(GetInstancePtr(), &aServerPort, &aConfiguredServerPortForPacketInspection);
            
            OutputLine("Server Status \n  Server port = %d \n  Configured App Port for Server = %d \n", aServerPort, aConfiguredServerPortForPacketInspection);
        }
    }
    
    return OT_ERROR_NONE;
}

template <> otError TimeSyncService::Process<Cmd("gettimesync")>(Arg aArgs[])
{
    OT_UNUSED_VARIABLE(aArgs);
    otSendTimeSyncMessage(GetInstancePtr());
    return OT_ERROR_NONE;
}

template <> otError TimeSyncService::Process<Cmd("forceserver")>(Arg aArgs[])
{
    if (aArgs[0] == "start")
    {
        otForceStartAsTimeSyncServer(GetInstancePtr());
    } else if (aArgs[0] == "stop") {
        otForceStopAsTimeSyncServer(GetInstancePtr());
    } else {
        return OT_ERROR_INVALID_ARGS;
    }
    
    return OT_ERROR_NONE;
}

template <> otError TimeSyncService::Process<Cmd("getclienthistory")>(Arg aArgs[])
{
    OT_UNUSED_VARIABLE(aArgs);
    uint8_t history_size = otGetTimeSyncClientHistorySize(GetInstancePtr());

    OutputLine("Client History Size = %d \n", history_size);
    for (uint8_t idx = 0; idx < history_size; idx++)
    {   
        char * history = otGetTimeSyncClientHistoryAtIndex(GetInstancePtr(), idx);
        if(history)
            OutputLine("%s", history);
    }
    
    return OT_ERROR_NONE;
}

template <> otError TimeSyncService::Process<Cmd("getserverhistory")>(Arg aArgs[])
{
    OT_UNUSED_VARIABLE(aArgs);
    uint8_t history_size = otGetTimeSyncServerHistorySize(GetInstancePtr());

    OutputLine("Server History Size = %d \n", history_size);
    for (uint8_t idx = 0; idx < history_size; idx++)
    {   
        char * history = otGetTimeSyncServerHistoryAtIndex(GetInstancePtr(), idx);
        if(history)
            OutputLine("%s", history);
    }
    
    return OT_ERROR_NONE;
}

template <> otError TimeSyncService::Process<Cmd("clearhistory")>(Arg aArgs[])
{
    OT_UNUSED_VARIABLE(aArgs);
    otTimeSyncClearHistory(GetInstancePtr());

    return OT_ERROR_NONE;
}

otError TimeSyncService::Process(Arg aArgs[])
{
#define CmdEntry(aCommandString) {aCommandString, &TimeSyncService::Process<Cmd(aCommandString)>}

    static constexpr Command kCommands[] = {
        CmdEntry("clearhistory"), CmdEntry("forceserver"), CmdEntry("getclienthistory"),
        CmdEntry("getserverhistory"), CmdEntry("gettimesync"), CmdEntry("status"),
    };

    static_assert(BinarySearch::IsSorted(kCommands), "kCommands is not sorted");

    otError        error = OT_ERROR_INVALID_COMMAND;
    const Command *command;

    if (aArgs[0].IsEmpty() || (aArgs[0] == "help"))
    {
        OutputCommandTable(kCommands);
        ExitNow(error = aArgs[0].IsEmpty() ? error : OT_ERROR_NONE);
    }

    command = BinarySearch::Find(aArgs[0].GetCString(), kCommands);
    VerifyOrExit(command != nullptr);

    error = (this->*command->mHandler)(aArgs + 1);

exit:
    return error;
}

}
}

#endif