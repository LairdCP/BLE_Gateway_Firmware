/**
 * @file bl654_sensor.h
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BL654_SENSOR_H__
#define __BL654_SENSOR_H__

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
void bl654_sensor_initialize(void);

/**
 * @brief If BL654 sensor is connected, then disconnect.
 *
 * @return int zero on success or negative error code on failure
 */
int bl654_sensor_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* __BL654_SENSOR_H__ */
