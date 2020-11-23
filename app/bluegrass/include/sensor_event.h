/**
 * @file sensor_event.h
 * @brief Sensor event types for Laird BT sensors.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SENSOR_EVENT_H__
#define __SENSOR_EVENT_H__

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

typedef enum MAGNET_STATE { MAGNET_NEAR = 0, MAGNET_FAR } MagnetState_t;

typedef enum SENSOR_EVENT {
	SENSOR_EVENT_RESERVED = 0,
	SENSOR_EVENT_TEMPERATURE = 1,
	SENSOR_EVENT_MAGNET = 2, /* or proximity */
	SENSOR_EVENT_MOVEMENT = 3,
	SENSOR_EVENT_ALARM_HIGH_TEMP_1 = 4,
	SENSOR_EVENT_ALARM_HIGH_TEMP_2 = 5,
	SENSOR_EVENT_ALARM_HIGH_TEMP_CLEAR = 6,
	SENSOR_EVENT_ALARM_LOW_TEMP_1 = 7,
	SENSOR_EVENT_ALARM_LOW_TEMP_2 = 8,
	SENSOR_EVENT_ALARM_LOW_TEMP_CLEAR = 9,
	SENSOR_EVENT_ALARM_DELTA_TEMP = 10,
	SENSOR_EVENT_ALARM_TEMPERATURE_RATE_OF_CHANGE = 11,
	SENSOR_EVENT_BATTERY_GOOD = 12,
	SENSOR_EVENT_ADV_ON_BUTTON = 13,
	SENSOR_EVENT_RESERVED_14 = 14,
	SENSOR_EVENT_IMPACT = 15,
	SENSOR_EVENT_BATTERY_BAD = 16,
	SENSOR_EVENT_RESET = 17,

	NUMBER_OF_SENSOR_EVENTS
} SensorEventType_t;
BUILD_ASSERT(sizeof(SensorEventType_t) <= sizeof(uint8_t),
		 "Sensor Event enum too large");

/* The IG60 publishes events with these names and a voltage or temperature. */
#define IG60_GENERATED_EVENT_STR_BATTERY_GOOD "batteryGood"
#define IG60_GENERATED_EVENT_STR_BATTERY_BAD "batteryBad"
#define IG60_GENERATED_EVENT_STR_ALARM_HIGH_TEMP_1 "alarmHighTemp1"
#define IG60_GENERATED_EVENT_STR_ALARM_HIGH_TEMP_2 "alarmHighTemp2"
#define IG60_GENERATED_EVENT_STR_ALARM_HIGH_TEMP_CLEAR "alarmHighTempClear"
#define IG60_GENERATED_EVENT_STR_ALARM_LOW_TEMP_1 "alarmLowTemp1"
#define IG60_GENERATED_EVENT_STR_ALARM_LOW_TEMP_2 "alarmLowTemp2"
#define IG60_GENERATED_EVENT_STR_ALARM_LOW_TEMP_CLEAR "alarmLowTempClear"
#define IG60_GENERATED_EVENT_STR_ALARM_DELTA_TEMP "alarmDeltaTemp"
#define IG60_GENERATED_EVENT_STR_ADVERTISE_ON_BUTTON "advertiseOnButton"

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_EVENT_H__ */
