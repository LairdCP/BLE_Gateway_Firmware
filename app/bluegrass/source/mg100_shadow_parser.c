/**
 * @file mg100_shadow_parser.c
 *
 * @note The parsers do not process the "desired" section of the get accepted
 * data.  It is processed when the delta topic is received.
 *
 * Copyright (c) 2020-2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(mg100_shadow_parser, CONFIG_SHADOW_PARSER_LOG_LEVEL);

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
#include "lairdconnect_battery.h"
#include "lcz_motion.h"
#include "sdcard_log.h"
#include "shadow_parser.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
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
BUILD_ASSERT((JSON_DEFAULT_BUF_SIZE * 2) + 256 < CONFIG_BUFFER_POOL_SIZE,
	     "Buffer pool too small: Need space for 2 messages"
	     "(and system messages)");

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct shadow_parser_agent mg100_agent;

static uint16_t local_updates = 0;

/* NOTE: Order matters. The order of the string array must match
 * the corresponding handler function array below it. Names are
 * based on the MG100 schema.
 */
static const char *WriteableLocalObject[MAX_WRITEABLE_LOCAL_OBJECTS] = {
	BATTERY_LOW_STRING, BATTERY_0_STRING,	 BATTERY_1_STRING,
	BATTERY_2_STRING,   BATTERY_3_STRING,	 BATTERY_4_STRING,
	BATTERY_BAD_STRING, ODR_STRING,		 SCALE_STRING,
	ACT_THRESH_STRING,  MAX_LOG_SIZE_STRING,
};

static const uint16_t LocalConfigUpdateBits[MAX_WRITEABLE_LOCAL_OBJECTS] = {
	LOCAL_UPDATE_BIT_BATTERY_BAD,  LOCAL_UPDATE_BIT_BATTERY_0,
	LOCAL_UPDATE_BIT_BATTERY_1,    LOCAL_UPDATE_BIT_BATTERY_2,
	LOCAL_UPDATE_BIT_BATTERY_3,    LOCAL_UPDATE_BIT_BATTERY_4,
	LOCAL_UPDATE_BIT_BATTERY_BAD,  LOCAL_UPDATE_BIT_MOTION_ODR,
	LOCAL_UPDATE_BIT_MOTION_SCALE, LOCAL_UPDATE_BIT_MOTION_THR,
	LOCAL_UPDATE_BIT_MAX_LOG_SIZE,
};

#ifndef CONFIG_SD_CARD_LOG
static int no_sd_card_update_max_size(int Value)
{
	return -1;
}

static int no_sd_card_get_max_size(void)
{
	return -1;
}
#endif

static int (*LocalConfigUpdate[MAX_WRITEABLE_LOCAL_OBJECTS])(int) = {
	UpdateBatteryLowThreshold,	 UpdateBatteryThreshold0,
	UpdateBatteryThreshold1,	 UpdateBatteryThreshold2,
	UpdateBatteryThreshold3,	 UpdateBatteryThreshold4,
	UpdateBatteryBadThreshold,	 lcz_motion_set_and_update_odr,
	lcz_motion_set_and_update_scale, lcz_motion_set_and_update_threshold,
#ifdef CONFIG_SD_CARD_LOG
	sdCardLogUpdateMaxSize,
#else
	no_sd_card_update_max_size,
#endif
};

static int (*LocalConfigGet[MAX_WRITEABLE_LOCAL_OBJECTS])() = {
	GetBatteryLowThreshold,	  GetBatteryThreshold0, GetBatteryThreshold1,
	GetBatteryThreshold2,	  GetBatteryThreshold3, GetBatteryThreshold4,
	GetBatteryBadThreshold,	  lcz_motion_get_odr,	lcz_motion_get_scale,
	lcz_motion_get_threshold,
#ifdef CONFIG_SD_CARD_LOG
	sdCardLogGetMaxSize,
#else
	no_sd_card_get_max_size,
#endif

};

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void mg100_parser(const char *topic, struct topic_flags flags);

static void MiniGatewayParser(const char *topic, struct topic_flags flags);
static bool ValuesUpdated(uint16_t Value);
static void BuildAndSendLocalConfigResponse(void);
static void BuildAndSendLocalConfigNullResponse(void);

/******************************************************************************/
/* Init                                                                       */
/******************************************************************************/
static int mg100_shadow_parser_init(const struct device *device)
{
	ARG_UNUSED(device);

	mg100_agent.parser = mg100_parser;
	shadow_parser_register_agent(&mg100_agent);

	return 0;
}

SYS_INIT(mg100_shadow_parser_init, APPLICATION, 99);

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void mg100_parser(const char *topic, struct topic_flags flags)
{
	if (flags.gateway) {
		MiniGatewayParser(topic, tf);
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void BuildAndSendLocalConfigNullResponse(void)
{
	size_t size = JSON_DEFAULT_BUF_SIZE;
	JsonMsg_t *pMsg = BP_TRY_TO_TAKE(FWK_BUFFER_MSG_SIZE(JsonMsg_t, size));

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
		 aws_get_gateway_update_delta_topic());

	FRAMEWORK_MSG_SEND(pMsg);
}

static void BuildAndSendLocalConfigResponse(void)
{
	int index = 0;
	size_t size = JSON_DEFAULT_BUF_SIZE;
	JsonMsg_t *pMsg = BP_TRY_TO_TAKE(FWK_BUFFER_MSG_SIZE(JsonMsg_t, size));

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
		 aws_get_gateway_update_delta_topic());

	FRAMEWORK_MSG_SEND(pMsg);
}

static bool ValuesUpdated(uint16_t Value)
{
	return ((Value & local_updates) == Value);
}

static void MiniGatewayParser(const char *topic, struct topic_flags flags)
{
	ARG_UNUSED(topic);
	int objectData = 0;
	int objectIndex = 0;
	bool valueUpdated = false;
	bool configRequestHandled = false;
	uint32_t version = 0;

	/* Now try to find the desired local state for the gateway */
	if (flags.get_accepted || (shadow_parser_find_state() <= 0) ||
	    !shadow_parser_find_uint(&version, "version")) {
		return;
	}

	/* Now search for anything under the root of the JSON string. The root will
	 * contain the data for any local configuration items. Names are based on
	 * the MG100 schema (names are unique; heirarchy can be ignored).
	 */
	local_updates = 0;
	do {
		if (shadow_parser_find_uint(
			    &objectData, WriteableLocalObject[objectIndex])) {
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