/**
 * @file ble.h
 * @brief This module enables Bluetooth during system initialization.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLE_H__
#define __BLE_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Update Bluetooth device name.  This name is used in advertisements.
 *
 * @param imei from SIM card.
 *
 */
void ble_update_name(const char *imei);

#ifdef __cplusplus
}
#endif

#endif /* __BLE_H__ */
