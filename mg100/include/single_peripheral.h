/**
 * @file single_peripheral.h
 * @brief Connection handler for a device that supports 1 connection as a
 * a peripheral.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SINGLE_PERIPHERAL_H__
#define __SINGLE_PERIPHERAL_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief Starts advertising.
 */
void single_peripheral_initialize(void);

/**
 * @brief Get connection handle for a connection where this device is the
 * peripheral (a central has connected to this device).
 */
struct bt_conn *single_peripheral_get_conn(void);

/**
 * @brief Advertise as connectable with name and 128-bit UUID of
 * Laird Connectivity's Custom Cellular Service.
 */
int start_advertising(void);

#ifdef __cplusplus
}
#endif

#endif /* __SINGLE_PERIPHERAL_H__ */
