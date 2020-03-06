/*
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MG100_BLE_H__
#define __MG100_BLE_H__

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <misc/byteorder.h>

/* Callback function for passing sensor data */
typedef void (*sensor_updated_function_t)(u8_t sensor, s32_t reading);

/* Function for initialising the BLE portion of the MG100 */
void mg100_ble_initialise(const char *imei);

/* Function for setting the sensor read callback function */
void mg100_ble_set_callback(sensor_updated_function_t func);

struct bt_conn *mg100_ble_get_central_connection(void);

#endif /* __MG100_BLE_H__ */
