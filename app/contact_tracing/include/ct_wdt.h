/**
 * @file ct_wdt.h
 * @brief Couples hardware watchdog timer to items being sent to
 * system workqueue.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __CT_WDT_H__
#define __CT_WDT_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define WDOG_FLAGS_SYSWORKQ 1
#define WDOG_FLAGS_ALL (WDOG_FLAGS_SYSWORKQ)

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Initialize contact tracing watchdog
 */
void ct_wdt_init(void);

/**
 * @brief Accessor
 *
 * @retval true if watchdog has been initialized, false otherwise
 */
bool ct_wdt_initialized(void);

/**
 * @brief Test function for forcing watchdog timeout
 */
void ct_wdt_force(void);

/**
 * @brief Feeds watchdog if WDOG_FLAGS_ALL has been set.  Must be called periodically.
 */
void ct_wdt_handler(void);

/**
 * @brief Accessor
 *
 * @retval flags
 */
uint32_t ct_wdt_get_flags(void);

/**
 * @brief Accessor
 *
 * @param flag Bitmask to set
 */
void ct_wdt_set_flags(uint32_t flag);

#ifdef __cplusplus
}
#endif

#endif /* __CT_WDT_H__ */
