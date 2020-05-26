/**
 * @file sensor_cmd.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <string.h>
#include <misc/util.h>

#include "sensor_cmd.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
static const char *attributesThatRequireReset[] = {
	"sensorName", "advertisingInterval", "advertisingDuration",
	"passkey",    "activeMode",	     "useCodedPhy"
};

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
const char SENSOR_CMD_SET_PREFIX[] =
	"{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"set\",\"params\":";

const char SENSOR_CMD_SUFFIX[] = "}";

const char SENSOR_CMD_DUMP[] =
	"{\"jsonrpc\":\"2.0\",\"method\":\"dump\",\"id\":1}";

const char SENSOR_CMD_REBOOT[] =
	"{\"jsonrpc\": \"2.0\",\"method\":\"reboot\",\"id\":2}";

const char SENSOR_CMD_ACCEPTED_SUB_STR[] = "\"result\":\"ok\"";

const char SENSOR_CMD_DEFAULT_QUERY[] =
	"{\"jsonrpc\":\"2.0\",\"method\":\"get\",\"id\":4,\"params\":[\"sensorName\",\"hardwareMinorVersion\",\"location\",\"advertisingInterval\",\"advertisingDuration\",\"connectionTimeout\",\"passkey\",\"lock\",\"batterySenseInterval\",\"temperatureAggregationCount\",\"temperatureSenseInterval\",\"highTemperatureAlarmThreshold1\",\"highTemperatureAlarmThreshold2\",\"lowTemperatureAlarmThreshold1\",\"lowTemperatureAlarmThreshold2\",\"deltaTemperatureAlarmTheshold\",\"odr\",\"scale\",\"activationThreshold\",\"returnToSleepDuration\",\"tempCc\",\"batteryVoltageMv\",\"magnetState\",\"highTemperatureAlarm\",\"lowTemperatureAlarm\",\"deltaTemperatureAlarm\",\"movementAlarm\",\"hwVersion\",\"firmwareVersion\",\"resetReason\",\"bluetoothAddress\",\"activeMode\",\"flags\",\"resetCount\",\"useCodedPhy\",\"txPower\",\"networkId\",\"configVersion\",\"bootloaderVersion\"]}";

const char SENSOR_CMD_SET_CONFIG_VERSION_1[] =
	"{\"jsonrpc\":\"2.0\",\"method\":\"set\",\"id\":5,\"params\":{\"activeMode\":1,\"scale\":2,\"odr\":5,\"activationThreshold\":8,\"temperatureSenseInterval\":120,\"batterySenseInterval\":3600,\"configVersion\":1}}";

const char SENSOR_CMD_SET_EPOCH_FMT_STR[] =
	"{\"jsonrpc\": \"2.0\", \"method\": \"setEpoch\", \"params\": [%u], \"id\": 6}";

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/

bool SensorCmd_RequiresReset(char *pCmd)
{
	size_t i;
	for (i = 0; i < ARRAY_SIZE(attributesThatRequireReset); i++) {
		if (strstr(pCmd, attributesThatRequireReset[i]) != NULL) {
			return true;
		}
	}
	return false;
}
