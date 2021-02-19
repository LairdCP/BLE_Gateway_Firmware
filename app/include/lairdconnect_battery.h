/**
 * @file laird_battery.h
 * @brief this module implements the battery metering and management task.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BATTERY_H__
#define __BATTERY_H__

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
struct battery_data {
	int batteryVoltage;
	int batteryCapacity;
	int batteryThreshold0;
	int batteryThreshold1;
	int batteryThreshold2;
	int batteryThreshold3;
	int batteryThreshold4;
	int batteryThresholdGood;
	int batteryThresholdBad;
	int batteryThresholdLow;
	int batteryChgState;
	int ambientTemperature;
};

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
/* values used to report remaining battery capacity */
enum battery_status {
	BATTERY_STATUS_0 = 0,
	BATTERY_STATUS_1,
	BATTERY_STATUS_2,
	BATTERY_STATUS_3,
	BATTERY_STATUS_4,
};

/* values used to access the threshold voltages */
enum battery_thresh_idx {
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

#define BATTERY_MV_PER_V 1000
/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief this function retrieves the latest information about the battery.
 *
 * @param none
 *
 * @retval a pointer to the battery data structure.
 */
struct battery_data *batteryGetStatus();

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
enum battery_status BatteryCalculateRemainingCapacity(uint16_t Voltage);

/**
 * @brief this function sets the various thresholds used for battery metering.
 *          this allows battery metering behavior to be changed without the need
 *          for a firmware update.
 *
 * @param Thresh - an enumerated value indicating which threshold is being set
 * @param Value - a uint16_t value indicating the voltage level for the threshold
 *
 * @retval u8 error code - 0 Success, 1 Failure
 */
uint8_t BatterySetThresholds(enum battery_thresh_idx Thresh, uint16_t Value);

/**
 * @brief this function gets the various thresholds used for battery metering.
 *
 * @param Thresh - an enumerated value indicating which threshold is being retrieved
 *
 * @retval a uint16_t value indicating the voltage level for the threshold
 */
uint16_t BatteryGetThresholds(enum battery_thresh_idx Thresh);

/**
 * @brief this function selects which threshold triggers a warning
 *
 * @param Thresh - an enumerated value indicating which threshold is selected
 *
 * @retval u8 error code - 0 Success, 1 Failure
 */
uint8_t BatterySetWarning(enum battery_status Thresh);

/**
 * @brief this function gets the state of the battery charger.
 *
 * @param none
 *
 * @retval uint8_t value indicating the state of the battery charger.
 *          bit 0 - power state: 0 = no external power, 1 = external power is present
 *          bit 1 - charger state: 0 = discharging, 1 = charging
 */
uint8_t BatteryGetChgState();

/**
 * @brief this function is called by the gateway JSON parser to set a battery theshold.
 *
 * @param int Value - the value for the threshold in millivolts.
 *
 * @retval none
 */
bool UpdateBatteryThreshold0(int Value);

/**
 * @brief this function is called by the gateway JSON parser to set a battery theshold.
 *
 * @param int Value - the value for the threshold in millivolts.
 *
 * @retval none
 */
bool UpdateBatteryThreshold1(int Value);

/**
 * @brief this function is called by the gateway JSON parser to set a battery theshold.
 *
 * @param int Value - the value for the threshold in millivolts.
 *
 * @retval none
 */
bool UpdateBatteryThreshold2(int Value);

/**
 * @brief this function is called by the gateway JSON parser to set a battery theshold.
 *
 * @param int Value - the value for the threshold in millivolts.
 *
 * @retval none
 */
bool UpdateBatteryThreshold3(int Value);

/**
 * @brief this function is called by the gateway JSON parser to set a battery theshold.
 *
 * @param int Value - the value for the threshold in millivolts.
 *
 * @retval none
 */
bool UpdateBatteryThreshold4(int Value);

/**
 * @brief this function is called by the gateway JSON parser to set a battery theshold.
 *        the low battery value can be different than the bad threshold. Bad is the
 *        the threshold at which an alarm is generated.
 *
 * @param int Value - the value for the threshold in millivolts.
 *
 * @retval none
 */

bool UpdateBatteryLowThreshold(int Value);

/**
 * @brief this function is called by the gateway JSON parser to set a battery theshold.
 *        the low battery value can be different than the bad threshold. Bad is the
 *        the threshold at which an alarm is generated.
 *
 * @param int Value - the value for the threshold in millivolts.
 *
 * @retval none
 */
bool UpdateBatteryBadThreshold(int Value);

int GetBatteryThreshold0();
int GetBatteryThreshold1();
int GetBatteryThreshold2();
int GetBatteryThreshold3();
int GetBatteryThreshold4();
int GetBatteryLowThreshold();
int GetBatteryBadThreshold();

#ifdef __cplusplus
}
#endif

#endif /* __BATTERY_H__ */
