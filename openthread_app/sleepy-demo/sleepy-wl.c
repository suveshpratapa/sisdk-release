/*******************************************************************************
 * @file
 * @brief Thread Direct Wake Listener (WL) application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
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

#define CURRENT_MODULE_NAME "OPENTHREAD_SAMPLE_APP"

#include <assert.h>
#include <string.h>

#include <common/code_utils.hpp>
#include <openthread-core-config.h>
#include <openthread/cli.h>
#include <openthread/dataset.h>
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <openthread/thread_direct.h>

#include "sl_button.h"
#include "sl_simple_button.h"
#include "sl_simple_button_instances.h"

#include "sl_component_catalog.h"
#ifdef SL_CATALOG_POWER_MANAGER_PRESENT
#include "sl_power_manager.h"
#endif
#ifdef SL_CATALOG_KERNEL_PRESENT
#include "sl_ot_rtos_adaptation.h"
#endif

#define TD_SLW_PERIOD_SLOT 64 // 40000 us (unit of slot duration 625 us).
#define TD_SLW_TIMEOUT_MS 200 // Supervision interval, in milliseconds.

static bool sAllowSleep         = false;
static bool sSleepTogglePressed = false;
static bool sPrintState         = false;

extern void otSysEventSignalPending(void);

static void handleDirectEvent(otThreadDirectEvent aEvent, const otThreadDirectPeerInfo *aPeerInfo, void *aContext)
{
    (void)aContext;

    switch (aEvent)
    {
    case OT_THREAD_DIRECT_EVENT_WAKE_RECEIVED:
        if (aPeerInfo != NULL)
        {
            const otExtAddress *a = &aPeerInfo->mExtAddress;
            otCliOutputFormat("TD Wake Command received from: %02X%02X%02X%02X%02X%02X%02X%02X"
                              "  type: %u rv-time: %lu us retries: %u x %u\r\n",
                              a->m8[0],
                              a->m8[1],
                              a->m8[2],
                              a->m8[3],
                              a->m8[4],
                              a->m8[5],
                              a->m8[6],
                              a->m8[7],
                              aPeerInfo->mWakeType,
                              (unsigned long)aPeerInfo->mWakeRvTimeUs,
                              aPeerInfo->mWakeRetryCount,
                              aPeerInfo->mWakeRetryInterval);
        }
        break;
    case OT_THREAD_DIRECT_EVENT_LINKED:
        if (aPeerInfo != NULL)
        {
            const otExtAddress *a = &aPeerInfo->mExtAddress;
            otCliOutputFormat("TD link established with %02X%02X%02X%02X%02X%02X%02X%02X\r\n",
                              a->m8[0],
                              a->m8[1],
                              a->m8[2],
                              a->m8[3],
                              a->m8[4],
                              a->m8[5],
                              a->m8[6],
                              a->m8[7]);
        }
        else
        {
            otCliOutputFormat("TD link established\r\n");
        }
        break;
    case OT_THREAD_DIRECT_EVENT_UNLINKED:
        if (aPeerInfo != NULL)
        {
            const otExtAddress *a = &aPeerInfo->mExtAddress;
            otCliOutputFormat("TD link unlinked with %02X%02X%02X%02X%02X%02X%02X%02X\r\n",
                              a->m8[0],
                              a->m8[1],
                              a->m8[2],
                              a->m8[3],
                              a->m8[4],
                              a->m8[5],
                              a->m8[6],
                              a->m8[7]);
        }
        else
        {
            otCliOutputFormat("TD link unlinked\r\n");
        }
        break;
    default:
        break;
    }
}

void sleepyInit(void)
{
    otError          error;
    otExtAddress     extAddress = {.m8 = {0xAB, 0x89, 0x67, 0x45, 0x23, 0x01, 0xCD, 0xEF}};
    otLinkModeConfig config;

    otCliOutputFormat("sleepy-demo-wl starting in EM1 (idle) mode\r\n");
    otCliOutputFormat("Press BTN0 to toggle between EM2 (sleep) and EM1 (idle) modes\r\n");
    otCliOutputFormat("To get state of Thread Direct link:\r\n");
    otCliOutputFormat("   direct link state\r\n");

    // Fix our extended address so the WI can target us by a known value.
    SuccessOrExit(error = otLinkSetExtendedAddress(otInstanceGetSingle(), &extAddress));
    SuccessOrExit(error = otThreadDirectSetSlwSchedule(otInstanceGetSingle(), TD_SLW_PERIOD_SLOT));
    SuccessOrExit(error = otThreadDirectSetSlwTimeout(otInstanceGetSingle(), TD_SLW_TIMEOUT_MS));

    // Set link mode: rx-off-when-idle sleepy end device.
    config.mRxOnWhenIdle = 0;
    config.mDeviceType   = 0;
    config.mNetworkData  = 0;
    SuccessOrExit(error = otThreadSetLinkMode(otInstanceGetSingle(), config));

    otThreadDirectSetEventCallback(otInstanceGetSingle(), handleDirectEvent, NULL);

exit:
    if (error != OT_ERROR_NONE)
    {
        otCliOutputFormat("Initialization failed with: %s\r\n", otThreadErrorToString(error));
    }
    return;
}

/*
 * Callback from sl_ot_is_ok_to_sleep to check if it is ok to go to sleep.
 */
bool efr32AllowSleepCallback(void)
{
    return sAllowSleep;
}

void setNetworkConfiguration(void)
{
    static char          aNetworkName[] = "ThreadDirect";
    otError              error;
    otOperationalDataset aDataset;

    memset(&aDataset, 0, sizeof(otOperationalDataset));

    aDataset.mActiveTimestamp.mSeconds             = 1;
    aDataset.mComponents.mIsActiveTimestampPresent = true;

    aDataset.mChannel                      = 25;
    aDataset.mComponents.mIsChannelPresent = true;

    aDataset.mWakeupChannel                      = OPENTHREAD_CONFIG_THREAD_DIRECT_DEFAULT_WAKE_CHANNEL;
    aDataset.mComponents.mIsWakeupChannelPresent = true;

    aDataset.mPanId                      = (otPanId)0xD001;
    aDataset.mComponents.mIsPanIdPresent = true;

    uint8_t extPanId[OT_EXT_PAN_ID_SIZE] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0x00, 0x01};
    memcpy(aDataset.mExtendedPanId.m8, extPanId, sizeof(aDataset.mExtendedPanId));
    aDataset.mComponents.mIsExtendedPanIdPresent = true;

    uint8_t key[OT_NETWORK_KEY_SIZE] =
        {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    memcpy(aDataset.mNetworkKey.m8, key, sizeof(aDataset.mNetworkKey));
    aDataset.mComponents.mIsNetworkKeyPresent = true;

    size_t length = strlen(aNetworkName);
    assert(length <= OT_NETWORK_NAME_MAX_SIZE);
    memcpy(aDataset.mNetworkName.m8, aNetworkName, length);
    aDataset.mComponents.mIsNetworkNamePresent = true;

    error = otDatasetSetActive(otInstanceGetSingle(), &aDataset);
    if (error != OT_ERROR_NONE)
    {
        otCliOutputFormat("setNetworkConfiguration failed: %s\r\n", otThreadErrorToString(error));
    }
}

void sl_button_on_change(const sl_button_t *handle)
{
    if (sl_button_get_state(handle) == SL_SIMPLE_BUTTON_PRESSED)
    {
        if (&sl_button_btn0 == handle)
        {
            sSleepTogglePressed = true;
        }
        otSysEventSignalPending();
    }
#ifdef SL_CATALOG_KERNEL_PRESENT
    sl_ot_rtos_set_pending_event(SL_OT_RTOS_EVENT_APP);
#endif
}

#ifdef SL_CATALOG_KERNEL_PRESENT
#define applicationTick sl_ot_rtos_application_tick
#endif

void applicationTick(void)
{
    if (sPrintState)
    {
        otCliOutputFormat("sleepy-demo-wl switching to %s mode\r\n", sAllowSleep ? "EM2 (sleep)" : "EM1 (idle)");
        sPrintState = false;
    }

    if (sSleepTogglePressed)
    {
        sSleepTogglePressed = false;
        sAllowSleep         = !sAllowSleep;
        sPrintState         = true;

#if defined(SL_CATALOG_KERNEL_PRESENT) && defined(SL_CATALOG_POWER_MANAGER_PRESENT)
        if (sAllowSleep)
        {
            sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
        }
        else
        {
            sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
        }
#endif
    }
}
