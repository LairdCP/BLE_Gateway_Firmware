/**
 * @file sensor_gateway_parser.c
 * @brief Uses jsmn to parse JSON from AWS that controls gateway functionality
 * and sensor configuration.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(sensor_gateway_parser, LOG_LEVEL_INF);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "aws.h"

#ifdef CONFIG_BOARD_MG100
#include "lairdconnect_battery.h"
#include "ble_motion_service.h"
#include "sdcard_log.h"
#endif

#ifdef CONFIG_CONTACT_TRACING
#include "rpc_params.h"
#endif

#define JSMN_PARENT_LINKS
#define JSMN_HEADER
#include "jsmn.h"
#include "jsmn_json.h"

#include "sensor_cmd.h"
#include "sensor_table.h"
#include "shadow_builder.h"
#include "coap_fota_shadow.h"
#include "FrameworkIncludes.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/

#define CHILD_ARRAY_SIZE 3
#define CHILD_ARRAY_INDEX 0
#define ARRAY_NAME_INDEX 1
#define ARRAY_EPOCH_INDEX 2
#define ARRAY_WLIST_INDEX 3
#define JSMN_NO_CHILDREN 0
#define RECORD_TYPE_INDEX 1
#define EVENT_DATA_INDEX 3

#define GATEWAY_TOPIC_SUB_STR "deviceId-"
#define GET_ACCEPTED_SUB_STR "/get/accepted"
#define SENSOR_SHADOW_PREFIX "$aws/things/"

#define MAX_CONVERSION_STR_SIZE 11
#define MAX_CONVERSION_STR_LEN (MAX_CONVERSION_STR_SIZE - 1)

#ifdef CONFIG_BOARD_MG100
#define MAX_WRITEABLE_LOCAL_OBJECTS 11

#define LOCAL_UPDATE_BIT_BATTERY_0 BIT(0)
#define LOCAL_UPDATE_BIT_BATTERY_1 BIT(1)
#define LOCAL_UPDATE_BIT_BATTERY_2 BIT(2)
#define LOCAL_UPDATE_BIT_BATTERY_3 BIT(3)
#define LOCAL_UPDATE_BIT_BATTERY_4 BIT(4)
#define LOCAL_UPDATE_BIT_BATTERY_BAD BIT(5)
#define LOCAL_UPDATE_BIT_MOTION_THR BIT(6)
#define LOCAL_UPDATE_BIT_MOTION_ODR BIT(7)
#define LOCAL_UPDATE_BIT_MOTION_SCALE BIT(8)
#define LOCAL_UPDATE_BIT_MAX_LOG_SIZE BIT(9)
#define LOCAL_UPDATE_BIT_BATTERY_LOW BIT(10)

static const char BATTERY_BAD_STRING[] = "batteryBadThreshold";
static const char BATTERY_LOW_STRING[] = "batteryLowThreshold";
static const char BATTERY_0_STRING[] = "battery0";
static const char BATTERY_1_STRING[] = "battery1";
static const char BATTERY_2_STRING[] = "battery2";
static const char BATTERY_3_STRING[] = "battery3";
static const char BATTERY_4_STRING[] = "battery4";
static const char ODR_STRING[] = "odr";
static const char SCALE_STRING[] = "scale";
static const char ACT_THRESH_STRING[] = "activationThreshold";
static const char MAX_LOG_SIZE_STRING[] = "maxLogSizeMB";

#define JSON_DEFAULT_BUF_SIZE (1536)
#endif /* CONFIG_BOARD_MG100 */

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static bool getAcceptedTopic;

#ifdef CONFIG_BOARD_MG100
static uint16_t local_updates = 0;

/* NOTE: Order matters. The order of the string array must match
 * the corresponding handler function array below it. Names are
 * based on the MG100 schema.
 */
static const char *WriteableLocalObject[MAX_WRITEABLE_LOCAL_OBJECTS] = {
	BATTERY_LOW_STRING, BATTERY_0_STRING,	BATTERY_1_STRING,
	BATTERY_2_STRING,   BATTERY_3_STRING,	BATTERY_4_STRING,
	BATTERY_BAD_STRING, ODR_STRING,		SCALE_STRING,
	ACT_THRESH_STRING,  MAX_LOG_SIZE_STRING
};

static const uint16_t LocalConfigUpdateBits[MAX_WRITEABLE_LOCAL_OBJECTS] = {
	LOCAL_UPDATE_BIT_BATTERY_BAD,  LOCAL_UPDATE_BIT_BATTERY_0,
	LOCAL_UPDATE_BIT_BATTERY_1,    LOCAL_UPDATE_BIT_BATTERY_2,
	LOCAL_UPDATE_BIT_BATTERY_3,    LOCAL_UPDATE_BIT_BATTERY_4,
	LOCAL_UPDATE_BIT_BATTERY_BAD,  LOCAL_UPDATE_BIT_MOTION_ODR,
	LOCAL_UPDATE_BIT_MOTION_SCALE, LOCAL_UPDATE_BIT_MOTION_THR,
	LOCAL_UPDATE_BIT_MAX_LOG_SIZE
};

static bool (*LocalConfigUpdate[MAX_WRITEABLE_LOCAL_OBJECTS])(int) = {
	UpdateBatteryLowThreshold,
	UpdateBatteryThreshold0,
	UpdateBatteryThreshold1,
	UpdateBatteryThreshold2,
	UpdateBatteryThreshold3,
	UpdateBatteryThreshold4,
	UpdateBatteryBadThreshold,
	UpdateOdr,
	UpdateScale,
	UpdateActivityThreshold,
	UpdateMaxLogSize
};

static int (*LocalConfigGet[MAX_WRITEABLE_LOCAL_OBJECTS])() = {
	GetBatteryLowThreshold,
	GetBatteryThreshold0,
	GetBatteryThreshold1,
	GetBatteryThreshold2,
	GetBatteryThreshold3,
	GetBatteryThreshold4,
	GetBatteryBadThreshold,
	GetOdr,
	GetScale,
	GetActivityThreshold,
	GetMaxLogSize
};
#endif /* CONFIG_BOARD_MG100 */

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void FotaParser(const char *pTopic, enum fota_image_type Type);
static void FotaHostParser(const char *pTopic);
static void FotaBlockSizeParser(const char *pTopic);
static void UnsubscribeToGetAcceptedHandler(void);

#ifdef CONFIG_SENSOR_TASK
static void GatewayParser(const char *pTopic);
static void SensorParser(const char *pTopic);
static void SensorDeltaParser(const char *pTopic);
static void SensorEventLogParser(const char *pTopic);
static void ParseEventArray(const char *pTopic);
static void ParseArray(int ExpectedSensors);
#endif

#if defined(CONFIG_SENSOR_TASK) || defined(CONFIG_BOARD_MG100)
static int FindState(void);
static bool FindUint(uint32_t *pVersion, const char *key);
#endif

#ifdef CONFIG_BOARD_MG100
static void MiniGatewayParser(const char *pTopic);
static bool ValuesUpdated(uint16_t Value);
static void BuildAndSendLocalConfigResponse();
static void BuildAndSendLocalConfigNullResponse();
#endif /* CONFIG_BOARD_MG100 */

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void SensorGatewayParser(const char *pTopic, const char *pJson)
{
	jsmn_start(pJson);
	if (!jsmn_valid()) {
		LOG_ERR("Unable to parse subscription %d", jsmn_tokens_found());
		return;
	}

	getAcceptedTopic = strstr(pTopic, GET_ACCEPTED_SUB_STR) != NULL;
	if (strstr(pTopic, GATEWAY_TOPIC_SUB_STR) != NULL) {
#ifdef CONFIG_SENSOR_TASK
		GatewayParser(pTopic);
#endif
#ifdef CONFIG_BOARD_MG100
		MiniGatewayParser(pTopic);
#endif
#ifdef CONFIG_CONTACT_TRACING
		rpc_params_gateway_parser(getAcceptedTopic);
#endif
		FotaParser(pTopic, APP_IMAGE_TYPE);
		FotaParser(pTopic, MODEM_IMAGE_TYPE);
		FotaHostParser(pTopic);
		FotaBlockSizeParser(pTopic);
		UnsubscribeToGetAcceptedHandler();
	} else {
#ifdef CONFIG_SENSOR_TASK
		SensorParser(pTopic);
#endif
	}

	jsmn_end();
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
#ifdef CONFIG_BOARD_MG100
static void BuildAndSendLocalConfigNullResponse()
{
	size_t size = JSON_DEFAULT_BUF_SIZE;
	JsonMsg_t *pMsg = BufferPool_Take(FWK_BUFFER_MSG_SIZE(JsonMsg_t, size));

	if (pMsg == NULL) {
		return;
	}

	pMsg->header.msgCode = FMC_SENSOR_PUBLISH;
	pMsg->header.rxId = FWK_ID_CLOUD;
	pMsg->size = size;

	ShadowBuilder_Start(pMsg, SKIP_MEMSET);
	/* create the state group */
	ShadowBuilder_StartGroup(pMsg, "state");
	/* create the desired group */
	ShadowBuilder_AddNull(pMsg, "desired");
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_Finalize(pMsg);

	snprintk(pMsg->topic, CONFIG_AWS_TOPIC_MAX_SIZE, "%s",
		 awsGetGatewayUpdateDeltaTopic());

	FRAMEWORK_MSG_SEND(pMsg);
}

static void BuildAndSendLocalConfigResponse()
{
	int index = 0;
	size_t size = JSON_DEFAULT_BUF_SIZE;
	JsonMsg_t *pMsg = BufferPool_Take(FWK_BUFFER_MSG_SIZE(JsonMsg_t, size));

	if (pMsg == NULL) {
		return;
	}

	pMsg->header.msgCode = FMC_SENSOR_PUBLISH;
	pMsg->header.rxId = FWK_ID_CLOUD;
	pMsg->size = size;

	ShadowBuilder_Start(pMsg, SKIP_MEMSET);
	/* create the state group */
	ShadowBuilder_StartGroup(pMsg, "state");
	/* create the desired group */
	ShadowBuilder_StartGroup(pMsg, "desired");
	do {
		/* send "null" if the value was successfully updated */
		if (ValuesUpdated(LocalConfigUpdateBits[index])) {
			ShadowBuilder_AddNull(pMsg,
					      WriteableLocalObject[index]);
		}
	} while (++index < MAX_WRITEABLE_LOCAL_OBJECTS);
	/* end desired */
	ShadowBuilder_EndGroup(pMsg);
	/* create the reported group */
	ShadowBuilder_StartGroup(pMsg, "reported");
	index = 0;
	do {
		ShadowBuilder_AddUint32(pMsg, WriteableLocalObject[index],
					(*LocalConfigGet[index])());
	} while (++index < MAX_WRITEABLE_LOCAL_OBJECTS);
	/* end reported */
	ShadowBuilder_EndGroup(pMsg);
	/* end state */
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_Finalize(pMsg);

	snprintk(pMsg->topic, CONFIG_AWS_TOPIC_MAX_SIZE, "%s",
		 awsGetGatewayUpdateDeltaTopic());

	FRAMEWORK_MSG_SEND(pMsg);
}

static bool ValuesUpdated(uint16_t Value)
{
	return ((Value & local_updates) == Value);
}

static void MiniGatewayParser(const char *pTopic)
{
	ARG_UNUSED(pTopic);
	int objectData = 0;
	int objectIndex = 0;
	bool valueUpdated = false;
	bool configRequestHandled = false;
	uint32_t version = 0;

	/* Now try to find the desired local state for the gateway */
	if (getAcceptedTopic || (FindState() <= 0) ||
	    !FindUint(&version, "version")) {
		return;
	}

	/* Now search for anything under the root of the JSON string. The root will
	 * contain the data for any local configuration items. Names are based on
	 * the MG100 schema (names are unique; heirarchy can be ignored).
	 */
	local_updates = 0;
	do {
		if (FindUint(&objectData, WriteableLocalObject[objectIndex])) {
			/* flag values that have update requests */
			local_updates |= LocalConfigUpdateBits[objectIndex];
			/* execute the local updates */
			valueUpdated =
				(*LocalConfigUpdate[objectIndex])(objectData);
			/* clear the flags of values that were not updated */
			if (!valueUpdated) {
				local_updates &=
					LocalConfigUpdateBits[objectIndex];
			}
			configRequestHandled = true;
		}
	} while (++objectIndex < MAX_WRITEABLE_LOCAL_OBJECTS);

	if (configRequestHandled) {
		BuildAndSendLocalConfigResponse();
		LOG_INF("Local gateway configuration update successful.");
	} else {
		/* null the desired object so it doesn't get repeatedly sent by the server */
		BuildAndSendLocalConfigNullResponse();
		LOG_INF("No local gateway configuration updates found.");
	}

	return;
}

#endif /* CONFIG_BOARD_MG100 */

/**
 * @brief Process $aws/things/deviceId-X/shadow/update/accepted to find sensors
 * that need to be added/removed.
 *
 * Process $aws/things/deviceId-%s/shadow/get/accepted to get the list of
 * sensors when the Pinnacle has reset.
 *
 * @note This function assumes that the AWS task acknowledges the publish so
 * that it isn't repeatedly sent to the gateway.
 */
#ifdef CONFIG_SENSOR_TASK
static void GatewayParser(const char *pTopic)
{
	jsmn_reset_index();

	/* Now try to find {"state": {"bt510": {"sensors": */
	jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	if (getAcceptedTopic) {
		/* Add to heirarchy {"state":{"reported": ... */
		jsmn_find_type("reported", JSMN_OBJECT, NEXT_PARENT);
	}
	jsmn_find_type("bt510", JSMN_OBJECT, NEXT_PARENT);
	jsmn_find_type("sensors", JSMN_ARRAY, NEXT_PARENT);

	if (jsmn_index() > 0) {
		/* Backup one token to get the number of arrays (sensors). */
		int expectedSensors = jsmn_size(jsmn_index() - 1);
		ParseArray(expectedSensors);
	} else {
		LOG_DBG("Did not find sensor array");
		/* It is okay for the list to be empty or non-existant.
		 * When rebooting after talking to sensors - then it
		 * shouldn't be.
		 */
	}
}
#endif

static void UnsubscribeToGetAcceptedHandler(void)
{
	/* Once this has been processed (after reset) we can unsubscribe. */
	if (getAcceptedTopic) {
		FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_CLOUD, FWK_ID_CLOUD,
					      FMC_AWS_GET_ACCEPTED_RECEIVED);
	}
}

static void FotaParser(const char *pTopic, enum fota_image_type Type)
{
	UNUSED_PARAMETER(pTopic);

	int location = 0;
	jsmn_reset_index();

	/* Try to find "state":{"app":{"desired":"2.1.0","switchover":10}} */
	jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	if (getAcceptedTopic) {
		jsmn_find_type("reported", JSMN_OBJECT, NEXT_PARENT);
	}
	jsmn_find_type(coap_fota_get_image_name(Type), JSMN_OBJECT,
		       NEXT_PARENT);

	if (jsmn_index() > 0) {
		jsmn_save_index();

		location = jsmn_find_type(SHADOW_FOTA_DESIRED_STR, JSMN_STRING,
					  NEXT_PARENT);
		if (location > 0) {
			coap_fota_set_desired_version(Type,
						      jsmn_string(location),
						      jsmn_strlen(location));
		}

		jsmn_restore_index();
		location = jsmn_find_type(SHADOW_FOTA_DESIRED_FILENAME_STR,
					  JSMN_STRING, NEXT_PARENT);
		if (location > 0) {
			coap_fota_set_desired_filename(Type,
						       jsmn_string(location),
						       jsmn_strlen(location));
		}

		jsmn_restore_index();
		location = jsmn_find_type(SHADOW_FOTA_SWITCHOVER_STR,
					  JSMN_PRIMITIVE, NEXT_PARENT);
		if (location > 0) {
			coap_fota_set_switchover(Type,
						 jsmn_convert_uint(location));
		}

		jsmn_restore_index();
		location = jsmn_find_type(SHADOW_FOTA_START_STR, JSMN_PRIMITIVE,
					  NEXT_PARENT);
		if (location > 0) {
			coap_fota_set_start(Type, jsmn_convert_uint(location));
		}

		jsmn_restore_index();
		location = jsmn_find_type(SHADOW_FOTA_ERROR_STR, JSMN_PRIMITIVE,
					  NEXT_PARENT);
		if (location > 0) {
			coap_fota_set_error_count(Type,
						  jsmn_convert_uint(location));
		}
	}
}

static void FotaHostParser(const char *pTopic)
{
	UNUSED_PARAMETER(pTopic);

	jsmn_reset_index();

	/* Try to find "state":{"fwBridge":"something.com"}} */
	jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	if (getAcceptedTopic) {
		jsmn_find_type("reported", JSMN_OBJECT, NEXT_PARENT);
	}
	int location = jsmn_find_type(SHADOW_FOTA_BRIDGE_STR, JSMN_STRING,
				      NEXT_PARENT);
	if (location > 0) {
		coap_fota_set_host(jsmn_string(location),
				   jsmn_strlen(location));
	}
}

static void FotaBlockSizeParser(const char *pTopic)
{
	UNUSED_PARAMETER(pTopic);

	jsmn_reset_index();

	jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	if (getAcceptedTopic) {
		jsmn_find_type("reported", JSMN_OBJECT, NEXT_PARENT);
	}
	int location = jsmn_find_type(SHADOW_FOTA_BLOCKSIZE_STR, JSMN_PRIMITIVE,
				      NEXT_PARENT);
	if (location > 0) {
		coap_fota_set_blocksize(jsmn_convert_uint(location));
	}
}

#ifdef CONFIG_SENSOR_TASK
static void SensorParser(const char *pTopic)
{
	if (getAcceptedTopic) {
		SensorEventLogParser(pTopic);
	} else {
		SensorDeltaParser(pTopic);
	}
}

static void SensorDeltaParser(const char *pTopic)
{
	uint32_t version = 0;
	int stateIndex = FindState();
	if (!FindUint(&version, "configVersion") || stateIndex <= 0) {
		return;
	}

	/* The state object contains a string of the values that need to be set. */
	size_t stateLength = jsmn_strlen(stateIndex);
	size_t bufSize = stateLength + strlen(SENSOR_CMD_SET_PREFIX) +
			 strlen(SENSOR_CMD_SUFFIX) + 1;

	SensorCmdMsg_t *pMsg =
		BufferPool_Take(FWK_BUFFER_MSG_SIZE(SensorCmdMsg_t, bufSize));
	if (pMsg != NULL) {
		pMsg->header.msgCode = FMC_CONFIG_REQUEST;
		pMsg->header.txId = FWK_ID_CLOUD;
		pMsg->header.rxId = FWK_ID_SENSOR_TASK;
		pMsg->size = bufSize;
		pMsg->length = (bufSize > 0) ? (bufSize - 1) : 0;

		/* The version in the delta document changes anytime a publsh occurs,
		 * so use a CRC to filter out duplicates. */
		pMsg->configVersion = version;

		memcpy(pMsg->addrString, pTopic + strlen(SENSOR_SHADOW_PREFIX),
		       SENSOR_ADDR_STR_LEN);

		/* Format AWS data into a JSON-RPC set command */
		strcat(pMsg->cmd, SENSOR_CMD_SET_PREFIX);
		/* JSON string isn't null terminated */
		strncat(pMsg->cmd, jsmn_string(stateIndex), stateLength);
		strcat(pMsg->cmd, SENSOR_CMD_SUFFIX);
		FRAMEWORK_DEBUG_ASSERT(strlen(pMsg->cmd) == bufSize - 1);
		FRAMEWORK_MSG_SEND(pMsg);
	}
}

static void SensorEventLogParser(const char *pTopic)
{
	jsmn_reset_index();

	/* Now try to find {"state":{"reported": ... "eventLog":
	 * Parents are required because shadow contains timestamps
	 * ("eventLog" wont be unique). */
	(void)jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	(void)jsmn_find_type("reported", JSMN_OBJECT, NEXT_PARENT);
	(void)jsmn_find_type("eventLog", JSMN_ARRAY, NEXT_PARENT);

	ParseEventArray(pTopic);
}

/**
 * @brief Parse the elements in the anonymous array into a c-structure.
 * (Zephyr library can't handle anonymous arrays.)
 * ["addrString", epoch, whitelist (boolean)]
 * The epoch isn't used.
 */
static void ParseArray(int ExpectedSensors)
{
	if (jsmn_index() <= 0) {
		return;
	}

	SensorWhitelistMsg_t *pMsg =
		BufferPool_Take(sizeof(SensorWhitelistMsg_t));
	if (pMsg == NULL) {
		return;
	}

	size_t maxSensors = MIN(ExpectedSensors, CONFIG_SENSOR_TABLE_SIZE);
	int sensorsFound = 0;
	size_t i = jsmn_index();
	while (((i + CHILD_ARRAY_SIZE) < jsmn_tokens_found()) &&
	       (sensorsFound < maxSensors)) {
		int addrLength = jsmn_strlen(i);
		if ((jsmn_type(i + CHILD_ARRAY_INDEX) == JSMN_ARRAY) &&
		    (jsmn_size(i + CHILD_ARRAY_INDEX) == CHILD_ARRAY_SIZE) &&
		    (jsmn_type(i + ARRAY_NAME_INDEX) == JSMN_STRING) &&
		    (jsmn_size(i + ARRAY_NAME_INDEX) == JSMN_NO_CHILDREN) &&
		    (jsmn_type(i + ARRAY_EPOCH_INDEX) == JSMN_PRIMITIVE) &&
		    (jsmn_size(i + ARRAY_EPOCH_INDEX) == JSMN_NO_CHILDREN) &&
		    (jsmn_type(i + ARRAY_WLIST_INDEX) == JSMN_PRIMITIVE) &&
		    (jsmn_size(i + ARRAY_WLIST_INDEX) == JSMN_NO_CHILDREN)) {
			LOG_DBG("Found array at %d", i);
			strncpy(pMsg->sensors[sensorsFound].addrString,
				jsmn_string(i + ARRAY_NAME_INDEX),
				MIN(addrLength, SENSOR_ADDR_STR_LEN));
			/* The 't' in true is used to determine true/false.
			 * This is safe because primitives are
			 * numbers, true, false, and null. */
			pMsg->sensors[sensorsFound].whitelist =
				(jsmn_string(i + ARRAY_WLIST_INDEX)[0] == 't');
			sensorsFound += 1;
			i += CHILD_ARRAY_SIZE + 1;
		} else {
			LOG_ERR("Gateway Shadow parsing error");
			break;
		}
	}

	pMsg->header.msgCode = FMC_WHITELIST_REQUEST;
	pMsg->header.rxId = FWK_ID_SENSOR_TASK;
	pMsg->sensorCount = sensorsFound;
	FRAMEWORK_MSG_SEND(pMsg);

	LOG_INF("Processed %d of %d sensors in desired list from AWS",
		sensorsFound, ExpectedSensors);
}

static void ParseEventArray(const char *pTopic)
{
	SensorShadowInitMsg_t *pMsg =
		BufferPool_Take(sizeof(SensorShadowInitMsg_t));
	if (pMsg == NULL) {
		return;
	}

	/* If the event log isn't found a message still needs to be sent. */
	int expectedLogs = jsmn_size(jsmn_index() - 1);
	int maxLogs = 0;
	if (jsmn_index() <= 0) {
		LOG_DBG("Could not find event log");
	} else {
		maxLogs = MIN(expectedLogs, CONFIG_SENSOR_LOG_MAX_SIZE);
	}

	/* 1st and 3rd items are hex. {"eventLog":[["01",466280,"0899"]] */
	size_t i = jsmn_index();
	size_t j = 0;
	while (((i + CHILD_ARRAY_SIZE) < jsmn_tokens_found()) &&
	       (j < maxLogs)) {
		if ((jsmn_type(i + CHILD_ARRAY_INDEX) == JSMN_ARRAY) &&
		    (jsmn_size(i + CHILD_ARRAY_INDEX) == CHILD_ARRAY_SIZE) &&
		    (jsmn_type(i + RECORD_TYPE_INDEX) == JSMN_STRING) &&
		    (jsmn_size(i + RECORD_TYPE_INDEX) == JSMN_NO_CHILDREN) &&
		    (jsmn_type(i + ARRAY_EPOCH_INDEX) == JSMN_PRIMITIVE) &&
		    (jsmn_size(i + ARRAY_EPOCH_INDEX) == JSMN_NO_CHILDREN) &&
		    (jsmn_type(i + EVENT_DATA_INDEX) == JSMN_STRING) &&
		    (jsmn_size(i + EVENT_DATA_INDEX) == JSMN_NO_CHILDREN)) {
			LOG_DBG("Found array at %d", i);
			pMsg->events[j].recordType =
				jsmn_convert_hex(i + RECORD_TYPE_INDEX);
			pMsg->events[j].epoch =
				jsmn_convert_uint(i + ARRAY_EPOCH_INDEX);
			pMsg->events[j].data =
				jsmn_convert_hex(i + EVENT_DATA_INDEX);
			LOG_DBG("%u %x,%d,%x", j, pMsg->events[j].recordType,
				pMsg->events[j].epoch, pMsg->events[j].data);
			j += 1;
			i += CHILD_ARRAY_SIZE + 1;
		} else {
			LOG_ERR("Sensor shadow event log parsing error");
			break;
		}
	}

	pMsg->eventCount = j;
	memcpy(pMsg->addrString, pTopic + strlen(SENSOR_SHADOW_PREFIX),
	       SENSOR_ADDR_STR_LEN);
	pMsg->header.msgCode = FMC_SENSOR_SHADOW_INIT;
	pMsg->header.rxId = FWK_ID_SENSOR_TASK;
	LOG_INF("Processed %d of %d sensor events in shadow", pMsg->eventCount,
		expectedLogs);
	FRAMEWORK_MSG_SEND(pMsg);
}
#endif

#if defined(CONFIG_SENSOR_TASK) || defined(CONFIG_BOARD_MG100)
/**
 * @note sets jsonIndex to 1
 *
 * @retval index of state
 */
static int FindState(void)
{
	jsmn_reset_index();
	return jsmn_find_type("state", JSMN_OBJECT, NO_PARENT);
}

/**
 * @retval true if version found, otherwise false
 *
 * @param key is the name of the string to look for
 * @param pValue pointer to value
 *
 * @note sets jsonIndex to 1
 */
static bool FindUint(uint32_t *pValue, const char *key)
{
	jsmn_reset_index();
	int location = jsmn_find_type(key, JSMN_PRIMITIVE, NO_PARENT);
	if (location > 0) {
		*pValue = jsmn_convert_uint(location);
		return true;
	} else {
		*pValue = 0;
		LOG_DBG("%s not found", log_strdup(key));
		return false;
	}
}
#endif
