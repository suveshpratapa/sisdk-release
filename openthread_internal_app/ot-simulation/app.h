/*******************************************************************************
 * @file
 * @brief Application interface provided to main().
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

#ifndef APP_H
#define APP_H

#if defined(SL_COMPONENT_CATALOG_PRESENT)
#include "sl_component_catalog.h"
#endif

/******************************************************************************
 * Application Init.
 *****************************************************************************/
void app_init(int argc, char *argv[]);

/******************************************************************************
 * Application Exit.
 *****************************************************************************/
void app_exit(void);

/******************************************************************************
 * Application Process Action.
 *****************************************************************************/
void app_process_action(void);

#endif
