/*******************************************************************************
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
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

// Define module name for Power Manager debuging feature.
#define CURRENT_MODULE_NAME "OPENTHREAD_SAMPLE_APP"

#include <assert.h>
#include <openthread-core-config.h>
#include <openthread/config.h>

#include <openthread/cli.h>
#include <openthread/diag.h>
#include <openthread/tasklet.h>
#include <openthread/platform/logging.h>

#include "app.h"
#include "openthread-system.h"
#include "reset_util.h"
#include "common/code_utils.hpp"

#include "sl_component_catalog.h"

#ifdef SL_CATALOG_OPENTHREAD_CLI_PRESENT
/**
 * This function initializes the CLI app.
 *
 * @param[in]  aInstance  The OpenThread instance structure.
 *
 */
extern void otAppCliInit(otInstance *aInstance);
#endif

#ifdef SL_CATALOG_OPENTHREAD_NCP_PRESENT
/**
 * This function initializes the NCP app.
 *
 * @param[in]  aInstance  The OpenThread instance structure.
 *
 */
extern void otAppNcpInit(otInstance *aInstance);
#endif

#if OPENTHREAD_CONFIG_HEAP_EXTERNAL_ENABLE
OT_TOOL_WEAK void *otPlatCAlloc(size_t aNum, size_t aSize)
{
    return calloc(aNum, aSize);
}

OT_TOOL_WEAK void otPlatFree(void *aPtr)
{
    free(aPtr);
}
#endif

static otInstance *sInstance = NULL;

/*
 * Provide, if required an "otPlatLog()" function
 */
#if OPENTHREAD_CONFIG_LOG_OUTPUT == OPENTHREAD_CONFIG_LOG_OUTPUT_APP && defined(SL_CATALOG_OPENTHREAD_CLI_PRESENT)
void otPlatLog(otLogLevel aLogLevel, otLogRegion aLogRegion, const char *aFormat, ...)
{
    OT_UNUSED_VARIABLE(aLogLevel);
    OT_UNUSED_VARIABLE(aLogRegion);
    OT_UNUSED_VARIABLE(aFormat);

    va_list ap;
    va_start(ap, aFormat);
    otCliPlatLogv(aLogLevel, aLogRegion, aFormat, ap);
    va_end(ap);
}
#endif

void sl_ot_create_instance(void)
{
    sInstance = otInstanceInitSingle();
    assert(sInstance);
}

#ifdef SL_CATALOG_OPENTHREAD_CLI_PRESENT
void sl_ot_cli_init(void)
{
    otAppCliInit(sInstance);
}
#endif

#ifdef SL_CATALOG_OPENTHREAD_NCP_PRESENT
void sl_ot_ncp_init(void)
{
    otAppNcpInit(sInstance);
}
#endif

/******************************************************************************
 * Application Init.
 *****************************************************************************/

otError ProcessExit(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(aArgsLength);
    OT_UNUSED_VARIABLE(aArgs);

    app_exit();
    exit(EXIT_SUCCESS);
}

void app_init(int argc, char *argv[])
{
    OT_UNUSED_VARIABLE(argc);

    OT_SETUP_RESET_JUMP(argv);
}

/******************************************************************************
 * Application Process Action.
 *****************************************************************************/
void app_process_action(void)
{
    otTaskletsProcess(sInstance);
    otSysProcessDrivers(sInstance);
}

/******************************************************************************
 * Application Exit.
 *****************************************************************************/
void app_exit(void)
{
    otInstanceFinalize(sInstance);
    // TO DO : pseudo reset?
}
