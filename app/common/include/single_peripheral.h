/**
 * @file single_peripheral.h
 * @brief Connection handler for a device that supports 1 connection as a
 * a peripheral.  This module is initialized during system initialization.
 *
 * Copyright (c) 2021 Laird Connectivity
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
 * @brief Get connection handle for a connection in which this device is the
 * peripheral (a central has connected to this device).
 */
struct bt_conn *single_peripheral_get_conn(void);

/**
 * @brief Advertise as connectable with name and 128-bit UUID of
 * Laird Connectivity's Custom Cellular Service.
 *
 * @note Request is pushed to system workqueue.  This can be called from
 * interrupt context.
 */
void single_peripheral_start_advertising(void);

/**
 * @brief Helper function
 *
 * @note This can be called from interrupt context.
 */
void single_peripheral_stop_advertising(void);

/**
 * @brief Accessor
 *
 * @return true if advertising or trying to pair
 * @return false otherwise
 */
bool single_peripheral_security_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* __SINGLE_PERIPHERAL_H__ */
