/*******************************************************************************
 * @brief This file enables support for all Silabs specific features available
 * only through the Simplicity SDK
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: LicenseRef-MSLA
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of the Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement
 * By installing, copying or otherwise using this software, you agree to the
 * terms of the MSLA.
 *
 ******************************************************************************/

#include "radio_extension.h"
#include "sl_ot_custom_cli.h"
#include <common/code_utils.hpp>

#ifdef SL_OPENTHREAD_MEMORY_USAGE_CLI_ENABLE

static otError heapCommand(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(aArgsLength);
    OT_UNUSED_VARIABLE(aArgs);

    otError  error       = OT_ERROR_NONE;
    uint32_t used_memory = 0U;
    SuccessOrExit(error = otPlatRadioExtensionGetUsedHeap(&used_memory));
    otCliOutputFormat("Current Used Heap Size: %lu bytes\n", (unsigned long)used_memory);
    otCliOutputFormat("\r\n");

exit:
    return error;
}

static otError highwatermarkCommand(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(aArgsLength);
    OT_UNUSED_VARIABLE(aArgs);

    otError  error          = OT_ERROR_NONE;
    uint32_t high_watermark = 0U;
    SuccessOrExit(error = otPlatRadioExtensionGetHighWaterMark(&high_watermark));
    otCliOutputFormat("Current Heap High Watermark: %lu bytes\n", (unsigned long)high_watermark);
    otCliOutputFormat("\r\n");

exit:
    return error;
}

static otError helpCommand(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(aArgsLength);
    OT_UNUSED_VARIABLE(aArgs);

    otCliOutputFormat("Available commands:\n");
    otCliOutputFormat("  heap - Show current used heap size\n");
    otCliOutputFormat("  highwatermark - Show current heap high watermark\n");
    otCliOutputFormat("  help - Show this help message\n");
    otCliOutputFormat("\r\n");
    return OT_ERROR_NONE;
}

static otCliCommand memoryUsageCommands[] = {
    {"heap", heapCommand},
    {"highwatermark", highwatermarkCommand},
    {"help", helpCommand},
};

otError memoryUsageHandler(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    otError error =
        processCommand(aContext, aArgsLength, aArgs, OT_ARRAY_LENGTH(memoryUsageCommands), memoryUsageCommands);

    if (error == OT_ERROR_INVALID_COMMAND)
    {
        (void)helpCommand(NULL, 0, NULL);
    }

    return error;
}

#endif // SL_OPENTHREAD_MEMORY_USAGE_CLI_ENABLE
