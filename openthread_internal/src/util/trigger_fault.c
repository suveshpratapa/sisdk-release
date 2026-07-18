/*******************************************************************************
 * @file
 * @brief Functions required to automatically trigger a fault after boot
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

#include <stdint.h>

#include <openthread/platform/toolchain.h>

#include "sl_rail.h"
#include "sl_rail_types.h"

#include "sl_openthread_trigger_fault_config.h"

static sl_rail_multi_timer_t triggerFaultTimer;
extern sl_rail_handle_t      gRailHandle;

static void timerCb(sl_rail_multi_timer_t *tmr, sl_rail_time_t expectedTimeOfEvent, void *cbArg)
{
    OT_UNUSED_VARIABLE(tmr);
    OT_UNUSED_VARIABLE(expectedTimeOfEvent);
    OT_UNUSED_VARIABLE(cbArg);

    // Intentionally trigger a fault
    uint32_t x = 0xdeadbeef;
    *((uint32_t volatile *)x) += x;
}

static void setTimer(sl_rail_multi_timer_t *timer, uint32_t time, sl_rail_multi_timer_callback_t cb)
{
    if (!sl_rail_is_multi_timer_running(gRailHandle, timer))
    {
        sl_rail_set_multi_timer(gRailHandle, timer, time, SL_RAIL_TIME_DELAY, cb, NULL);
    }
}

void sl_ot_trigger_fault_init(void)
{
    // sl_rail_time_t is microseconds
    sl_rail_time_t expirationTime = SL_OPENTHREAD_TRIGGER_FAULT_DELAY_MS * 1000;
    setTimer(&triggerFaultTimer, expirationTime, &timerCb);
}
