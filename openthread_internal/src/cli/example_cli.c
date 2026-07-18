/*******************************************************************************
 * @file
 * @brief Example implementation of a custom CLI for OpenThread.
 *
 * @note Commands implemented using otCliOutputFormat will be able to print
 * for both SoC and RCP builds
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

#include "sl_ot_custom_cli.h"
#include <common/code_utils.hpp>

static otError helloWorld(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(aArgsLength);
    OT_UNUSED_VARIABLE(aArgs);

    otCliOutputFormat("Hello World! :D\n");

    return OT_ERROR_NONE;
}

//------------------------------------------------------------------------------

static otError helpCommand(void *aContext, uint8_t aArgsLength, char *aArgs[]);

static otCliCommand exampleCommands[] = {
    {"help", helpCommand},
    {"hw", helloWorld},
};

static otError helpCommand(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(aArgsLength);
    OT_UNUSED_VARIABLE(aArgs);
    printCommands(exampleCommands, OT_ARRAY_LENGTH(exampleCommands));

    return OT_ERROR_NONE;
}

otError exampleCliCommand(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    otError error = processCommand(aContext, aArgsLength, aArgs, OT_ARRAY_LENGTH(exampleCommands), exampleCommands);

    if (error == OT_ERROR_INVALID_COMMAND)
    {
        (void)helpCommand(NULL, 0, NULL);
    }

    return error;
}
