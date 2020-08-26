/**
 * @file ble_motion_service.h
 * @brief Allows the a notification to be sent on movement of the device.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLE_MOTION_SERVICE_H__
#define __BLE_MOTION_SERVICE_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/conn.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define MOTION_DEFAULT_THS		10
#define MOTION_DEFAULT_ODR		5
#define MOTION_DEFAULT_SCALE	2
#define MOTION_DEFAULT_DUR		6

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
struct motion_status {
	int scale;
	int odr;
	int thr;
	u8_t motion;
};
/**
 * @brief this function retrieves the latest motion status
 *
 * @param none
 *
 * @retval motion status structure.
 */
struct motion_status *motionGetStatus();

/**
 * @brief this function is called by the gateway JSON parser to set a parameter
 *  in the accelerometer.
 *
 * @param int Value - the value
 *
 * @retval none
 */
bool UpdateOdr(int Value);

/**
 * @brief this function is called by the gateway JSON parser to set a parameter
 *  in the accelerometer.
 *
 * @param int Value - the value
 *
 * @retval none
 */
bool UpdateScale(int Value);

/**
 * @brief this function is called by the gateway JSON parser to set a parameter
 *  in the accelerometer.
 *
 * @param int Value - the value
 *
 * @retval none
 */
bool UpdateActivityThreshold(int Value);

/**
 * @brief this function is called by the gateway JSON parser to get a parameter
 *  in the accelerometer.
 *
 * @param int * Value - the value read from NVS
 *
 * @retval none
 */
int GetOdr();

/**
 * @brief this function is called by the gateway JSON parser to get a parameter
 *  in the accelerometer.
 *
 * @param int * Value - the value read from NVS
 *
 * @retval none
 */
int GetScale();

/**
 * @brief this function is called by the gateway JSON parser to get a parameter
 *  in the accelerometer.
 *
 * @param int * Value - the value read from NVS
 *
 * @retval none
 */
int GetActivityThreshold();

void motion_svc_init();
void motion_svc_set_alarm_state(u8_t alarmState);

#ifdef __cplusplus
}
#endif

#endif /* __BLE_MOTION_SERVICE_H__ */