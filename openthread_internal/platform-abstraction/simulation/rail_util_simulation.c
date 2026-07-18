/***************************************************************************/
/**
 * @file
 * @brief Simulation for rail antenna diversity and coex APIs.
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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "rail_util_simulation.h"

static sl_rail_util_antenna_mode_t   txAntennaMode      = 0;
static sl_rail_util_antenna_mode_t   rxAntennaMode      = 0;
static sl_rail_ieee802154_phy_t      activeRadioConfig  = SL_RAIL_IEEE802154_PHY_2P4_GHZ_FEM_ANT_DIV_COEX;
static uint8_t                       priorityPulseWidth = 0;
static bool                          gpioInputOverride[COEX_GPIO_INDEX_COUNT] = {0};
static uint8_t                       phySelectTimeout                         = 0;
static sl_rail_util_coex_options_t   coexOptions                              = 0;
static sl_rail_util_coex_options_t   constantOptions = 5; // no setter so set to non zero for testing
static bool                          coexEnabled     = true;
static sl_rail_util_coex_pwm_args_t  pwmArgs         = {0};
static bool                          radioHoldoff    = true;
uint32_t                             efr32RadioCoexCounters[SL_RAIL_UTIL_COEX_EVENT_COUNT] = {1, 2, 3, 4, 5, 6};
static sl_rail_ieee802154_cca_mode_t ccaMode        = SL_RAIL_IEEE802154_CCA_MODE_SIGNAL_AND_RSSI;
static sl_rail_ieee802154_phy_t      ptiRadioConfig = SL_RAIL_IEEE802154_PHY_2P4_GHZ_FEM_ANT_DIV_COEX;

sl_rail_util_antenna_mode_t sl_rail_util_ant_div_get_tx_antenna_mode(void)
{
    return txAntennaMode;
}

sl_status_t sl_rail_util_ant_div_set_tx_antenna_mode(sl_rail_util_antenna_mode_t mode)
{
    txAntennaMode = mode;
    return SL_STATUS_OK;
}

sl_rail_util_antenna_mode_t sl_rail_util_ant_div_get_rx_antenna_mode(void)
{
    return rxAntennaMode;
}

sl_status_t sl_rail_util_ant_div_set_rx_antenna_mode(sl_rail_util_antenna_mode_t mode)
{
    rxAntennaMode = mode;
    return SL_STATUS_OK;
}

sl_rail_ieee802154_phy_t sl_rail_util_ieee802154_get_active_radio_config(void)
{
    return activeRadioConfig;
}

uint8_t sl_rail_util_coex_get_directional_priority_pulse_width(void)
{
    return priorityPulseWidth;
}

sl_status_t sl_rail_util_coex_set_directional_priority_pulse_width(uint8_t pulseWidthUs)
{
    priorityPulseWidth = pulseWidthUs;
    return SL_STATUS_OK;
}

bool sl_rail_util_coex_get_gpio_input_override(sl_rail_util_coex_gpio_index_t gpioIndex)
{
    return gpioInputOverride[gpioIndex + 1];
}

sl_status_t sl_rail_util_coex_set_gpio_input_override(sl_rail_util_coex_gpio_index_t gpioIndex, bool enabled)
{
    gpioInputOverride[gpioIndex] = enabled;
    return SL_STATUS_OK;
}

uint8_t sl_rail_util_coex_get_phy_select_timeout(void)
{
    return phySelectTimeout;
}

sl_status_t sl_rail_util_coex_set_phy_select_timeout(uint8_t timeoutMs)
{
    phySelectTimeout = timeoutMs;
    return SL_STATUS_OK;
}

sl_rail_util_coex_options_t sl_rail_util_coex_get_options(void)
{
    return coexOptions;
}

sl_status_t sl_rail_util_coex_set_options(sl_rail_util_coex_options_t options)
{
    coexOptions = options;
    return SL_STATUS_OK;
}

sl_rail_util_coex_options_t sl_rail_util_coex_get_constant_options(void)
{
    return constantOptions;
}

bool sl_rail_util_coex_is_enabled(void)
{
    return coexEnabled;
}

sl_status_t sl_rail_util_coex_set_enable(bool enabled)
{
    coexEnabled = enabled;
    return SL_STATUS_OK;
}

const sl_rail_util_coex_pwm_args_t *sl_rail_util_coex_get_request_pwm_args(void)
{
    return &pwmArgs;
}

sl_status_t sl_rail_util_coex_set_request_pwm(sl_rail_util_coex_req_t ptaReq,
                                              sl_rail_util_coex_cb_t  ptaCb,
                                              uint8_t                 dutyCycle,
                                              uint8_t                 periodHalfMs)
{
    (void)ptaCb;

    pwmArgs.req          = ptaReq;
    pwmArgs.dutyCycle    = dutyCycle;
    pwmArgs.periodHalfMs = periodHalfMs;
    return SL_STATUS_OK;
}

bool sl_rail_util_coex_get_radio_holdoff(void)
{
    return radioHoldoff;
}

void efr32RadioClearCoexCounters(void)
{
    memset((void *)efr32RadioCoexCounters, 0, sizeof(efr32RadioCoexCounters));
}

sl_status_t sl_rail_util_coex_set_radio_holdoff(bool enabled)
{
    radioHoldoff = enabled;
    return SL_STATUS_OK;
}

sl_rail_ieee802154_phy_t efr32GetPtiRadioConfig(void)
{
    return ptiRadioConfig;
}

sl_rail_status_t efr32RadioSetCcaMode(uint8_t aMode)
{
    ccaMode = (sl_rail_ieee802154_cca_mode_t)aMode;
    return SL_RAIL_STATUS_NO_ERROR;
}
