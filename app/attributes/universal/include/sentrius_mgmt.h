/**
 * @file sentrius_mgmt.h
 *
 * @brief SMP interface for Sentrius Command Group
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SENTRIUS_MGMT_H__
#define __SENTRIUS_MGMT_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

int sentrius_mgmt_led_test(uint32_t duration);
int sentrius_mgmt_factory_reset(void);
int sentrius_mgmt_calibrate_thermistor(float c1, float c2, float *ge,
				       float *oe);

#ifdef __cplusplus
}
#endif

#endif /* __SENTRIUS_MGMT_H__ */
