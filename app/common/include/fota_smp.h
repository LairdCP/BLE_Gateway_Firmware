/**
 * @file fota_smp.h
 * @brief Firmware update over-the-air using SMP file transfer and a
 * control point. Currently used for updating HL7800 (and nothing else).
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __FOTA_SMP_H__
#define __FOTA_SMP_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief FOTA control point handler
 */
void fota_smp_cmd_handler(void);

/**
 * @brief Modem FOTA state handler
 *
 * @param state @ref mdm_hl7800_fota_state
 */
void fota_smp_state_handler(uint8_t state);

/**
 * @brief Size of image in bytes
 */
void fota_smp_set_count(uint32_t value);

/**
 * @brief Accessor
 *
 * @retval true when modem fota is in progress, false otherwise
 */
bool fota_smp_modem_busy(void);

/**
 * @brief Starts modem update when requested.  Must be periodically called.
 */
void fota_smp_start_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __FOTA2_H__ */
