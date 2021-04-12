/**
 * @file sensor_state.h
 * @brief State enumeration for BLE central state. States may be specific to
 * a specific type of sensor (BL654, BT510-CT, BT710).
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLE_STATE_H__
#define __BLE_STATE_H__

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
enum sensor_state {
	/* Scanning for remote sensor */
	BT_DEMO_APP_STATE_FINDING_DEVICE = 0,
	/* Searching for ESS service */
	BT_DEMO_APP_STATE_FINDING_SERVICE,
	/* Searching for ESS Temperature characteristic */
	BT_DEMO_APP_STATE_FINDING_TEMP_CHAR,
	/* Searching for ESS Humidity characteristic */
	BT_DEMO_APP_STATE_FINDING_HUMIDITY_CHAR,
	/* Searching for ESS Pressure characteristic */
	BT_DEMO_APP_STATE_FINDING_PRESSURE_CHAR,
	/* All characteristics were found and subscribed to */
	BT_DEMO_APP_STATE_CONNECTED_AND_CONFIGURED,
	/* Searching for SMP characteristic */
	BT_DEMO_APP_STATE_FINDING_SMP_CHAR,
	/* Authentication process - request challenge */
	BT_DEMO_APP_STATE_CHALLENGE_REQ,
	/* Authentication process - challenge response  */
	BT_DEMO_APP_STATE_CHALLENGE_RSP,
	/* Downloading logs */
	BT_DEMO_APP_STATE_LOG_DOWNLOAD,
};

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Helper function
 *
 * @param state of sensor
 *
 * @retval string representation of state
 */
char *get_sensor_state_string(enum sensor_state state);

#ifdef __cplusplus
}
#endif

#endif /* __BLE_STATE_H__ */
