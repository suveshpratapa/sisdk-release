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

#ifndef _RAIL_UTIL_SIMULATION_H
#define _RAIL_UTIL_SIMULATION_H

#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int8_t sl_rail_util_antenna_mode_t;

typedef uint16_t sl_rail_ieee802154_phy_t;
typedef uint8_t  sl_rail_util_coex_gpio_index_t;

typedef uint32_t sl_rail_util_coex_options_t;

typedef uint8_t COEX_Req_t;
enum COEX_Req_t_enum
{
    COEX_REQ_ON_SHIFT = 0U,
    COEX_REQ_HIPRI_SHIFT,
    COEX_REQ_FORCE_SHIFT,
    COEX_REQCB_REQUESTED_SHIFT,
    COEX_REQCB_GRANTED_SHIFT,
    COEX_REQCB_NEGATED_SHIFT,
    COEX_REQCB_OFF_SHIFT,
    COEX_REQ_PWM_SHIFT,
};

typedef void (*COEX_ReqCb_t)(COEX_Req_t coexStatus);

#define sl_rail_util_coex_req_t COEX_Req_t
#define sl_rail_util_coex_cb_t COEX_ReqCb_t

typedef struct COEX_PwmArgs
{
    COEX_Req_t req;
    uint8_t    dutyCycle;
    uint8_t    periodHalfMs;
} COEX_PwmArgs_t;

#define sl_rail_util_coex_pwm_args_t COEX_PwmArgs_t

typedef uint8_t sl_rail_util_coex_event_t;
enum sl_rail_util_coex_event_t_enum
{
    SL_RAIL_UTIL_COEX_EVENT_LO_PRI_REQUESTED,
    SL_RAIL_UTIL_COEX_EVENT_HI_PRI_REQUESTED,
    SL_RAIL_UTIL_COEX_EVENT_LO_PRI_DENIED,
    SL_RAIL_UTIL_COEX_EVENT_HI_PRI_DENIED,
    SL_RAIL_UTIL_COEX_EVENT_LO_PRI_TX_ABORTED,
    SL_RAIL_UTIL_COEX_EVENT_HI_PRI_TX_ABORTED,
    SL_RAIL_UTIL_COEX_EVENT_COUNT,
};

typedef uint8_t COEX_GpioIndex_t;
enum COEX_GpioIndex_t_enum
{
    COEX_GPIO_INDEX_NONE             = 0,
    COEX_GPIO_INDEX_RHO              = 1,
    COEX_GPIO_INDEX_REQ              = 2,
    COEX_GPIO_INDEX_GNT              = 3,
    COEX_GPIO_INDEX_PHY_SELECT       = 4,
    COEX_GPIO_INDEX_WIFI_TX          = 5,
    COEX_GPIO_INDEX_INTERNAL_REQ     = 6,
    COEX_GPIO_INDEX_INTERNAL_PWM_REQ = 7,
    COEX_GPIO_INDEX_COUNT
};

typedef uint16_t RAIL_IEEE802154_PtiRadioConfig_t;
enum RAIL_IEEE802154_PtiRadioConfig_t_enum
{
    SL_RAIL_IEEE802154_PHY_2P4_GHZ                  = 0x00U,
    SL_RAIL_IEEE802154_PHY_2P4_GHZ_ANT_DIV          = 0x01U,
    SL_RAIL_IEEE802154_PHY_2P4_GHZ_COEX             = 0x02U,
    SL_RAIL_IEEE802154_PHY_2P4_GHZ_ANT_DIV_COEX     = 0x03U,
    SL_RAIL_IEEE802154_PHY_2P4_GHZ_FEM              = 0x08U,
    SL_RAIL_IEEE802154_PHY_2P4_GHZ_FEM_ANT_DIV      = 0x09U,
    SL_RAIL_IEEE802154_PHY_2P4_GHZ_FEM_COEX         = 0x0AU,
    SL_RAIL_IEEE802154_PHY_2P4_GHZ_FEM_ANT_DIV_COEX = 0x0BU,
    SL_RAIL_IEEE802154_PHY_863_MHZ_GB868            = 0x85U,
    SL_RAIL_IEEE802154_PHY_915_MHZ_GB868            = 0x86U,
};

typedef uint8_t sl_rail_status_t;
enum sl_rail_status_t_enum
{
    SL_RAIL_STATUS_NO_ERROR,
    SL_RAIL_STATUS_INVALID_PARAMETER,
    SL_RAIL_STATUS_INVALID_STATE,
    SL_RAIL_STATUS_INVALID_CALL,
    SL_RAIL_STATUS_SUSPENDED,
    SL_RAIL_STATUS_SCHED_ERROR,
};

typedef uint8_t sl_rail_ieee802154_cca_mode_t;
enum sl_rail_ieee802154_cca_mode_t_enum
{
    SL_RAIL_IEEE802154_CCA_MODE_RSSI = 0,
    SL_RAIL_IEEE802154_CCA_MODE_SIGNAL,
    SL_RAIL_IEEE802154_CCA_MODE_SIGNAL_OR_RSSI,
    SL_RAIL_IEEE802154_CCA_MODE_SIGNAL_AND_RSSI,
    SL_RAIL_IEEE802154_CCA_MODE_ALWAYS_TRANSMIT,
    SL_RAIL_IEEE802154_CCA_MODE_COUNT
};

sl_rail_util_antenna_mode_t sl_rail_util_ant_div_get_tx_antenna_mode(void);
sl_status_t                 sl_rail_util_ant_div_set_tx_antenna_mode(sl_rail_util_antenna_mode_t mode);
sl_rail_util_antenna_mode_t sl_rail_util_ant_div_get_rx_antenna_mode(void);
sl_status_t                 sl_rail_util_ant_div_set_rx_antenna_mode(sl_rail_util_antenna_mode_t mode);
sl_rail_ieee802154_phy_t    sl_rail_util_ieee802154_get_active_radio_config(void);
uint8_t                     sl_rail_util_coex_get_directional_priority_pulse_width(void);
sl_status_t                 sl_rail_util_coex_set_directional_priority_pulse_width(uint8_t pulseWidthUs);
bool                        sl_rail_util_coex_get_gpio_input_override(sl_rail_util_coex_gpio_index_t gpioIndex);
sl_status_t sl_rail_util_coex_set_gpio_input_override(sl_rail_util_coex_gpio_index_t gpioIndex, bool enabled);
uint8_t     sl_rail_util_coex_get_phy_select_timeout(void);
sl_status_t sl_rail_util_coex_set_phy_select_timeout(uint8_t timeoutMs);
sl_rail_util_coex_options_t         sl_rail_util_coex_get_options(void);
sl_status_t                         sl_rail_util_coex_set_options(sl_rail_util_coex_options_t options);
sl_rail_util_coex_options_t         sl_rail_util_coex_get_constant_options(void);
bool                                sl_rail_util_coex_is_enabled(void);
sl_status_t                         sl_rail_util_coex_set_enable(bool enabled);
const sl_rail_util_coex_pwm_args_t *sl_rail_util_coex_get_request_pwm_args(void);
sl_status_t                         sl_rail_util_coex_set_request_pwm(sl_rail_util_coex_req_t ptaReq,
                                                                      sl_rail_util_coex_cb_t  ptaCb,
                                                                      uint8_t                 dutyCycle,
                                                                      uint8_t                 periodHalfMs);
bool                                sl_rail_util_coex_get_radio_holdoff(void);
sl_status_t                         sl_rail_util_coex_set_radio_holdoff(bool enabled);

#ifdef __cplusplus
}
#endif
#endif // _RAIL_UTIL_SIMULATION_H
