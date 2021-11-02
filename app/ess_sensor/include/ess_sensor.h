/**
 * @file ess_sensor.h
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __ESS_SENSOR_H__
#define __ESS_SENSOR_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/bluetooth.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
void ess_sensor_initialize(void);

/**
 * @brief If ESS sensor is connected, then disconnect.
 *
 * @return int zero on success or negative error code on failure
 */
int ess_sensor_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESS_SENSOR_H__ */
