/**
 * @file bluegrass.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(bluegrass);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "aws.h"
#include "sensor_task.h"
#include "sensor_table.h"
#include "bluegrass.h"

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#ifndef CONFIG_USE_SINGLE_AWS_TOPIC
#define CONFIG_USE_SINGLE_AWS_TOPIC 0
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static bool gatewaySubscribed;
static bool subscribedToGetAccepted;
static bool getShadowProcessed;
static struct k_timer gatewayInitTimer;
static FwkQueue_t *pMsgQueue;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void StartGatewayInitTimer(void);
static void GatewayInitTimerCallbackIsr(struct k_timer *timer_id);
static int GatewaySubscriptionHandler(void);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void Bluegrass_Initialize(FwkQueue_t *pQ)
{
	pMsgQueue = pQ;
	SensorTask_Initialize();
	k_timer_init(&gatewayInitTimer, GatewayInitTimerCallbackIsr, NULL);
}

/* This caller of this function will throw away sensor data
if it cant' be sent */
int Bluegrass_MsgHandler(FwkMsg_t *pMsg, bool *pFreeMsg)
{
	int rc = -EINVAL;

	switch (pMsg->header.msgCode) {
	case FMC_SENSOR_PUBLISH: {
		JsonMsg_t *pJsonMsg = (JsonMsg_t *)pMsg;
		rc = awsSendData(pJsonMsg->buffer, CONFIG_USE_SINGLE_AWS_TOPIC ?
							   GATEWAY_TOPIC :
							   pJsonMsg->topic);
	} break;

	case FMC_GATEWAY_OUT: {
		JsonMsg_t *pJsonMsg = (JsonMsg_t *)pMsg;
		rc = awsSendData(pJsonMsg->buffer, GATEWAY_TOPIC);
	} break;

	case FMC_SUBSCRIBE: {
		SubscribeMsg_t *pSubMsg = (SubscribeMsg_t *)pMsg;
		rc = awsSubscribe(pSubMsg->topic, pSubMsg->subscribe);
		pSubMsg->success = (rc == 0);
		FRAMEWORK_MSG_REPLY(pSubMsg, FMC_SUBSCRIBE_ACK);
		*pFreeMsg = false;
	} break;

	case FMC_AWS_GET_ACCEPTED_RECEIVED: {
		rc = awsGetAcceptedUnsub();
		if (rc == 0) {
			getShadowProcessed = true;
		}
	} break;

	case FMC_GATEWAY_INIT: {
		rc = GatewaySubscriptionHandler();
	} break;

	default:
		break;
	}

	return rc;
}

void Bluegrass_ConnectedCallback(void)
{
	StartGatewayInitTimer();
	FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_RESERVED, FMC_AWS_CONNECTED);
}

void Bluegrass_DisconnectedCallback(void)
{
	gatewaySubscribed = false;
	FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_RESERVED,
					   FMC_AWS_DISCONNECTED);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int GatewaySubscriptionHandler(void)
{
	int rc = 0;

	if (CONFIG_USE_SINGLE_AWS_TOPIC) {
		return rc;
	}

	if (!subscribedToGetAccepted) {
		rc = awsGetAcceptedSubscribe();
		if (rc == 0) {
			subscribedToGetAccepted = true;
		}
	}

	if (!getShadowProcessed) {
		rc = awsGetShadow();
	}

	if (getShadowProcessed) {
		rc = awsSubscribe(GATEWAY_TOPIC, true);
		if (rc == 0) {
			gatewaySubscribed = true;
			SensorTable_EnableGatewayShadowGeneration();
		}
	}

	if (!subscribedToGetAccepted || !getShadowProcessed ||
	    !gatewaySubscribed) {
		StartGatewayInitTimer();
	}

	return rc;
}

static void StartGatewayInitTimer(void)
{
	k_timer_start(&gatewayInitTimer, K_SECONDS(1), K_NO_WAIT);
}

/******************************************************************************/
/* Interrupt Service Routines                                                 */
/******************************************************************************/
static void GatewayInitTimerCallbackIsr(struct k_timer *timer_id)
{
	UNUSED_PARAMETER(timer_id);
	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_CLOUD, FWK_ID_CLOUD, FMC_GATEWAY_INIT);
}