/**
 * @file bt_scan.h
 * @brief Controls Bluetooth scanning for multiple centrals/observers with a
 * counting semaphore.
 *
 * @note Scanning must be disabled in order to connect.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BT_SCAN_H__
#define __BT_SCAN_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts scanning if the number of stop requests is zero.
 */
void bt_scan_start(void);

/**
 * @brief Stops scanning and increments the number of stop requests.
 */
void bt_scan_stop(void);

/**
 * @brief Decrements the number of stop requests and then calls bt_scan_start.
 *
 * @note A user should only call this once for each time that it has called
 * stop.
 */
void bt_scan_resume(void);

#ifdef __cplusplus
}
#endif

#endif /* __BT_SCAN_H__ */
