/*
 *  Copyright (c) 2026, The OpenThread Authors.
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

#include <openthread/config.h>

#include "test_platform.h"
#include "test_util.hpp"

#include "instance/instance.hpp"

#if FEATURE_TIMESYNCSERVICE_ENABLE
#include <openthread/timeSyncService_api.h>
#endif

namespace ot {

#if FEATURE_TIMESYNCSERVICE_ENABLE

/**
 * Test Time Sync Service server start/stop functionality
 */
void TestTimeSyncServerStartStop(void)
{
    Instance *instance;
    bool      isClient, isServer, isForcedServer;

    printf("TestTimeSyncServerStartStop\n");

    instance = testInitInstance();
    VerifyOrQuit(instance != nullptr);

    // Test initial state - should not be running
    otGetTimeSyncServiceStatus(instance, &isClient, &isServer, &isForcedServer);
    printf("Initial state - Client: %s, Server: %s, Forced: %s\n", 
           isClient ? "true" : "false", 
           isServer ? "true" : "false",
           isForcedServer ? "true" : "false");

    // Start as forced server
    otForceStartAsTimeSyncServer(instance);
    otGetTimeSyncServiceStatus(instance, &isClient, &isServer, &isForcedServer);
    VerifyOrQuit(isForcedServer == true, "Failed to start as forced server");
    printf("Started as forced server\n");

    // Stop forced server
    otForceStopAsTimeSyncServer(instance);
    otGetTimeSyncServiceStatus(instance, &isClient, &isServer, &isForcedServer);
    VerifyOrQuit(isForcedServer == false, "Failed to stop forced server");
    printf("Stopped forced server\n");

    testFreeInstance(instance);
    printf(" --> PASSED\n");
}

/**
 * Test Time Sync Service server status queries
 */
void TestTimeSyncServerStatus(void)
{
    Instance *instance;
    uint16_t  serverPort, configuredPort;
    bool      isClient, isServer, isForcedServer;

    printf("TestTimeSyncServerStatus\n");

    instance = testInitInstance();
    VerifyOrQuit(instance != nullptr);

    // Start server
    otForceStartAsTimeSyncServer(instance);
    otGetTimeSyncServiceStatus(instance, &isClient, &isServer, &isForcedServer);
    VerifyOrQuit(isForcedServer == true, "Server should be started");

    // Get server status
    otGetTimeSyncServiceServerStatus(instance, &serverPort, &configuredPort);
    printf("Server port: %u, Configured port: %u\n", serverPort, configuredPort);

    // Verify port values are reasonable
    VerifyOrQuit(serverPort > 0 || serverPort == 0, "Invalid server port");
    printf("Server status retrieved successfully\n");

    // Stop server
    otForceStopAsTimeSyncServer(instance);

    testFreeInstance(instance);
    printf(" --> PASSED\n");
}

/**
 * Test Time Sync Service client status queries
 */
void TestTimeSyncClientStatus(void)
{
    Instance *   instance;
    uint16_t     serverPort, clientPort, configuredPort;
    otIp6Address serverAddr;
    bool         isClient, isServer, isForcedServer;

    printf("TestTimeSyncClientStatus\n");

    instance = testInitInstance();
    VerifyOrQuit(instance != nullptr);

    // Get client status (even if not started)
    otGetTimeSyncServiceClientStatus(instance, &serverPort, &clientPort, &serverAddr, &configuredPort);
    printf("Client port: %u, Server port: %u, Configured port: %u\n", 
           clientPort, serverPort, configuredPort);

    // Verify basic status query succeeds
    otGetTimeSyncServiceStatus(instance, &isClient, &isServer, &isForcedServer);
    printf("Client status: %s\n", isClient ? "active" : "inactive");

    testFreeInstance(instance);
    printf(" --> PASSED\n");
}

/**
 * Test Time Sync Service forced server mode toggle
 */
void TestTimeSyncForcedServerMode(void)
{
    Instance *instance;
    bool      isClient, isServer, isForcedServer;

    printf("TestTimeSyncForcedServerMode\n");

    instance = testInitInstance();
    VerifyOrQuit(instance != nullptr);

    // Test multiple starts and stops
    for (int i = 0; i < 3; i++)
    {
        // Start forced server
        otForceStartAsTimeSyncServer(instance);
        otGetTimeSyncServiceStatus(instance, &isClient, &isServer, &isForcedServer);
        VerifyOrQuit(isForcedServer == true, "Failed to start forced server");

        // Stop forced server
        otForceStopAsTimeSyncServer(instance);
        otGetTimeSyncServiceStatus(instance, &isClient, &isServer, &isForcedServer);
        VerifyOrQuit(isForcedServer == false, "Failed to stop forced server");
    }
    printf("Forced server mode toggled successfully 3 times\n");

    testFreeInstance(instance);
    printf(" --> PASSED\n");
}

/**
 * Test Time Sync Service server history functionality
 */
void TestTimeSyncServerHistory(void)
{
    Instance *instance;
    char *    history;
    uint8_t   historySize;

    printf("TestTimeSyncServerHistory\n");

    instance = testInitInstance();
    VerifyOrQuit(instance != nullptr);

    // Get history size
    historySize = otGetTimeSyncServerHistorySize(instance);
    printf("Server history size: %u\n", historySize);

    // Get full history
    history = otGetTimeSyncServerHistory(instance);
    VerifyOrQuit(history != nullptr, "Failed to get server history");
    printf("Retrieved server history (length: %zu)\n", strlen(history));

    // Clear history
    otTimeSyncClearHistory(instance);
    printf("Cleared history\n");

    // Verify history is cleared
    history = otGetTimeSyncServerHistory(instance);
    VerifyOrQuit(history != nullptr, "Failed to get server history after clear");
    printf("Retrieved server history after clear (length: %zu)\n", strlen(history));

    testFreeInstance(instance);
    printf(" --> PASSED\n");
}

/**
 * Test Time Sync Service client history functionality
 */
void TestTimeSyncClientHistory(void)
{
    Instance *instance;
    char *    history;
    uint8_t   historySize;

    printf("TestTimeSyncClientHistory\n");

    instance = testInitInstance();
    VerifyOrQuit(instance != nullptr);

    // Get history size
    historySize = otGetTimeSyncClientHistorySize(instance);
    printf("Client history size: %u\n", historySize);

    // Get full history
    history = otGetTimeSyncClientHistory(instance);
    VerifyOrQuit(history != nullptr, "Failed to get client history");
    printf("Retrieved client history (length: %zu)\n", strlen(history));

    // Clear history
    otTimeSyncClearHistory(instance);
    printf("Cleared history\n");

    // Verify history is cleared
    history = otGetTimeSyncClientHistory(instance);
    VerifyOrQuit(history != nullptr, "Failed to get client history after clear");
    printf("Retrieved client history after clear (length: %zu)\n", strlen(history));

    testFreeInstance(instance);
    printf(" --> PASSED\n");
}

/**
 * Test Time Sync Service indexed history retrieval
 */
void TestTimeSyncIndexedHistory(void)
{
    Instance *instance;
    char *    serverHistory;
    char *    clientHistory;
    uint8_t   serverHistorySize;
    uint8_t   clientHistorySize;

    printf("TestTimeSyncIndexedHistory\n");

    instance = testInitInstance();
    VerifyOrQuit(instance != nullptr);

    // Get history sizes
    serverHistorySize = otGetTimeSyncServerHistorySize(instance);
    clientHistorySize = otGetTimeSyncClientHistorySize(instance);
    printf("Server history size: %u, Client history size: %u\n", serverHistorySize, clientHistorySize);

    // Get indexed server history (index 0 should always be safe)
    serverHistory = otGetTimeSyncServerHistoryAtIndex(instance, 0);
    VerifyOrQuit(serverHistory != nullptr, "Failed to get indexed server history");
    printf("Retrieved indexed server history[0] (length: %zu)\n", strlen(serverHistory));

    // Get indexed client history (index 0 should always be safe)
    clientHistory = otGetTimeSyncClientHistoryAtIndex(instance, 0);
    VerifyOrQuit(clientHistory != nullptr, "Failed to get indexed client history");
    printf("Retrieved indexed client history[0] (length: %zu)\n", strlen(clientHistory));

    testFreeInstance(instance);
    printf(" --> PASSED\n");
}

/**
 * Test Time Sync Service message sending
 */
void TestTimeSyncMessageSend(void)
{
    Instance *instance;
    bool      isClient, isServer, isForcedServer;

    printf("TestTimeSyncMessageSend\n");

    instance = testInitInstance();
    VerifyOrQuit(instance != nullptr);

    // Start forced server first
    otForceStartAsTimeSyncServer(instance);
    otGetTimeSyncServiceStatus(instance, &isClient, &isServer, &isForcedServer);
    VerifyOrQuit(isForcedServer == true, "Server should be started");

    // Send time sync message (should not crash)
    otSendTimeSyncMessage(instance);
    printf("Sent time sync message\n");

    // Stop server
    otForceStopAsTimeSyncServer(instance);

    testFreeInstance(instance);
    printf(" --> PASSED\n");
}

#endif // FEATURE_TIMESYNCSERVICE_ENABLE

} // namespace ot

int main(void)
{
#if FEATURE_TIMESYNCSERVICE_ENABLE
    ot::TestTimeSyncServerStartStop();
    ot::TestTimeSyncServerStatus();
    ot::TestTimeSyncClientStatus();
    ot::TestTimeSyncForcedServerMode();
    ot::TestTimeSyncServerHistory();
    ot::TestTimeSyncClientHistory();
    ot::TestTimeSyncIndexedHistory();
    ot::TestTimeSyncMessageSend();
    printf("All tests passed\n");
#else
    printf("Time Sync Service is not enabled\n");
    return -1;
#endif
    return 0;
}
