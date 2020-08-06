/**
 * @file ble_sensor_service.h
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLE_SENSOR_SERVICE_H__
#define __BLE_SENSOR_SERVICE_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/conn.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/* For multi-peripheral device the weak implementation can be overriden. */
struct bt_conn *bss_get_conn(void);

void bss_set_sensor_state(uint8_t state);

/**
 * @param addr If NULL then sensor bt addr string is cleared.
 */
void bss_set_sensor_bt_addr(char *addr);

/**
 * @brief Initialize the sensor service
 */
void bss_init();

#ifdef __cplusplus
}
#endif

#endif /* __BLE_SENSOR_SERVICE_H__ */
