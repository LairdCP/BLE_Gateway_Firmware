/**
 * @file ble_battery_service.h
 * @brief Allows voltage to be read/notified and reset to be issued.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLE_BATTERY_SERVICE_H__
#define __BLE_BATTERY_SERVICE_H__

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

/** @param function that battery service should use to get connection handle when
 * determining if a value should by notified.
 */
void battery_svc_assign_connection_handler_getter(
	struct bt_conn *(*function)(void));

void battery_svc_init();
void battery_svc_set_battery(u16_t voltage, u8_t capacity);
void battery_svc_set_chg_state(u8_t chgState);
void battery_svc_set_alarm_state(u8_t alarmState);
void battery_svc_update_data();

#ifdef __cplusplus
}
#endif

#endif /* __BLE_POWER_SERVICE_H__ */