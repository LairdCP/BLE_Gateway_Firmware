/**
 * @file bluegrass.c
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(bluegrass, CONFIG_BLUEGRASS_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>

#include "aws.h"
#include "lte.h"
#include "lcz_memfault.h"
#include "led_configuration.h"
#include "attr.h"

#ifdef CONFIG_SENSOR_TASK
#include "sensor_task.h"
#include "sensor_table.h"
#endif

#ifdef CONFIG_CONTACT_TRACING
#include "ct_ble.h"
#endif

#include "bluegrass.h"

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#ifndef CONFIG_USE_SINGLE_AWS_TOPIC
#define CONFIG_USE_SINGLE_AWS_TOPIC 0
#endif

#define CONNECT_TO_SUBSCRIBE_DELAY 4

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct {
	bool init_shadow;
	bool gateway_subscribed;
	bool subscribed_to_get_accepted;
	bool get_shadow_processed;
	struct k_work_delayable heartbeat;
	uint32_t subscription_delay;
} bg;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void heartbeat_work_handler(struct k_work *work);
static void aws_init_shadow(void);

static FwkMsgHandler_t sensor_publish_msg_handler;
static FwkMsgHandler_t gateway_publish_msg_handler;
static FwkMsgHandler_t subscription_msg_handler;
static FwkMsgHandler_t get_accepted_msg_handler;
static FwkMsgHandler_t ess_sensor_msg_handler;
static FwkMsgHandler_t heartbeat_msg_handler;

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void bluegrass_initialize(void)
{
	k_work_init_delayable(&bg.heartbeat, heartbeat_work_handler);

#ifdef CONFIG_SENSOR_TASK
	SensorTask_Initialize();
#endif
}

void bluegrass_init_shadow_request(void)
{
	bg.init_shadow = true;
}

void bluegrass_connected_callback(void)
{
	lcz_led_turn_on(CLOUD_LED);

	k_work_schedule(&bg.heartbeat, K_NO_WAIT);

	FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_RESERVED, FMC_AWS_CONNECTED);

	aws_init_shadow();

	LCZ_MEMFAULT_BUILD_TOPIC(CONFIG_LCZ_MEMFAULT_MQTT_TOPIC, CONFIG_BOARD,
				 attr_get_quasi_static(ATTR_ID_gatewayId),
				 CONFIG_MEMFAULT_NCS_PROJECT_KEY);

#ifdef CONFIG_CONTACT_TRACING
	ct_ble_publish_dummy_data_to_aws();
	/* Try to send stashed entries immediately on re-connect */
	ct_ble_check_stashed_log_entries();
#endif
}

void bluegrass_disconnected_callback(void)
{
	lcz_led_turn_off(CLOUD_LED);

	bg.gateway_subscribed = false;
	bg.subscribed_to_get_accepted = false;
	bg.get_shadow_processed = false;

	FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_RESERVED,
					   FMC_AWS_DISCONNECTED);
}

bool bluegrass_ready_for_publish(void)
{
	return (awsConnected() && bg.get_shadow_processed &&
		bg.gateway_subscribed);
}

DispatchResult_t bluegrass_msg_handler(FwkMsgReceiver_t *pMsgRxer,
				       FwkMsg_t *pMsg)
{
	if (!awsConnected()) {
		return DISPATCH_OK;
	}

	/* clang-format off */
	switch (pMsg->header.msgCode)
	{
	case FMC_SENSOR_PUBLISH:            return sensor_publish_msg_handler(pMsgRxer, pMsg);
	case FMC_GATEWAY_OUT:               return gateway_publish_msg_handler(pMsgRxer, pMsg);
	case FMC_SUBSCRIBE:                 return subscription_msg_handler(pMsgRxer, pMsg);
	case FMC_AWS_GET_ACCEPTED_RECEIVED: return get_accepted_msg_handler(pMsgRxer, pMsg);
	case FMC_ESS_SENSOR_EVENT:          return ess_sensor_msg_handler(pMsgRxer, pMsg);
	case FMC_AWS_HEARTBEAT:             return heartbeat_msg_handler(pMsgRxer, pMsg);
	default:                            return DISPATCH_OK;
	}
	/* clang-format on */
}

int bluegrass_subscription_handler(void)
{
	int rc = 0;

	if (!awsConnected()) {
		bg.subscription_delay = CONNECT_TO_SUBSCRIBE_DELAY;
		return rc;
	}

	if (CONFIG_USE_SINGLE_AWS_TOPIC) {
		return rc;
	}

	if (bg.subscription_delay > 0) {
		bg.subscription_delay -= 1;
		return rc;
	}

	if (!bg.subscribed_to_get_accepted) {
		rc = awsGetAcceptedSubscribe();
		if (rc == 0) {
			bg.subscribed_to_get_accepted = true;
		}
	}

	if (!bg.get_shadow_processed) {
		rc = awsGetShadow();
	}

	if (bg.get_shadow_processed && !bg.gateway_subscribed) {
		rc = awsSubscribe(GATEWAY_TOPIC, true);
		if (rc == 0) {
			bg.gateway_subscribed = true;

			FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_CLOUD,
							   FMC_BLUEGRASS_READY);
		}
	}

	return rc;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void heartbeat_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LCZ_MEMFAULT_PUBLISH_DATA(awsGetMqttClient());

	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_CLOUD, FWK_ID_CLOUD,
				      FMC_AWS_HEARTBEAT);
}

static void aws_init_shadow(void)
{
	int r;

	/* The shadow init is only sent once after the very first connect. */
	if (bg.init_shadow) {
		awsGenerateGatewayTopics(
			attr_get_quasi_static(ATTR_ID_gatewayId));

		r = awsPublishShadowPersistentData();

		if (r != 0) {
			LOG_ERR("Could not publish shadow (%d)", r);
		} else {
			bg.init_shadow = false;
		}
	}
}

static DispatchResult_t sensor_publish_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	JsonMsg_t *pJsonMsg = (JsonMsg_t *)pMsg;

	if (bluegrass_ready_for_publish()) {
		awsSendData(pJsonMsg->buffer, CONFIG_USE_SINGLE_AWS_TOPIC ?
							    GATEWAY_TOPIC :
							    pJsonMsg->topic);
	}

	return DISPATCH_OK;
}

static DispatchResult_t gateway_publish_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						    FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	JsonMsg_t *pJsonMsg = (JsonMsg_t *)pMsg;

	awsSendData(pJsonMsg->buffer, GATEWAY_TOPIC);

	return DISPATCH_OK;
}

static DispatchResult_t subscription_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg)
{
#ifdef CONFIG_SENSOR_TASK
	ARG_UNUSED(pMsgRxer);
	SubscribeMsg_t *pSubMsg = (SubscribeMsg_t *)pMsg;
	int r;

	r = awsSubscribe(pSubMsg->topic, pSubMsg->subscribe);
	pSubMsg->success = (r == 0);

	FRAMEWORK_MSG_REPLY(pSubMsg, FMC_SUBSCRIBE_ACK);

	return DISPATCH_DO_NOT_FREE;
#else
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);
	return DISPATCH_OK;
#endif
}

static DispatchResult_t get_accepted_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);
	int r;

	r = awsGetAcceptedUnsub();
	if (r == 0) {
		bg.get_shadow_processed = true;
	}
	return DISPATCH_OK;
}

static DispatchResult_t ess_sensor_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					       FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ESSSensorMsg_t *pBmeMsg = (ESSSensorMsg_t *)pMsg;

	awsPublishESSSensorData(pBmeMsg->temperatureC, pBmeMsg->humidityPercent,
				pBmeMsg->pressurePa);

	return DISPATCH_OK;
}

static DispatchResult_t heartbeat_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					      FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);

	awsPublishHeartbeat();

#if CONFIG_AWS_HEARTBEAT_SECONDS != 0
	k_work_schedule(&bg.heartbeat, K_SECONDS(CONFIG_AWS_HEARTBEAT_SECONDS));
#endif

	return DISPATCH_OK;
}
