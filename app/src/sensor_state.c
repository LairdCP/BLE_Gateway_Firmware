/**
 * @file sensor_state.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "laird_utility_macros.h"
#include "sensor_state.h"

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
char *get_sensor_state_string(enum sensor_state state)
{
	/* clang-format off */
	switch (state) {
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_DEMO_APP_STATE, FINDING_DEVICE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_DEMO_APP_STATE, FINDING_SERVICE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_DEMO_APP_STATE, FINDING_TEMP_CHAR);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_DEMO_APP_STATE, FINDING_HUMIDITY_CHAR);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_DEMO_APP_STATE, FINDING_PRESSURE_CHAR);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_DEMO_APP_STATE, FINDING_SMP_CHAR);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_DEMO_APP_STATE, CONNECTED_AND_CONFIGURED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_DEMO_APP_STATE, CHALLENGE_REQ);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_DEMO_APP_STATE, CHALLENGE_RSP);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_DEMO_APP_STATE, LOG_DOWNLOAD);
	default:
		return "UNKNOWN";
	}
	/* clang-format on */
}
