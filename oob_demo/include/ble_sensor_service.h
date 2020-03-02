/*
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BLE_SENSOR_SERVICE_H__
#define __BLE_SENSOR_SERVICE_H__

#include <bluetooth/conn.h>
#include <zephyr/types.h>

/** @param function that sensor service should use to get connection handle when
 * determining if a value should by notified.
 */
void bss_assign_connection_handler_getter(struct bt_conn *(*function)(void));

void bss_set_sensor_state(u8_t state);

/** @param addr If NULL then sensor bt addr string is cleared.
 */
void bss_set_sensor_bt_addr(char *addr);

/** Initialize the sensor service
 */
void bss_init();

#endif