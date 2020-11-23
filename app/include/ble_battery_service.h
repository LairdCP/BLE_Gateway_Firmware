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
/* For multi-peripheral device the weak implementation can be overriden. */
struct bt_conn *battery_svc_get_conn(void);

void battery_svc_init();
void battery_svc_set_battery(uint16_t voltage, uint8_t capacity);
void battery_svc_set_chg_state(uint8_t chgState);
void battery_svc_set_alarm_state(uint8_t alarmState);
void battery_svc_update_data();

#ifdef __cplusplus
}
#endif

#endif /* __BLE_POWER_SERVICE_H__ */