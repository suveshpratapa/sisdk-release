/*******************************************************************************
 * @file
 * @brief This file includes simulation compile-time configuration constants
 *   for OpenThread.
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

#ifndef OPENTHREAD_CORE_EFR32_SIMULATION_CONFIG_H_
#define OPENTHREAD_CORE_EFR32_SIMULATION_CONFIG_H_

// Use (user defined) application config file to define OpenThread configurations
#ifdef SL_OPENTHREAD_APPLICATION_CONFIG_FILE
#include SL_OPENTHREAD_APPLICATION_CONFIG_FILE
#endif

// Use (pre-defined) stack features config file available for applications built
// with Simplicity Studio
#ifdef SL_OPENTHREAD_STACK_FEATURES_CONFIG_FILE
#include SL_OPENTHREAD_STACK_FEATURES_CONFIG_FILE
#endif

#ifndef OPENTHREAD_RADIO
#define OPENTHREAD_RADIO 0
#endif

/**
 * @def OPENTHREAD_CONFIG_PLATFORM_INFO
 *
 * The platform-specific string to insert into the OpenThread version string.
 *
 */
#define OPENTHREAD_CONFIG_PLATFORM_INFO "SIMULATION"

/**
 * @def OPENTHREAD_CONFIG_LOG_OUTPUT
 *
 * Specify where the log output should go.
 *
 */
#ifndef OPENTHREAD_CONFIG_LOG_OUTPUT /* allow command line override */
#define OPENTHREAD_CONFIG_LOG_OUTPUT OPENTHREAD_CONFIG_LOG_OUTPUT_PLATFORM_DEFINED
#endif

/**
 * @def OPENTHREAD_CONFIG_IP6_SLAAC_ENABLE
 *
 * Define as 1 to enable support for adding of auto-configured SLAAC addresses by OpenThread.
 *
 */
#ifndef OPENTHREAD_CONFIG_IP6_SLAAC_ENABLE /* allows command line override */
#define OPENTHREAD_CONFIG_IP6_SLAAC_ENABLE 1
#endif

#if OPENTHREAD_RADIO
/**
 * @def OPENTHREAD_CONFIG_MAC_SOFTWARE_ACK_TIMEOUT_ENABLE
 *
 * Define to 1 if you want to enable software ACK timeout logic.
 *
 */
#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_ACK_TIMEOUT_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_ACK_TIMEOUT_ENABLE 1
#endif

/**
 * @def OPENTHREAD_CONFIG_MAC_SOFTWARE_ENERGY_SCAN_ENABLE
 *
 * Define to 1 if you want to enable software energy scanning logic.
 *
 */
#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_ENERGY_SCAN_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_ENERGY_SCAN_ENABLE 1
#endif

/**
 * @def OPENTHREAD_CONFIG_MAC_SOFTWARE_RETRANSMIT_ENABLE
 *
 * Define to 1 if you want to enable software retransmission logic.
 *
 */
#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_RETRANSMIT_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_RETRANSMIT_ENABLE 1
#endif

/**
 * @def OPENTHREAD_CONFIG_MAC_SOFTWARE_CSMA_BACKOFF_ENABLE
 *
 * Define to 1 if you want to enable software CSMA-CA backoff logic.
 *
 */
#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_CSMA_BACKOFF_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_CSMA_BACKOFF_ENABLE 1
#endif

/**
 * @def OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_SECURITY_ENABLE
 *
 * Define to 1 if you want to enable software transmission security logic.
 *
 */
#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_SECURITY_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_SECURITY_ENABLE 1
#endif

/**
 * @def OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_TIMING_ENABLE
 *
 * Define to 1 to enable software transmission target time logic.
 *
 */
#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_TIMING_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_TIMING_ENABLE 1
#endif
#endif // OPENTHREAD_RADIO

/**
 * @def OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE
 *
 * Define to 1 if you want to support microsecond timer in platform.
 *
 */
#define OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE 1

/**
 * @def OPENTHREAD_CONFIG_PLATFORM_FLASH_API_ENABLE
 *
 * Define to 1 to enable otPlatFlash* APIs to support non-volatile storage.
 *
 * When defined to 1, the platform MUST implement the otPlatFlash* APIs instead of the otPlatSettings* APIs.
 *
 */
#define OPENTHREAD_CONFIG_PLATFORM_FLASH_API_ENABLE 1

/**
 * @def CLI_COAP_SECURE_USE_COAP_DEFAULT_HANDLER
 *
 * Define to 1 to use DefaultHandler for unhandled requests
 *
 */
#define CLI_COAP_SECURE_USE_COAP_DEFAULT_HANDLER 1

/**
 * @def OPENTHREAD_CONFIG_PLATFORM_RADIO_COEX_ENABLE
 *
 * Define to 1 if you want to enable radio coexistence implemented in platform.
 *
 */
#ifndef OPENTHREAD_CONFIG_PLATFORM_RADIO_COEX_ENABLE
#define OPENTHREAD_CONFIG_PLATFORM_RADIO_COEX_ENABLE 1
#endif

#ifndef OPENTHREAD_CONFIG_PARENT_SEARCH_ENABLE
#define OPENTHREAD_CONFIG_PARENT_SEARCH_ENABLE 1
#endif

/**
 * @def OPENTHREAD_CONFIG_LOG_PLATFORM
 *
 * Define to enable platform region logging.
 *
 */
#ifndef OPENTHREAD_CONFIG_LOG_PLATFORM
#define OPENTHREAD_CONFIG_LOG_PLATFORM 1
#endif

/**
 * @def OPENTHREAD_CONFIG_CLI_MAX_LINE_LENGTH
 *
 * The maximum size of the CLI line in bytes.
 *
 */
#ifndef OPENTHREAD_CONFIG_CLI_MAX_LINE_LENGTH
#define OPENTHREAD_CONFIG_CLI_MAX_LINE_LENGTH 640
#endif

/**
 * @def OPENTHREAD_CONFIG_CLI_UART_RX_BUFFER_SIZE
 *
 * The size of CLI UART RX buffer in bytes.
 *
 */
#ifndef OPENTHREAD_CONFIG_CLI_UART_RX_BUFFER_SIZE
#define OPENTHREAD_CONFIG_CLI_UART_RX_BUFFER_SIZE 640
#endif

/**
 * @def OPENTHREAD_CONFIG_MLE_MAX_CHILDREN
 *
 * The maximum number of children.
 *
 */
#ifndef OPENTHREAD_CONFIG_MLE_MAX_CHILDREN
#define OPENTHREAD_CONFIG_MLE_MAX_CHILDREN 128
#endif

/**
 * @def OPENTHREAD_CONFIG_MLE_INFORM_PREVIOUS_PARENT_ON_REATTACH
 *
 * Define as 1 for a child to inform its previous parent when it attaches to a new parent.
 *
 * If this feature is enabled, when a device attaches to a new parent, it will send an IP message (with empty payload
 * and mesh-local IP address as the source address) to its previous parent.
 *
 */
#ifndef OPENTHREAD_CONFIG_MLE_INFORM_PREVIOUS_PARENT_ON_REATTACH
#define OPENTHREAD_CONFIG_MLE_INFORM_PREVIOUS_PARENT_ON_REATTACH 1
#endif

/**
 * @def OPENTHREAD_CONFIG_UPTIME_ENABLE
 *
 * Define to 1 to enable tracking the uptime of OpenThread instance.
 *
 */
#ifndef OPENTHREAD_CONFIG_UPTIME_ENABLE
#define OPENTHREAD_CONFIG_UPTIME_ENABLE 1
#endif

/**
 * @def OPENTHREAD_CONFIG_LOG_PREPEND_UPTIME
 *
 * Define as 1 to prepend the current uptime to all log messages.
 *
 */
#ifndef OPENTHREAD_CONFIG_LOG_PREPEND_UPTIME
#define OPENTHREAD_CONFIG_LOG_PREPEND_UPTIME 1
#endif

/**
 * @def OPENTHREAD_CONFIG_SRP_CLIENT_BUFFERS_MAX_SERVICES
 *
 * Specifies number of service entries in the SRP client service pool.
 *
 * This config is applicable only when `OPENTHREAD_CONFIG_SRP_CLIENT_BUFFERS_ENABLE` is enabled.
 *
 */
#ifndef OPENTHREAD_CONFIG_SRP_CLIENT_BUFFERS_MAX_SERVICES
#define OPENTHREAD_CONFIG_SRP_CLIENT_BUFFERS_MAX_SERVICES 20
#endif

/**
 * @def OPENTHREAD_CONFIG_DETERMINISTIC_ECDSA_ENABLE
 *
 * Define to 1 to generate ECDSA signatures determinsitically
 * according to RFC 6979 instead of randomly.
 *
 */
#ifndef OPENTHREAD_CONFIG_DETERMINISTIC_ECDSA_ENABLE
#define OPENTHREAD_CONFIG_DETERMINISTIC_ECDSA_ENABLE 1
#endif

/**
 * @def OPENTHREAD_CONFIG_NCP_HDLC_ENABLE
 *
 * Define to 1 to enable NCP HDLC support.
 *
 */
#ifndef OPENTHREAD_CONFIG_NCP_HDLC_ENABLE
#define OPENTHREAD_CONFIG_NCP_HDLC_ENABLE 1
#endif

#endif // OPENTHREAD_CORE_EFR32_SIMULATION_CONFIG_H_
