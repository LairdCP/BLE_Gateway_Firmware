/**
 * @file button.h
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BUTTON_H__
#define __BUTTON_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
typedef struct button_config {
	/* Durations in milliseconds.  When 0 value isn't used. */
	int64_t min_hold;
	int64_t max_hold;
	int (*callback)(void);
} button_config_t;

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Assign callbacks that occur on release of the button.
 *
 * @note Callbacks occur in ISR context.
 *
 * @param config is a button configuration structure
 * @param count is the number of configurations
 * @param on_press_callback is called when button is pressed
 *
 * @retval 0 on success, negative error code otherwise.
 */
int button_initialize(const button_config_t *const config, size_t count,
		      int (*on_press_callback)(void));

#ifdef __cplusplus
}
#endif

#endif /* __BUTTON_H__ */
