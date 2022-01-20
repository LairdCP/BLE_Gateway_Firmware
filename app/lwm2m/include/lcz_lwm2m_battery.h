/**
 * @file lcz_lwm2m_battery.h
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCZ_LWM2M_BATTERY_H__
#define __LCZ_LWM2M_BATTERY_H__

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
struct lwm2m_battery_obj_cfg {
	uint16_t instance;
	uint8_t level;
	double voltage;
};

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Create battery object.  Object type must be enabled in Zephyr.
 *
 * @param cfg @ref lwm2m_battery_obj_cfg
 * @return int zero on success, otherwise negative error code
 */
int lcz_lwm2m_battery_create(struct lwm2m_battery_obj_cfg *cfg);

/**
 * @brief Set battery level in battery object
 *
 * @param instance each device containing a battery has a unique instance
 * @param level battery level in percent (%) 0 - 100
 * @return int zero on success, otherwise negative error code
 */
int lcz_lwm2m_battery_level_set(uint16_t instance, uint8_t level);

/**
 * @brief Set battery voltage in battery object
 *
 * @param instance each device containing a battery has a unique instance
 * @param voltage battery voltage in volts
 * @return int zero on success, otherwise negative error code
 */
int lcz_lwm2m_battery_voltage_set(uint16_t instance, double *voltage);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_LWM2M_BATTERY_H__ */
