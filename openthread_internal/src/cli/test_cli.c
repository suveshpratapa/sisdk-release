/*******************************************************************************
 * @file
 * @brief Test CLI
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#ifdef SL_COMPONENT_CATALOG_PRESENT
#include "sl_component_catalog.h"
#endif // SL_COMPONENT_CATALOG_PRESENT

#include "radio_extension.h"
#include "sl_ot_custom_cli.h"
#include <common/code_utils.hpp>
#if defined(SL_CATALOG_OT_DEBUG_CHANNEL_PRESENT)
#include "debug_channel.h"
#include <sl_iostream_debug.h>
#include <sl_iostream_swo_itm_8.h>
#endif // SL_CATALOG_OT_DEBUG_CHANNEL_PRESENT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <openthread/cli.h>

#if defined(SL_CATALOG_OT_CRASH_HANDLER_PRESENT)
#if CORTEXM3_EFR32
#include "platform-efr32.h"
#include PLATFORM_HEADER
#if defined(_SILICON_LABS_32B_SERIES_2)
#define SL_OPENTHREAD_TEST_FAULT_ADDRESS 0xDEADBEEF
#else
#define SL_OPENTHREAD_TEST_FAULT_ADDRESS 0x00000000
#endif // _SILICON_LABS_32B_SERIES_2
#endif // CORTEXM3_EFR32
#endif // SL_CATALOG_OT_CRASH_HANDLER_PRESENT

#ifndef SL_OPENTHREAD_TEST_FAULT_ADDRESS
// For POSIX builds
#define SL_OPENTHREAD_TEST_FAULT_ADDRESS 0x00000000
#endif

#ifdef SL_OPENTHREAD_PERF_TEST_CLI_ENABLE
extern otError PerfTestCommand(void *context, uint8_t argc, char *argv[]);
static otError multicastTestCommand(void *context, uint8_t argc, char *argv[])
{
    // Support for the "test mx" command has been moved to perf_test_cli.cpp.
    // For backward compatibility support remains by redirecting "test mx"
    // to "perf mx".
    otError error = OT_ERROR_NONE;
    char   *args[33];

    VerifyOrExit(argc <= 32, error = OT_ERROR_INVALID_ARGS);

    args[0] = "mx";
    for (uint8_t i = 0; i < argc; i++)
    {
        args[i + 1] = argv[i];
    }

    error = PerfTestCommand(context, argc + 1, args);

exit:
    return error;
}
#endif // SL_OPENTHREAD_PERF_TEST_CLI_ENABLE

void sl_ot_test_custom_cli_init(void)
{
    return;
}

static otError printfTestCommand(void *context, uint8_t argc, char *argv[])
{
    OT_UNUSED_VARIABLE(context);
    otError error = OT_ERROR_NONE;

    VerifyOrExit(argc == 1, error = OT_ERROR_INVALID_ARGS);

    // Parse arguments
    const uint8_t *payload = (uint8_t *)argv[0];

#if defined(SL_CATALOG_OT_DEBUG_CHANNEL_PRESENT)
    // Print message on debug backchannel
    sl_debug_binary_format(EM_DEBUG_PRINTF, "F", payload);
#else
    otCliOutputFormat("EM_DEBUG_PRINTF: %s\r\n", payload);
#endif // SL_CATALOG_OT_DEBUG_CHANNEL_PRESENT

exit:
    return error;
}

static otError getPtiRadioConfigCommand(void *aContext, uint8_t argc, char *argv[])
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(argc);
    OT_UNUSED_VARIABLE(argv);

    otError  error = OT_ERROR_NONE;
    uint16_t radioConfig;

    SuccessOrExit(error = otPlatRadioExtensionGetPtiRadioConfig(&radioConfig));
    otCliOutputFormat("PTI Radio Config: 0x%02x", radioConfig);
    otCliOutputFormat("\r\n");

exit:
    return error;
}

static otError set802154CcaModeCommand(void *aContext, uint8_t argc, char *argv[])
{
    OT_UNUSED_VARIABLE(aContext);

    otError error = OT_ERROR_NONE;
    VerifyOrExit(argc == 1, error = OT_ERROR_INVALID_ARGS);

    uint8_t mode = (uint8_t)strtoul(argv[0], NULL, 10);

    SuccessOrExit(error = otPlatRadioExtensionSetCcaMode(mode));

exit:
    return error;
}

static otError faultCommand(void *aContext, uint8_t argc, char *argv[])
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(argv);

    otError error = OT_ERROR_NONE;
    VerifyOrExit(argc <= 1, error = OT_ERROR_INVALID_ARGS);
#if CORTEXM3_EFR32
    const char *help = "Usage: test fault [assert|udf|bkpt]\r\n";
#else
    const char *help = "Usage: test fault [assert]\r\n";
#endif

    // Parse fault type
    const char *faultType = ((argc > 0) ? argv[0] : NULL);

    if (argc == 0)
    {
        // Trigger an invalid memory access
        int x = 0;
        x += *((uint32_t *)SL_OPENTHREAD_TEST_FAULT_ADDRESS);
        otCliOutputFormat("%d", x);
    }
    else if (strcmp(faultType, "assert") == 0)
    {
        assert(0);
    }
#if CORTEXM3_EFR32
    else if (strcmp(faultType, "udf") == 0)
    {
        asm("udf");
    }
    else if (strcmp(faultType, "bkpt") == 0)
    {
        asm("bkpt");
    }
#endif
    else if (strcmp(faultType, "help") == 0)
    {
        otCliOutputFormat(help);
    }
    else
    {
        otCliOutputFormat("Invalid fault type: %s\r\n\n", faultType);
        otCliOutputFormat(help);
        ExitNow(error = OT_ERROR_INVALID_ARGS);
    }

exit:
    // Should not be reached
    return error;
}

#if defined(SL_CATALOG_OT_CRASH_HANDLER_PRESENT)
static otError crashInfoCommand(void *aContext, uint8_t argc, char *argv[])
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(argv);

    otError error = OT_ERROR_NONE;
    VerifyOrExit(argc == 0, error = OT_ERROR_INVALID_ARGS);

#if CORTEXM3_EFR32
    efr32PrintResetInfo();
#endif

exit:
    return error;
}
#endif

//------------------------------------------------------------------------------

// Forward declarations
static otError helpCommand(void *aContext, uint8_t argc, char *argv[]);

static otCliCommand testCommands[] = {
    {"help", helpCommand},
#ifdef SL_OPENTHREAD_PERF_TEST_CLI_ENABLE
    {"mx", multicastTestCommand},
#endif
    {"pf", printfTestCommand},
    {"get-pti-radio-config", getPtiRadioConfigCommand},
    {"set-802154-cca-mode", set802154CcaModeCommand},
    {"fault", faultCommand},

#if defined(SL_CATALOG_OT_CRASH_HANDLER_PRESENT)
    {"ci", crashInfoCommand},
#endif
};

otError testCommand(void *context, uint8_t argc, char *argv[])
{
    otError error = processCommand(context, argc, argv, OT_ARRAY_LENGTH(testCommands), testCommands);

    if (error == OT_ERROR_INVALID_COMMAND)
    {
        (void)helpCommand(NULL, 0, NULL);
    }

    return error;
}

static otError helpCommand(void *context, uint8_t argc, char *argv[])
{
    OT_UNUSED_VARIABLE(context);
    OT_UNUSED_VARIABLE(argc);
    OT_UNUSED_VARIABLE(argv);
    printCommands(testCommands, OT_ARRAY_LENGTH(testCommands));

    return OT_ERROR_NONE;
}
