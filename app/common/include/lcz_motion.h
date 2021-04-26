/**
 * @file lcz_motion.h
 * @brief Allows a notification to be sent on movement of the device.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCZ_MOTION__
#define __LCZ_MOTION__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define MOTION_DEFAULT_THS 10
#define MOTION_DEFAULT_ODR 5
#define MOTION_DEFAULT_SCALE 2
#define MOTION_DEFAULT_DUR 6

struct motion_status {
	int scale;
	int odr;
	int thr;
	int dur;
	uint8_t alarm;
	bool initialized;
};

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
/**
 * @brief Initialize sensor driver
 *
 * @retval negative error code, otherwise 0
 */
int lcz_motion_init(void);

/**
 * @brief Update sensor driver setting and value in non-volatile memory.
 *
 * @retval negative error code, 0 on success
 */
int lcz_motion_set_and_update_odr(int value);
int lcz_motion_set_and_update_scale(int value);
int lcz_motion_set_and_update_threshold(int value);
int lcz_motion_set_and_update_duration(int value);

/**
 * @brief Update sensor driver setting (read value from non-volatile shadow).
 *
 * @retval negative error code, 0 on success
 */
int lcz_motion_update_odr(void);
int lcz_motion_update_scale(void);
int lcz_motion_update_threshold(void);
int lcz_motion_update_duration(void);

/**
 * @brief Get lcz driver wrapper value.
 *
 * @note Some values are scaled before being set in the Zephyr sensor driver.
 *
 * @retval negative error code, 0 on success
 */
int lcz_motion_get_odr(void);
int lcz_motion_get_scale(void);
int lcz_motion_get_threshold(void);
int lcz_motion_get_duration(void);

/**
 * @brief This function retrieves the latest motion status
 *
 * @retval motion status structure.
 */
struct motion_status *lcz_motion_get_status(void);

/**
 * @brief Override this weak implementation in application.
 * Called by lcz_motion driver when motion alarm state changes.
 */
void lcz_motion_set_alarm_state(uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_MOTION__ */