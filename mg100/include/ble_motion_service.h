/**
 * @file ble_motion_service.h
 * @brief Allows the a notification to be sent on movement of the device.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLE_MOTION_SERVICE_H__
#define __BLE_MOTION_SERVICE_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/conn.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/

/** @param function that motion service should use to get connection handle when
 * determining if a value should by notified.
 */
void motion_svc_assign_connection_handler_getter(
	struct bt_conn *(*function)(void));

void motion_svc_init();
void motion_svc_set_alarm_state(u8_t alarmState);

#ifdef __cplusplus
}
#endif

#endif /* __BLE_MOTION_SERVICE_H__ */