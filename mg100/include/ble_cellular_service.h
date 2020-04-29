/**
 * @file ble_cellular_service.h
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLE_CELLULAR_SERVICE_H__
#define __BLE_CELLULAR_SERVICE_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <drivers/modem/hl7800.h>
#include <bluetooth/conn.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @param function that sensor service should use to get connection handle when
 * determining if a value should by notified.
 */
void cell_svc_assign_connection_handler_getter(
	struct bt_conn *(*function)(void));

void cell_svc_init();
void cell_svc_set_imei(const char *imei);
void cell_svc_set_network_state(u8_t state);
void cell_svc_set_startup_state(u8_t state);
void cell_svc_set_sleep_state(u8_t state);
void cell_svc_set_apn(struct mdm_hl7800_apn *access_point);
void cell_svc_set_rssi(int value);
void cell_svc_set_sinr(int value);
void cell_svc_set_fw_ver(const char *ver);
void cell_svc_set_rat(u8_t value);
void cell_svc_set_iccid(const char *value);
void cell_svc_set_serial_number(const char *value);

#ifdef __cplusplus
}
#endif

#endif /* __BLE_CELLULAR_SERVICE_H__ */
