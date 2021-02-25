/**
 * @file wdt.h
 * @brief Watchdog is fed by work queue with a priority lower than main.  This
 * ensures a reset will occur if the main thread does not go idle.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __WDT_H__
#define __WDT_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief Get a user id. Check-ins not required for this user id.
 *
 * @retval > 0 on success, negative on error, 0 is reserved for wdt force.
 */
int wdt_get_user_id(void);

/**
 * @brief Set check-in flag.  Check-ins are now required for this user id.
 *
 * @param id of user
 *
 * @retval negative on error, 0 on success
 */
int wdt_check_in(int id);

/**
 * @brief Pause required check-ins for specified user.
 *
 * @param id of user
 *
 * @retval negative on error, 0 on success
 */
int wdt_pause(int id);

/**
 * @brief Force a watchdog reset.
 *
 * @retval negative on error, 0 on success
 */
int wdt_force(void);

#ifdef __cplusplus
}
#endif

#endif /* __WDT_H__ */
