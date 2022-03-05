/**
 * @file sensor_shadow_parser.c
 * @brief Uses jsmn to parse JSON from AWS that controls BT510 sensor configuration.
 *
 * @note The parsers do not process the "desired" section of the get accepted
 * data.  It is processed when the delta topic is received.
 *
 * Copyright (c) 2020-2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <logging/log.h>
LOG_MODULE_REGISTER(sensor_shadow_parser, CONFIG_SHADOW_PARSER_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>
#include <init.h>

#define JSMN_PARENT_LINKS
#define JSMN_HEADER
#include "jsmn.h"
#include "jsmn_json.h"

#include "FrameworkIncludes.h"
#include "sensor_cmd.h"
#include "sensor_table.h"

#include "shadow_parser.h"

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

#define SENSOR_SHADOW_PREFIX "$aws/things/"

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct shadow_parser_agent sensor_agent;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void shadow_parser(const char *topic, struct topic_flags flags);

static void gateway_parser(const char *topic, struct topic_flags flags);
static void sensor_parser(const char *topic, struct topic_flags flags);
static void sensor_delta_parser(const char *topic, struct topic_flags flags);
static void sensor_event_log_parser(const char *topic,
				    struct topic_flags flags);
static void parse_event_array(const char *topic, struct topic_flags flags);
static void parse_array(int expected_sensors);

/******************************************************************************/
/* Init                                                                       */
/******************************************************************************/
static int sensor_shadow_parser_init(const struct device *device)
{
	ARG_UNUSED(device);

	sensor_agent.parser = shadow_parser;
	shadow_parser_register_agent(&sensor_agent);

	return 0;
}

SYS_INIT(sensor_shadow_parser_init, APPLICATION, 99);

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void shadow_parser(const char *topic, struct topic_flags flags)
{
	if (flags.gateway) {
		gateway_parser(topic, tf);
	} else {
		sensor_parser(topic, tf);
	}
}

static void gateway_parser(const char *topic, struct topic_flags flags)
{
	jsmn_reset_index();

	/* Now try to find {"state": {"bt510": {"sensors": */
	jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	if (flags.get_accepted) {
		/* Add to heirarchy {"state":{"reported": ... */
		jsmn_find_type("reported", JSMN_OBJECT, NEXT_PARENT);
	}
	jsmn_find_type("bt510", JSMN_OBJECT, NEXT_PARENT);
	jsmn_find_type("sensors", JSMN_ARRAY, NEXT_PARENT);

	if (jsmn_index() > 0) {
		/* Backup one token to get the number of arrays (sensors). */
		int expectedSensors = jsmn_size(jsmn_index() - 1);
		parse_array(expectedSensors);
	} else {
		LOG_DBG("Did not find sensor array");
		/* It is okay for the list to be empty or non-existant.
		 * When rebooting after talking to sensors - then it
		 * shouldn't be.
		 */
	}
}

static void sensor_parser(const char *topic, struct topic_flags flags)
{
	if (flags.get_accepted) {
		sensor_event_log_parser(topic);
	} else {
		sensor_delta_parser(topic);
	}
}

static void sensor_delta_parser(const char *topic, struct topic_flags flags)
{
	uint32_t version = 0;
	int state_index = shadow_parser_find_state();
	if (!shadow_parser_find_uint(&version, "configVersion") ||
	    state_index <= 0) {
		return;
	}

	/* The state object contains a string of the values that need to be set. */
	size_t state_len = jsmn_strlen(state_index);
	size_t buf_size = state_len + strlen(SENSOR_CMD_SET_PREFIX) +
			  strlen(SENSOR_CMD_SUFFIX) + 1;

	SensorCmdMsg_t *pMsg =
		BP_TRY_TO_TAKE(FWK_BUFFER_MSG_SIZE(SensorCmdMsg_t, buf_size));
	if (pMsg != NULL) {
		pMsg->header.msgCode = FMC_CONFIG_REQUEST;
		pMsg->header.txId = FWK_ID_CLOUD;
		pMsg->header.rxId = FWK_ID_SENSOR_TASK;
		pMsg->size = buf_size;
		pMsg->length = (buf_size > 0) ? (buf_size - 1) : 0;

		/* The AWS generated version in the delta document changes anytime
		 * a publsh occurs.  "configVersion" is used to filter duplicates.
		 */
		pMsg->configVersion = version;

		memcpy(pMsg->addrString, topic + strlen(SENSOR_SHADOW_PREFIX),
		       SENSOR_ADDR_STR_LEN);

		/* Format AWS data into a JSON-RPC set command */
		strcat(pMsg->cmd, SENSOR_CMD_SET_PREFIX);
		/* JSON string isn't null terminated */
		strncat(pMsg->cmd, jsmn_string(state_index), state_len);
		strcat(pMsg->cmd, SENSOR_CMD_SUFFIX);
		FRAMEWORK_DEBUG_ASSERT(strlen(pMsg->cmd) == buf_size - 1);
		FRAMEWORK_MSG_SEND(pMsg);
	}
}

static void sensor_event_log_parser(const char *topic,
				    struct topic_flags flags)
{
	jsmn_reset_index();

	/* Now try to find {"state":{"reported": ... "eventLog":
	 * Parents are required because shadow contains timestamps
	 * ("eventLog" wont be unique).
	 */
	(void)jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	(void)jsmn_find_type("reported", JSMN_OBJECT, NEXT_PARENT);
	(void)jsmn_find_type("eventLog", JSMN_ARRAY, NEXT_PARENT);

	parse_event_array(topic);
}

/**
 * @brief Parse the elements in the anonymous array into a c-structure.
 * (Zephyr library can't handle anonymous arrays.)
 * ["addrString", epoch, greenlist (boolean)]
 * The epoch isn't used.
 */
static void parse_array(int expected_sensors)
{
	if (jsmn_index() <= 0) {
		return;
	}

	SensorGreenlistMsg_t *pMsg =
		BP_TRY_TO_TAKE(sizeof(SensorGreenlistMsg_t));
	if (pMsg == NULL) {
		return;
	}

	size_t max_sensors = MIN(expected_sensors, CONFIG_SENSOR_TABLE_SIZE);
	int sensors_found = 0;
	size_t i = jsmn_index();
	while (((i + CHILD_ARRAY_SIZE) < jsmn_tokens_found()) &&
	       (sensors_found < max_sensors)) {
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
			strncpy(pMsg->sensors[sensors_found].addrString,
				jsmn_string(i + ARRAY_NAME_INDEX),
				MIN(addrLength, SENSOR_ADDR_STR_LEN));
			/* The 't' in true is used to determine true/false.
			 * This is safe because primitives are
			 * numbers, true, false, and null. */
			pMsg->sensors[sensors_found].greenlist =
				(jsmn_string(i + ARRAY_WLIST_INDEX)[0] == 't');
			sensors_found += 1;
			i += CHILD_ARRAY_SIZE + 1;
		} else {
			LOG_ERR("Gateway Shadow parsing error");
			break;
		}
	}

	pMsg->header.msgCode = FMC_GREENLIST_REQUEST;
	pMsg->header.rxId = FWK_ID_SENSOR_TASK;
	pMsg->sensorCount = sensors_found;
	FRAMEWORK_MSG_SEND(pMsg);

	LOG_INF("Processed %d of %d sensors in desired list from AWS",
		sensors_found, expected_sensors);
}

static void parse_event_array(const char *topic, struct topic_flags flags)
{
	SensorShadowInitMsg_t *pMsg =
		BP_TRY_TO_TAKE(sizeof(SensorShadowInitMsg_t));
	if (pMsg == NULL) {
		return;
	}

	/* If the event log isn't found a message still needs to be sent. */
	int expected_logs = jsmn_size(jsmn_index() - 1);
	int max_logs = 0;
	if (jsmn_index() <= 0) {
		LOG_DBG("Could not find event log");
	} else {
		max_logs = MIN(expected_logs, CONFIG_SENSOR_LOG_MAX_SIZE);
	}

	/* 1st and 3rd items are hex. {"eventLog":[["01",466280,"0899"]] */
	size_t i = jsmn_index();
	size_t j = 0;
	while (((i + CHILD_ARRAY_SIZE) < jsmn_tokens_found()) &&
	       (j < max_logs)) {
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
	memcpy(pMsg->addrString, topic + strlen(SENSOR_SHADOW_PREFIX),
	       SENSOR_ADDR_STR_LEN);
	pMsg->header.msgCode = FMC_SENSOR_SHADOW_INIT;
	pMsg->header.rxId = FWK_ID_SENSOR_TASK;
	LOG_INF("Processed %d of %d sensor events in shadow", pMsg->eventCount,
		expected_logs);
	FRAMEWORK_MSG_SEND(pMsg);
}
