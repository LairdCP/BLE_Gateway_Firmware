/**
 * @file sensor_task.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(sensor_task);
#define FWK_FNAME "sensor_task"

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <string.h>

#include "Framework.h"

#include "mg100_common.h"
#include "mg100_ble.h"
#include "sensor_bt510.h"
#include "sensor_gateway_parser.h"
#include "sensor_task.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifndef SENSOR_TASK_PRIORITY
#define SENSOR_TASK_PRIORITY K_PRIO_PREEMPT(1)
#endif

#ifndef SENSOR_TASK_STACK_DEPTH
#define SENSOR_TASK_STACK_DEPTH 1024
#endif

#ifndef SENSOR_TASK_QUEUE_DEPTH
#define SENSOR_TASK_QUEUE_DEPTH 10
#endif

#ifndef SCAN_FOR_BL654_SENSOR
#define SCAN_FOR_BL654_SENSOR 1
#endif

#ifndef SCAN_FOR_BT510
#define SCAN_FOR_BT510 1
#endif

typedef struct SensorTaskTag {
	FwkMsgTask_t msgTask;

} SensorTaskObj_t;

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static SensorTaskObj_t sensorTaskObject;

K_THREAD_STACK_DEFINE(sensorTaskStack, SENSOR_TASK_STACK_DEPTH);

K_MSGQ_DEFINE(sensorTaskQueue, FWK_QUEUE_ENTRY_SIZE, SENSOR_TASK_QUEUE_DEPTH,
	      FWK_QUEUE_ALIGNMENT);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void SensorTaskThread(void *pArg1, void *pArg2, void *pArg3);

static DispatchResult_t AdvertisementMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg);

static DispatchResult_t GatewayInMsgHandler(FwkMsgReceiver_t *pMsgRxer,
					    FwkMsg_t *pMsg);

static DispatchResult_t GatewayShadowRequestHandler(FwkMsgReceiver_t *pMsgRxer,
						    FwkMsg_t *pMsg);

static DispatchResult_t WhitelistRequestHandler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg);

/******************************************************************************/
/* Framework Message Dispatcher                                               */
/******************************************************************************/
static FwkMsgHandler_t SensorTaskMsgDispatcher(FwkMsgCode_t MsgCode)
{
	/* clang-format off */
	switch (MsgCode) {
	case FMC_INVALID:              return Framework_UnknownMsgHandler;
	case FMC_ADV:                  return AdvertisementMsgHandler;
	case FMC_BT510_GATEWAY_IN:     return GatewayInMsgHandler;
	case FMC_BT510_SHADOW_REQUEST: return GatewayShadowRequestHandler;
	case FMC_WHITELIST_REQUEST:    return WhitelistRequestHandler;
	default:                       return NULL;
	}
	/* clang-format on */
}

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void SensorTask_Initialize(void)
{
	memset(&sensorTaskObject, 0, sizeof(SensorTaskObj_t));

	sensorTaskObject.msgTask.rxer.id = FWK_ID_SENSOR_TASK;
	sensorTaskObject.msgTask.rxer.pQueue = &sensorTaskQueue;
	sensorTaskObject.msgTask.rxer.rxBlockTicks = K_FOREVER;
	sensorTaskObject.msgTask.rxer.pMsgDispatcher = SensorTaskMsgDispatcher;
	sensorTaskObject.msgTask.timerDurationTicks = 0;
	sensorTaskObject.msgTask.timerPeriodTicks = 0;

	Framework_RegisterTask(&sensorTaskObject.msgTask);

	sensorTaskObject.msgTask.pTid =
		k_thread_create(&sensorTaskObject.msgTask.threadData,
				sensorTaskStack,
				K_THREAD_STACK_SIZEOF(sensorTaskStack),
				SensorTaskThread, &sensorTaskObject, NULL, NULL,
				SENSOR_TASK_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(sensorTaskObject.msgTask.pTid, FWK_FNAME);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void SensorTaskThread(void *pArg1, void *pArg2, void *pArg3)
{
	SensorTaskObj_t *pObj = (SensorTaskObj_t *)pArg1;

	SensorBt510_Initialize();

	while (true) {
		Framework_MsgReceiver(&pObj->msgTask.rxer);
	}
}

DispatchResult_t AdvertisementMsgHandler(FwkMsgReceiver_t *pMsgRxer,
					 FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsgRxer);
	AdvMsg_t *pAdvMsg = (AdvMsg_t *)pMsg;
	if (SCAN_FOR_BL654_SENSOR) {
		bl654_sensor_adv_handler(&pAdvMsg->addr, pAdvMsg->rssi,
					 pAdvMsg->type, &pAdvMsg->ad);
	}
	if (SCAN_FOR_BT510) {
		SensorBt510_AdvertisementHandler(&pAdvMsg->addr, pAdvMsg->rssi,
						 pAdvMsg->type, &pAdvMsg->ad);
	}
	return DISPATCH_OK;
}

static DispatchResult_t GatewayInMsgHandler(FwkMsgReceiver_t *pMsgRxer,
					    FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsgRxer);
	JsonGatewayInMsg_t *pGatewayMsg = (JsonGatewayInMsg_t *)pMsg;
	SensorGatewayParser_Run(pGatewayMsg->buffer);
	return DISPATCH_OK;
}

static DispatchResult_t WhitelistRequestHandler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsgRxer);
	SensorBt510_ProcessWhitelistRequest((SensorWhitelistMsg_t *)pMsg);
	return DISPATCH_OK;
}

static DispatchResult_t GatewayShadowRequestHandler(FwkMsgReceiver_t *pMsgRxer,
						    FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsgRxer);
	UNUSED_PARAMETER(pMsg);
	SensorBt510_GenerateGatewayShadow();
	return DISPATCH_OK;
}
