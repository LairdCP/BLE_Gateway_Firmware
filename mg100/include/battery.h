/**
 * @file battery.h
 * @brief this module implements the battery metering and managment task.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BATTERY_H__
#define __BATTERY_H__

/* (Remove Empty Sections) */
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

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
/* values used to report remaining battery capacity */
enum battery_status
{
    BATTERY_STATUS_0 = 0,
    BATTERY_STATUS_1,
	BATTERY_STATUS_2,
	BATTERY_STATUS_3,
    BATTERY_STATUS_4,
};

/* values used to access the threshold voltages */
enum battery_thresh_idx
{
    BATTERY_IDX_0 = 0,
    BATTERY_IDX_1,
	BATTERY_IDX_2,
	BATTERY_IDX_3,
    BATTERY_IDX_4,
    BATTERY_IDX_LOW,
    BATTERY_IDX_ALARM,
    BATTERY_IDX_MAX,
};

#define BATTERY_SUCCESS 0
#define BATTERY_FAIL 1

#define BATTERY_ALARM_ACTIVE 1
#define BATTERY_ALARM_INACTIVE 0
/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief this function initializes the battery management sub-system. It must
 * be executed after nvInit because it relies on nv data.
 *
 * @param none
 *
 * @retval none
 */
void BatteryInit();

/**
 * @brief this function returns the remaining battery capacity
 *
 * @param Voltage - the raw voltage in mV.
 *
 * @retval an enum value indicating the remaining battery capacity as a
 *          threshold value.
 */
enum battery_status BatteryCalculateRemainingCapacity(u16_t Voltage);

/**
 * @brief this function sets the various thresholds used for battery metering.
 *          this allows battery metering behavior to be changed without the need
 *          for a firmware update.
 *
 * @param Thresh - an enumerated value indicating which threshold is being set
 * @param Value - a u16_t value indicating the voltage level for the threshold
 *
 * @retval u8 error code - 0 Success, 1 Failure
 */
u8_t BatterySetThresholds(enum battery_thresh_idx Thresh, u16_t Value);

/**
 * @brief this function gets the various thresholds used for battery metering.
 *
 * @param Thresh - an enumerated value indicating which threshold is being retrieved
 *
 * @retval a u16_t value indicating the voltage level for the threshold
 */
u16_t BatteryGetThresholds(enum battery_thresh_idx Thresh);

/**
 * @brief this function selects which threshold triggers a warning
 *
 * @param Thresh - an enumerated value indicating which threshold is selected
 *
 * @retval u8 error code - 0 Success, 1 Failure
 */
u8_t BatterySetWarning(enum battery_status Thresh);

/**
 * @brief this function gets the state of the battery charger.
 *
 * @param none
 *
 * @retval u8_t value indicating the state of the battery charger.
 *          bit 0 - power state: 0 = no external power, 1 = external power is present
 *          bit 1 - charger state: 0 = discharging, 1 = charging
 */
u8_t BatteryGetChgState();

#ifdef __cplusplus
}
#endif

#endif /* __BATTERY_H__ */
