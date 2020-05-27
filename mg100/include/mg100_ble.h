/**
 * @file mg100_ble.h
 * @brief MG100 BLE API
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __MG100_BLE_H__
#define __MG100_BLE_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <misc/byteorder.h>
#include "FrameworkIncludes.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
/* Callback function for passing sensor data */
typedef void (*sensor_updated_function_t)(u8_t sensor, s32_t reading);

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/* Function for initialising the BLE portion of the MG100 demo */
void mg100_ble_initialise(const char *imei);

/* Function for setting the sensor read callback function */
void mg100_ble_set_callback(sensor_updated_function_t func);

struct bt_conn *mg100_ble_get_central_connection(void);

void bl654_sensor_adv_handler(const bt_addr_le_t *addr, s8_t rssi, u8_t type,
			      Ad_t *ad);

#ifdef __cplusplus
}
#endif

#endif /* __MG100_BLE_H__ */
