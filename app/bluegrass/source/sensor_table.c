/**
 * @file sensor_table.c
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(sensor_table, CONFIG_SENSOR_TABLE_LOG_LEVEL);
#define FWK_FNAME "sensor_table"

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <zephyr.h>
#include <bluetooth/bluetooth.h>

#include "lcz_bluetooth.h"
#include "lcz_qrtc.h"
#include "ad_find.h"
#include "shadow_builder.h"
#include "sensor_cmd.h"
#include "lcz_sensor_adv_format.h"
#include "lcz_sensor_event.h"
#include "sensor_log.h"
#include "bt510_flags.h"
#include "sensor_table.h"
#include "attr.h"

#ifdef CONFIG_BOARD_MG100
#include "sdcard_log.h"
#endif

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
BUILD_ASSERT(CONFIG_SENSOR_GREENLIST_SIZE < CONFIG_SENSOR_TABLE_SIZE,
	     "Invalid greenlist size");

#define VERBOSE_AD_LOG(...)

#define LOG_EVT LOG_INF

#define SENSOR_UPDATE_TOPIC_FMT_STR CONFIG_SENSOR_TOPIC_FMT_STR_PREFIX "/update"

#define SENSOR_SUBSCRIPTION_TOPIC_FMT_STR                                      \
	CONFIG_SENSOR_TOPIC_FMT_STR_PREFIX "/update/delta"

#define SENSOR_GET_TOPIC_FMT_STR CONFIG_SENSOR_TOPIC_FMT_STR_PREFIX "/get"

#define SENSOR_GET_ACCEPTED_SUB_STR "/get/accepted"

#define SENSOR_GET_ACCEPTED_TOPIC_FMT_STR                                      \
	CONFIG_SENSOR_TOPIC_FMT_STR_PREFIX SENSOR_GET_ACCEPTED_SUB_STR

#ifndef CONFIG_USE_SINGLE_AWS_TOPIC
#define CONFIG_USE_SINGLE_AWS_TOPIC 0
#endif

#ifndef CONFIG_SENSOR_QUERY_CMD_MAX_SIZE
#define CONFIG_SENSOR_QUERY_CMD_MAX_SIZE 1024
#endif

#ifndef CONFIG_SENSOR_TTL_SECONDS
#define CONFIG_SENSOR_TTL_SECONDS (60 * 2)
#endif

#define JSON_DEFAULT_BUF_SIZE (1536)

/* An empty message can be sent, but a value is sent for test purposes.  */
#define GET_ACCEPTED_MSG "{\"message\":\"hi\"}"

#define SHADOW_BUF_SIZE                                                        \
	(JSON_DEFAULT_BUF_SIZE +                                               \
	 (CONFIG_SENSOR_LOG_MAX_SIZE * SENSOR_LOG_ENTRY_JSON_STR_SIZE))
CHECK_BUFFER_SIZE(FWK_BUFFER_MSG_SIZE(JsonMsg_t, SHADOW_BUF_SIZE));

BUILD_ASSERT(((sizeof(SENSOR_SUBSCRIPTION_TOPIC_FMT_STR) +
	       SENSOR_ADDR_STR_LEN) < CONFIG_AWS_TOPIC_MAX_SIZE),
	     "Topic too small");

#define MAX_KEY_STR_LEN 64
#define MANGLED_NAME_MAX_STR_LEN                                               \
	(SENSOR_NAME_MAX_SIZE + sizeof('-') + MAX_KEY_STR_LEN)
#define MANGLED_NAME_MAX_SIZE (MANGLED_NAME_MAX_STR_LEN + 1)

/* {"reported":{"bt510":{"sensors":[["c13a7e4118a2",<epoch>,false], .... */
#define SENSOR_GATEWAY_SHADOW_MAX_SIZE 600
CHECK_BUFFER_SIZE(FWK_BUFFER_MSG_SIZE(JsonMsg_t,
				      SENSOR_GATEWAY_SHADOW_MAX_SIZE));

typedef struct SensorEntry {
	bool inUse;
	bool validAd;
	bool validRsp;
	bool updatedName;
	bool updatedRsp;
	char name[SENSOR_NAME_MAX_SIZE];
	char addrString[SENSOR_ADDR_STR_SIZE];
	LczSensorAdEvent_t ad;
	LczSensorRsp_t rsp;
	int8_t rssi;
	uint8_t lastRecordType;
	uint32_t rxEpoch;
	bool greenlisted;
	bool subscribed;
	bool getAcceptedSubscribed;
	bool shadowInitReceived;
	uint64_t subscriptionDispatchTime;
	uint32_t ttl;
	void *pCmd;
	void *pSecondCmd;
	uint64_t configDispatchTime;
	bool configRequest;
	bool configBusy;
	uint32_t configBusyVersion;
	bool dumpBusy;
	bool firstDumpComplete;
	uint32_t adCount;
	uint16_t lastFlags;
	SensorLog_t *pLog;
} SensorEntry_t;

#define RSSI_UNKNOWN -127

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static size_t tableCount;
static SensorEntry_t sensorTable[CONFIG_SENSOR_TABLE_SIZE];
static char queryCmd[CONFIG_SENSOR_QUERY_CMD_MAX_SIZE];
static uint64_t ttlUptime;
static bool allowGatewayShadowGeneration;
static size_t greenCount;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void ClearTable(void);
static void ClearEntry(SensorEntry_t *pEntry);
static void FreeCmdBuffers(SensorEntry_t *pEntry);
static void FreeEntryBuffers(SensorEntry_t *pEntry);

static bool FindBt510Advertisement(AdHandle_t *pHandle);
static bool FindBt510ScanResponse(AdHandle_t *pHandle);
static bool FindBt510CodedAdvertisement(AdHandle_t *pHandle);

static size_t AddByScanResponse(const bt_addr_le_t *pAddr,
				AdHandle_t *pNameHandle, LczSensorRsp_t *pRsp,
				int8_t Rssi);
static size_t AddByAddress(const bt_addr_t *pAddr);
static void AddEntry(SensorEntry_t *pEntry, const bt_addr_t *pAddr,
		     int8_t Rssi);
static size_t FindTableIndex(const bt_addr_le_t *pAddr);
static size_t FindFirstFree(void);
static void AdEventHandler(LczSensorAdEvent_t *p, int8_t Rssi, uint32_t Index);

static bool AddrMatch(const void *p, size_t Index);
static bool AddrStringMatch(const char *str, size_t Index);
static bool NameMatch(const char *p, size_t Index);
static bool RspMatch(const LczSensorRsp_t *p, size_t Index);
static bool NewEvent(uint16_t Id, size_t Index);

static void SensorAddrToString(SensorEntry_t *pEntry);
static bt_addr_t BtAddrStringToStruct(const char *pAddrString);

static void ShadowMaker(SensorEntry_t *pEntry);
static void ShadowTemperatureHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry);
static void ShadowEventHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry);
static void ShadowIg60EventHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry);
static void ShadowBtHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry);
static void ShadowAdHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry);
static void ShadowRspHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry);
static void ShadowFlagHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry);
static void ShadowLogHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry);
static void ShadowSpecialHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry);
static void GatewayShadowMaker(bool GreenlistProcessed);

static char *MangleKey(const char *pKey, const char *pName);
static size_t GreenlistByAddress(const char *pAddrString, bool NextState);
static void Greenlist(SensorEntry_t *pEntry, bool Enable);

static int32_t GetTemperature(SensorEntry_t *pEntry);
static uint32_t GetBattery(SensorEntry_t *pEntry);
static bool LowBatteryAlarm(SensorEntry_t *pEntry);

static void ConnectRequestHandler(size_t Index, bool Coded);
static void CreateDumpRequest(SensorEntry_t *pEntry);
static void CreateConfigRequest(SensorEntry_t *pEntry);

static uint32_t GetFlag(uint16_t Value, uint32_t Mask, uint8_t Position);

static void PublishToGetAccepted(SensorEntry_t *pEntry);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void SensorTable_Initialize(void)
{
	ClearTable();
	strncpy(queryCmd, SENSOR_CMD_DEFAULT_QUERY,
		CONFIG_SENSOR_QUERY_CMD_MAX_SIZE - 1);
}

bool SensorTable_MatchBt510(struct net_buf_simple *ad)
{
	AdHandle_t manHandle = AdFind_Type(
		ad->data, ad->len, BT_DATA_MANUFACTURER_DATA, BT_DATA_INVALID);
	if (manHandle.pPayload == NULL) {
		return false;
	}

	if (FindBt510ScanResponse(&manHandle)) {
		return true;
	}

	if (FindBt510Advertisement(&manHandle)) {
		return true;
	}

	return FindBt510CodedAdvertisement(&manHandle);
}

/* If a new event has occurred then generate a message to send sensor event
 * data to AWS.
 */
void SensorTable_AdvertisementHandler(const bt_addr_le_t *pAddr, int8_t rssi,
				      uint8_t type, Ad_t *pAd)

{
	ARG_UNUSED(type);
	bool coded = false;

	/* Filter on presence of manufacturer specific data */
	AdHandle_t manHandle =
		AdFind_Type(pAd->data, pAd->len, BT_DATA_MANUFACTURER_DATA,
			    BT_DATA_INVALID);
	if (manHandle.pPayload == NULL) {
		return;
	}

	AdHandle_t nameHandle = AdFind_Name(pAd->data, pAd->len);
	size_t tableIndex = CONFIG_SENSOR_TABLE_SIZE;
	/* Take name from scan response and use it to populate table.
	 * If device is already in table, then check if any fields need to be updated.
	 */
	if (FindBt510ScanResponse(&manHandle)) {
		if (nameHandle.pPayload != NULL) {
			LczSensorRspWithHeader_t *pRspPacket =
				(LczSensorRspWithHeader_t *)manHandle.pPayload;
			tableIndex = AddByScanResponse(pAddr, &nameHandle,
						       &pRspPacket->rsp, rssi);
		}
		/* If scan response data was received then there won't be event data,
		 * but a connect request may still need to be issued
		 */
	}

	if (FindBt510Advertisement(&manHandle)) {
		size_t tableIndex = FindTableIndex(pAddr);
		if (tableIndex < CONFIG_SENSOR_TABLE_SIZE) {
			FRAMEWORK_DEBUG_ASSERT(
				memcmp(sensorTable[tableIndex].ad.addr.val,
				       pAddr->a.val, sizeof(bt_addr_t)) == 0);
		} else {
			/* Try to populate table with sensor (without name and scan rsp) */
			tableIndex = AddByAddress(&pAddr->a);
		}

		if (tableIndex < CONFIG_SENSOR_TABLE_SIZE) {
			LczSensorAdEvent_t *pAd =
				(LczSensorAdEvent_t *)manHandle.pPayload;
			AdEventHandler(pAd, rssi, tableIndex);
		}
	}

	/* The coded PHY ad (superset) is processed using the 1M PHY pieces. */
	if (FindBt510CodedAdvertisement(&manHandle)) {
		coded = true;
		LczSensorAdCoded_t *pCoded =
			(LczSensorAdCoded_t *)manHandle.pPayload;
		if (nameHandle.pPayload != NULL) {
			tableIndex = AddByScanResponse(pAddr, &nameHandle,
						       &pCoded->rsp, rssi);

			if (tableIndex < CONFIG_SENSOR_TABLE_SIZE) {
				AdEventHandler(&pCoded->ad, rssi, tableIndex);
			}
		}
	}

	if (tableIndex < CONFIG_SENSOR_TABLE_SIZE) {
		ConnectRequestHandler(tableIndex, coded);
		sensorTable[tableIndex].adCount += 1;
		VERBOSE_AD_LOG("'%s' %u",
			       log_strdup(sensorTable[tableIndex].name),
			       sensorTable[tableIndex].adCount);
	}
}

void SensorTable_ProcessGreenlistRequest(SensorGreenlistMsg_t *pMsg)
{
	size_t changed = 0;
	size_t i;
	for (i = 0; i < pMsg->sensorCount; i++) {
		changed += GreenlistByAddress(pMsg->sensors[i].addrString,
					      pMsg->sensors[i].greenlist);
	}
	LOG_DBG("Greenlist setting changed for %u sensors", changed);

	/* Filter out deltas due to timestamp changing. */
	if (changed > 0) {
		GatewayShadowMaker(true);
	}
}

DispatchResult_t SensorTable_AddConfigRequest(SensorCmdMsg_t *pMsg)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		if (AddrStringMatch(pMsg->addrString, i)) {
			break;
		}
	}

	if (i >= CONFIG_SENSOR_TABLE_SIZE) {
		LOG_ERR("Config request sensor not found");
		return DISPATCH_ERROR;
	}

	SensorEntry_t *p = &sensorTable[i];

	if (LowBatteryAlarm(p)) {
		/* A sensor in low battery mode is unable to write flash. */
		LOG_WRN("Unable to accept config for sensor in low battery mode");
		return DISPATCH_OK;
	}

	if (pMsg->dumpRequest) {
		pMsg->resetRequest = false;
	} else if (p->rsp.firmwareVersionMajor >=
		   BT510_MAJOR_VERSION_RESET_NOT_REQUIRED) {
		pMsg->resetRequest = false;
	} else {
		pMsg->resetRequest = SensorCmd_RequiresReset(pMsg->cmd);
	}

	/* The "configVersion" number in the shadow is used to filter out
	 * identical config changes that may be received on the /delta topic.
	 */
	if ((pMsg->configVersion != p->rsp.configVersion) ||
	    pMsg->dumpRequest) {
		/* If AWS sends a second config message while the first
		 * one is being processed, it must be saved so that it
	 	 * isn't lost.  This is one negative of not keeping the
	 	 * entire state of the BT510. The gateway isn't a
		 * passthrough device as previously intended.
		 */
		if (p->configBusy || p->pCmd != NULL) {
			if (!pMsg->dumpRequest &&
			    (p->configBusyVersion != pMsg->configVersion)) {
				LOG_WRN("(Config Busy) Saving config for sensor '%s' Version: %u",
					log_strdup(p->name),
					pMsg->configVersion);
				/* Delete any oustanding command. */
				if (p->pSecondCmd != NULL) {
					BufferPool_Free(p->pSecondCmd);
				}
				p->pSecondCmd = pMsg;
				return DISPATCH_DO_NOT_FREE;
			}
		} else {
			if (pMsg->dumpRequest) {
				LOG_WRN("State request for '%s'",
					log_strdup(p->name));
			} else {
				LOG_WRN("New config for sensor '%s' Version: %u",
					log_strdup(p->name),
					pMsg->configVersion);
			}
			p->pCmd = pMsg;
			return DISPATCH_DO_NOT_FREE;
		}
	}

	/* When editing the shadow manually (for example, in the AWS IoT console),
	 * the "configVersion" number must also be changed.
	 */
	LOG_WRN("Duplicate request from Bluegrass not accepted (version unchanged)");
	return DISPATCH_OK;
}

DispatchResult_t SensorTable_RetryConfigRequest(SensorCmdMsg_t *pMsg)
{
	FRAMEWORK_ASSERT(pMsg != NULL);
	FRAMEWORK_ASSERT(pMsg->tableIndex < CONFIG_SENSOR_TABLE_SIZE);

	if (pMsg->tableIndex < CONFIG_SENSOR_TABLE_SIZE) {
		SensorEntry_t *pEntry = &sensorTable[pMsg->tableIndex];

		if (pEntry->pCmd == NULL) {
			pEntry->configBusy = false;
			pEntry->pCmd = pMsg;
			return DISPATCH_DO_NOT_FREE;
		} else {
			LOG_ERR("Discarding config request: Command not empty");
			return DISPATCH_OK;
		}
	} else {
		LOG_ERR("Discarding config request: Invalid sensor table index");
		return DISPATCH_OK;
	}
}

void SensorTable_AckConfigRequest(SensorCmdMsg_t *pMsg)
{
	FRAMEWORK_ASSERT(pMsg != NULL);
	FRAMEWORK_ASSERT(pMsg->tableIndex < CONFIG_SENSOR_TABLE_SIZE);

	if (pMsg->tableIndex < CONFIG_SENSOR_TABLE_SIZE) {
		SensorEntry_t *pEntry = &sensorTable[pMsg->tableIndex];
		/* After AWS config was written and sensor was reset,
		 * send dump request to read state.
		 */
		pEntry->configBusy = false;
		if (pEntry->pSecondCmd != NULL) {
			pEntry->pCmd = pEntry->pSecondCmd;
			pEntry->pSecondCmd = NULL;
		} else if (pMsg->dumpRequest) {
			pEntry->dumpBusy = false;
			pEntry->firstDumpComplete = true;
		} else {
			CreateDumpRequest(pEntry);
		}
	} else {
		LOG_ERR("Invalid Ack request: Invalid sensor table index");
	}

	BufferPool_Free(pMsg);
}

void SensorTable_EnableGatewayShadowGeneration(void)
{
	allowGatewayShadowGeneration = true;
}

void SensorTable_DisableGatewayShadowGeneration(void)
{
	allowGatewayShadowGeneration = false;
}

void SensorTable_DecomissionHandler(void)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		Greenlist(&sensorTable[i], false);
		sensorTable[i].shadowInitReceived = false;
		sensorTable[i].firstDumpComplete = false;
	}
}

void SensorTable_UnsubscribeAll(void)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		sensorTable[i].subscribed = false;
		sensorTable[i].getAcceptedSubscribed = false;
	}
}

void SensorTable_ConfigRequestHandler(void)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		SensorEntry_t *p = &sensorTable[i];
		if (p->configRequest &&
		    (p->configDispatchTime <= k_uptime_get())) {
			p->configRequest = false;
			if (p->rsp.configVersion == 0) {
				CreateConfigRequest(p);
			} else if (!p->firstDumpComplete) {
				CreateDumpRequest(p);
			}
		}
	}
}

void SensorTable_SubscriptionHandler(void)
{
	char *fmt = SENSOR_SUBSCRIPTION_TOPIC_FMT_STR;
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		SensorEntry_t *pEntry = &sensorTable[i];
		/* Waiting until AD and RSP are valid makes things easier for config.
		 * When subscribing there must be a delay to allow AWS to configure
		 * permissions.
		 */
		if (pEntry->validAd && pEntry->validRsp &&
		    (pEntry->greenlisted != pEntry->subscribed) &&
		    (pEntry->subscriptionDispatchTime <= k_uptime_get())) {
			SubscribeMsg_t *pMsg =
				BP_TRY_TO_TAKE(sizeof(SubscribeMsg_t));
			if (pMsg != NULL) {
				pMsg->header.msgCode = FMC_SUBSCRIBE;
				pMsg->header.rxId = FWK_ID_CLOUD;
				pMsg->header.txId = FWK_ID_SENSOR_TASK;
				pMsg->subscribe = pEntry->greenlisted;
				pMsg->tableIndex = i;
				pMsg->length =
					snprintk(pMsg->topic,
						 CONFIG_AWS_TOPIC_MAX_SIZE, fmt,
						 pEntry->addrString);
				FRAMEWORK_MSG_SEND(pMsg);
				/* For now, assume the subscription will work. */
				pEntry->subscribed = pEntry->greenlisted;
			}
		}
	}
}

void SensorTable_GetAcceptedSubscriptionHandler(void)
{
	char *fmt = SENSOR_GET_ACCEPTED_TOPIC_FMT_STR;
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		SensorEntry_t *pEntry = &sensorTable[i];
		if (pEntry->subscribed && !pEntry->getAcceptedSubscribed &&
		    !pEntry->shadowInitReceived &&
		    (pEntry->subscriptionDispatchTime <= k_uptime_get())) {
			SubscribeMsg_t *pMsg =
				BP_TRY_TO_TAKE(sizeof(SubscribeMsg_t));
			if (pMsg != NULL) {
				pMsg->header.msgCode = FMC_SUBSCRIBE;
				pMsg->header.rxId = FWK_ID_CLOUD;
				pMsg->header.txId = FWK_ID_SENSOR_TASK;
				pMsg->subscribe = true;
				pMsg->tableIndex = i;
				pMsg->length =
					snprintk(pMsg->topic,
						 CONFIG_AWS_TOPIC_MAX_SIZE, fmt,
						 pEntry->addrString);
				FRAMEWORK_MSG_SEND(pMsg);
			}
		}
	}
}

void SensorTable_InitShadowHandler(void)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		SensorEntry_t *pEntry = &sensorTable[i];
		if (pEntry->getAcceptedSubscribed &&
		    !pEntry->shadowInitReceived) {
			PublishToGetAccepted(pEntry);
			/* Limit to one because it is memory intensive. */
			return;
		}
	}
}

void SensorTable_ProcessShadowInitMsg(SensorShadowInitMsg_t *pMsg)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		if (AddrStringMatch(pMsg->addrString, i)) {
			break;
		}
	}

	if (i >= CONFIG_SENSOR_TABLE_SIZE) {
		LOG_ERR("Shadow Init sensor not found");
		return;
	}

	SensorEntry_t *p = &sensorTable[i];
	p->shadowInitReceived = true;

	/* To keep things simple, throw away the table. */
	if (pMsg->eventCount > 0) {
		if (p->pLog != NULL) {
			SensorLog_Free(p->pLog);
		}
		p->pLog = SensorLog_Allocate(CONFIG_SENSOR_LOG_MAX_SIZE);
		for (i = 0; i < pMsg->eventCount; i++) {
			SensorLog_Add(p->pLog, &pMsg->events[i]);
		}
	}
}

void SensorTable_SubscriptionAckHandler(SubscribeMsg_t *pMsg)
{
	if (pMsg->tableIndex < CONFIG_SENSOR_TABLE_SIZE) {
		SensorEntry_t *p = &sensorTable[pMsg->tableIndex];

		if (strstr(pMsg->topic, SENSOR_GET_ACCEPTED_SUB_STR) != NULL) {
			if (pMsg->success) {
				p->getAcceptedSubscribed = true;
			}
		} else {
			/* This is a delta subscription ack */
			if (pMsg->success) {
				if (p->subscribed) {
					p->configRequest = true;
					p->configDispatchTime =
						k_uptime_get() +
						(CONFIG_SENSOR_CONFIG_DELAY_SECONDS *
						 MSEC_PER_SEC);
				}
			} else { /* Try again (most likely AWS disconnect has occurred) */
				p->subscribed = !pMsg->subscribe;
			}
		}
	}
}

void SensorTable_CreateShadowFromDumpResponse(FwkBufMsg_t *pRsp,
					      const char *pAddrStr)
{
	size_t size = JSON_DEFAULT_BUF_SIZE + pRsp->length + 1;
	JsonMsg_t *pMsg = BP_TRY_TO_TAKE(FWK_BUFFER_MSG_SIZE(JsonMsg_t, size));
	if (pMsg == NULL) {
		return;
	}
	pMsg->header.msgCode = FMC_SENSOR_PUBLISH;
	pMsg->header.rxId = FWK_ID_CLOUD;
	pMsg->size = size;

	ShadowBuilder_Start(pMsg, SKIP_MEMSET);
	ShadowBuilder_StartGroup(pMsg, "state");
	/* Clear desired because AWS gets all state information. */
	ShadowBuilder_AddNull(pMsg, "desired");
	/* Add the entire response.  AWS app will ignore jsonrpc, id field,
	 * and status fields.
	 */
	ShadowBuilder_AddString(pMsg, "reported", pRsp->buffer);
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_Finalize(pMsg);

	char *fmt = SENSOR_UPDATE_TOPIC_FMT_STR;
	snprintk(pMsg->topic, CONFIG_AWS_TOPIC_MAX_SIZE, fmt, pAddrStr);
	FRAMEWORK_MSG_SEND(pMsg);
}

void SensorTable_TimeToLiveHandler(void)
{
	int64_t deltaS = k_uptime_delta(&ttlUptime) / (1 * MSEC_PER_SEC);
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		SensorEntry_t *p = &sensorTable[i];
		if (p->inUse) {
			p->ttl = (p->ttl > deltaS) ? (p->ttl - deltaS) : 0;
			if (p->ttl == 0 && !p->greenlisted) {
				LOG_DBG("Removing '%s' sensor %s from table",
					log_strdup(p->name),
					log_strdup(p->addrString));
				ClearEntry(p);
				FRAMEWORK_DEBUG_ASSERT(tableCount > 0);
				tableCount -= 1;
			}
		}
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/

/* The purpose of the table is to keep track of the event id and
 * associate names with addresses.
 */
static void ClearTable(void)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		ClearEntry(&sensorTable[i]);
	}
	tableCount = 0;
}

static void ClearEntry(SensorEntry_t *pEntry)
{
	FreeEntryBuffers(pEntry);
	memset(pEntry, 0, sizeof(SensorEntry_t));
}

static void FreeCmdBuffers(SensorEntry_t *pEntry)
{
	if (pEntry->pCmd != NULL) {
		BufferPool_Free(pEntry->pCmd);
		pEntry->pCmd = NULL;
	}
	if (pEntry->pSecondCmd != NULL) {
		BufferPool_Free(pEntry->pSecondCmd);
		pEntry->pSecondCmd = NULL;
	}
}

static void FreeEntryBuffers(SensorEntry_t *pEntry)
{
	FreeCmdBuffers(pEntry);

	if (pEntry->pLog != NULL) {
		SensorLog_Free(pEntry->pLog);
		pEntry->pLog = NULL;
	}
}

static void AdEventHandler(LczSensorAdEvent_t *p, int8_t Rssi, uint32_t Index)
{
	if (sensorTable[Index].greenlisted) {
		sensorTable[Index].ttl = CONFIG_SENSOR_TTL_SECONDS;
	}

	if (NewEvent(p->id, Index)) {
		sensorTable[Index].validAd = true;
		sensorTable[Index].lastRecordType =
			sensorTable[Index].ad.recordType;
		memcpy(&sensorTable[Index].ad, p, sizeof(LczSensorAdEvent_t));
		sensorTable[Index].rssi = Rssi;
		/* If event occurs before epoch is set, then AWS shows ~1970. */
		sensorTable[Index].rxEpoch = lcz_qrtc_get_epoch();
		ShadowMaker(&sensorTable[Index]);

		LOG_EVT("%s event %u for [%u] '%s' (%s) RSSI: %d",
			lcz_sensor_event_get_string(
				sensorTable[Index].ad.recordType),
			sensorTable[Index].ad.id, Index,
			log_strdup(sensorTable[Index].name),
			log_strdup(sensorTable[Index].addrString), Rssi);

#ifdef CONFIG_SD_CARD_LOG
		sdCardLogAdEvent(p);
#endif
		/* The cloud uses the RX epoch (in the table) for filtering. */
		GatewayShadowMaker(false);
	}
}

/* The BT510 advertisement can be recognized by the manufacturer
 * specific data type with LAIRD as the company ID.
 * It is further qualified by having a length of 27 and matching protocol ID.
 */
static bool FindBt510Advertisement(AdHandle_t *pHandle)
{
	if (pHandle->pPayload != NULL) {
		if ((pHandle->size == LCZ_SENSOR_MSD_AD_PAYLOAD_LENGTH)) {
			if (memcmp(pHandle->pPayload, BTXXX_AD_HEADER,
				   sizeof(BTXXX_AD_HEADER)) == 0) {
				return true;
			}
		}
	}
	return false;
}

static bool FindBt510ScanResponse(AdHandle_t *pHandle)
{
	if (pHandle->pPayload != NULL) {
		if ((pHandle->size == LCZ_SENSOR_MSD_RSP_PAYLOAD_LENGTH)) {
			if (memcmp(pHandle->pPayload, BTXXX_RSP_HEADER,
				   sizeof(BTXXX_RSP_HEADER)) == 0) {
				return true;
			}
		}
	}
	return false;
}

static bool FindBt510CodedAdvertisement(AdHandle_t *pHandle)
{
	if (pHandle->pPayload != NULL) {
		if ((pHandle->size == LCZ_SENSOR_MSD_CODED_PAYLOAD_LENGTH)) {
			if (memcmp(pHandle->pPayload, BTXXX_CODED_HEADER,
				   sizeof(BTXXX_CODED_HEADER)) == 0) {
				return true;
			}
		}
	}
	return false;
}

static size_t AddByScanResponse(const bt_addr_le_t *pAddr,
				AdHandle_t *pNameHandle, LczSensorRsp_t *pRsp,
				int8_t Rssi)
{
	if (pNameHandle->pPayload == NULL) {
		return CONFIG_SENSOR_TABLE_SIZE;
	}

	if (pRsp == NULL) {
		return CONFIG_SENSOR_TABLE_SIZE;
	}

	/* The first free entry will be used after entire table is searched. */
	bool add = false;
	bool updateRsp = false;
	bool updateName = false;
	SensorEntry_t *pEntry = NULL;
	size_t i = FindTableIndex(pAddr);
	if (i < CONFIG_SENSOR_TABLE_SIZE) {
		pEntry = &sensorTable[i];
		if (!NameMatch(pNameHandle->pPayload, i)) {
			updateName = true;
		}
		if (!RspMatch(pRsp, i)) {
			updateRsp = true;
		}
	} else {
		i = FindFirstFree();
		if (i < CONFIG_SENSOR_TABLE_SIZE) {
			pEntry = &sensorTable[i];
			add = true;
		}
	}

	if ((pEntry != NULL) && (add || updateRsp || updateName)) {
		pEntry->validRsp = true;
		if (add || updateRsp) {
			pEntry->updatedRsp = true;
			memcpy(&pEntry->rsp, pRsp, sizeof(LczSensorRsp_t));
		}
		if (add || updateName) {
			pEntry->updatedName = true;
			memset(pEntry->name, 0, SENSOR_NAME_MAX_SIZE);
			strncpy(pEntry->name, pNameHandle->pPayload,
				MIN(SENSOR_NAME_MAX_STR_LEN,
				    pNameHandle->size));
		}
		if (add) {
			AddEntry(pEntry, &pAddr->a, Rssi);
		}
	}
	return i;
}

static size_t AddByAddress(const bt_addr_t *pAddr)
{
	size_t i = FindFirstFree();
	if (i < CONFIG_SENSOR_TABLE_SIZE) {
		AddEntry(&sensorTable[i], pAddr, RSSI_UNKNOWN);
	}
	return i;
}

static void AddEntry(SensorEntry_t *pEntry, const bt_addr_t *pAddr, int8_t Rssi)
{
	tableCount += 1;
	pEntry->ttl = CONFIG_SENSOR_TTL_SECONDS;
	pEntry->inUse = true;
	pEntry->rssi = Rssi;
	memcpy(pEntry->ad.addr.val, pAddr->val, sizeof(bt_addr_t));
	/* The address is duplicated in the advertisement payload because
	 * some operating systems don't provide the Bluetooth address to
	 * the application.  The address is copied into the AD field
	 * because the two formats are the same.
	 */
	SensorAddrToString(pEntry);
	LOG_DBG("Added BT510 sensor %s '%s' RSSI: %d",
		log_strdup(pEntry->addrString), log_strdup(pEntry->name),
		pEntry->rssi);
	GatewayShadowMaker(false);
}

/* Find index of advertiser's address in the sensor table */
static size_t FindTableIndex(const bt_addr_le_t *pAddr)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		if (sensorTable[i].inUse) {
			if (AddrMatch(pAddr->a.val, i)) {
				return i;
			}
		}
	}
	return CONFIG_SENSOR_TABLE_SIZE;
}

static size_t FindFirstFree(void)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		if (!sensorTable[i].inUse) {
			return i;
		}
	}
	return CONFIG_SENSOR_TABLE_SIZE;
}

static bool AddrMatch(const void *p, size_t Index)
{
	return (memcmp(p, sensorTable[Index].ad.addr.val, sizeof(bt_addr_t)) ==
		0);
}

static bool AddrStringMatch(const char *str, size_t Index)
{
	return (strncmp(str, sensorTable[Index].addrString,
			SENSOR_ADDR_STR_LEN) == 0);
}

static bool NameMatch(const char *p, size_t Index)
{
	return (strncmp(p, sensorTable[Index].name, SENSOR_NAME_MAX_STR_LEN) ==
		0);
}

static bool RspMatch(const LczSensorRsp_t *p, size_t Index)
{
	return (memcmp(p, &sensorTable[Index].rsp, sizeof(LczSensorRsp_t)) ==
		0);
}

static bool NewEvent(uint16_t Id, size_t Index)
{
	if (!sensorTable[Index].validAd) {
		return true;
	} else {
		return (Id != sensorTable[Index].ad.id);
	}
}

static void ShadowMaker(SensorEntry_t *pEntry)
{
	/* AWS will disconnect if data is sent for devices that have not
	 * been greenlisted.
	 */
	if (!CONFIG_USE_SINGLE_AWS_TOPIC) {
		if (!pEntry->greenlisted || !pEntry->shadowInitReceived) {
			return;
		}
	}

	JsonMsg_t *pMsg =
		BP_TRY_TO_TAKE(FWK_BUFFER_MSG_SIZE(JsonMsg_t, SHADOW_BUF_SIZE));
	if (pMsg == NULL) {
		return;
	}

	pMsg->header.msgCode = FMC_SENSOR_PUBLISH;
	pMsg->header.rxId = FWK_ID_CLOUD;
	pMsg->size = SHADOW_BUF_SIZE;

	ShadowBuilder_Start(pMsg, SKIP_MEMSET);
	ShadowBuilder_StartGroup(pMsg, "state");
	ShadowBuilder_StartGroup(pMsg, "reported");
	if (CONFIG_USE_SINGLE_AWS_TOPIC) {
		ShadowTemperatureHandler(pMsg, pEntry);
		/* Sending RSSI prevents an empty buffer when
		 * temperature isn't present.
		 */
		ShadowBuilder_AddSigned32(pMsg, MangleKey(pEntry->name, "rssi"),
					  pEntry->rssi);
	} else {
		ShadowBtHandler(pMsg, pEntry);
		ShadowAdHandler(pMsg, pEntry);
		ShadowRspHandler(pMsg, pEntry);
		ShadowLogHandler(pMsg, pEntry);
		ShadowSpecialHandler(pMsg, pEntry);
	}
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_Finalize(pMsg);

	/* The part of the topic that changes must match
	 * the format of the address field generated by ShadowGatewayMaker.
	 */
	char *fmt = SENSOR_UPDATE_TOPIC_FMT_STR;
	snprintk(pMsg->topic, CONFIG_AWS_TOPIC_MAX_SIZE, fmt,
		 pEntry->addrString);

	FRAMEWORK_MSG_SEND(pMsg);
}

/**
 * @brief Create unique names for each key so that everything can be
 * sent to a single topic.
 */
static char *MangleKey(const char *pName, const char *pKey)
{
#if CONFIG_USE_SINGLE_AWS_TOPIC
	static char mangled[MANGLED_NAME_MAX_SIZE];
	memset(mangled, 0, sizeof(mangled));
	strncat_max(mangled, pName, MANGLED_NAME_MAX_STR_LEN);
	strncat_max(mangled, "-", MANGLED_NAME_MAX_STR_LEN);
	strncat_max(mangled, pKey, MANGLED_NAME_MAX_STR_LEN);
	return mangled;
#else
	return (char *)pKey;
#endif
}

static void ShadowBtHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	ShadowBuilder_AddPair(pMsg, "bluetoothAddress", pEntry->addrString,
			      SB_IS_STRING);

	ShadowBuilder_AddSigned32(pMsg, "rssi", pEntry->rssi);
}

static void ShadowAdHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	/* If gateway was reset then a sensor may be enabled from AWS
	 * before an AD or RSP has been received.  The shadow is already valid.
	 * Don't send bad data.
	 */
	if (!pEntry->validAd) {
		return;
	}

	ShadowBuilder_AddUint32(pMsg, "networkId", pEntry->ad.networkId);
	ShadowBuilder_AddUint32(pMsg, "flags", pEntry->ad.flags);
	ShadowBuilder_AddUint32(pMsg, "resetCount", pEntry->ad.resetCount);

	ShadowTemperatureHandler(pMsg, pEntry);
	ShadowEventHandler(pMsg, pEntry);
	ShadowFlagHandler(pMsg, pEntry);
	ShadowIg60EventHandler(pMsg, pEntry);
}

/**
 * @brief Build JSON for items that are in the Scan Response
 * and don't change that often (when device is added to table).
 */
static void ShadowRspHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	if (!pEntry->validRsp) {
		return;
	}

	if (pEntry->updatedRsp) {
		pEntry->updatedRsp = false;
		ShadowBuilder_AddUint32(pMsg, "productId",
					pEntry->rsp.productId);
		ShadowBuilder_AddVersion(pMsg, "firmwareVersion",
					 pEntry->rsp.firmwareVersionMajor,
					 pEntry->rsp.firmwareVersionMinor,
					 pEntry->rsp.firmwareVersionPatch);
		ShadowBuilder_AddVersion(pMsg, "bootloaderVersion",
					 pEntry->rsp.bootloaderVersionMajor,
					 pEntry->rsp.bootloaderVersionMinor,
					 pEntry->rsp.bootloaderVersionPatch);
		ShadowBuilder_AddUint32(pMsg, "configVersion",
					pEntry->rsp.configVersion);
		ShadowBuilder_AddVersion(pMsg, "hardwareVersion",
					 ADV_FORMAT_HW_VERSION_GET_MAJOR(
						 pEntry->rsp.hardwareVersion),
					 ADV_FORMAT_HW_VERSION_GET_MINOR(
						 pEntry->rsp.hardwareVersion),
					 0);
	}

	if (pEntry->updatedName) {
		pEntry->updatedName = false;
		ShadowBuilder_AddPair(pMsg, "sensorName", pEntry->name,
				      SB_IS_STRING);
	}
}

/* The generic data field is unsigned but the temperature is signed.
 * Get temperature from advertisement (assumes event contains temperature).
 * retval temperature in hundredths of degree C
 */
static int32_t GetTemperature(SensorEntry_t *pEntry)
{
	return (int32_t)((int16_t)pEntry->ad.data.u16);
}

static uint32_t GetBattery(SensorEntry_t *pEntry)
{
	return (uint32_t)((uint16_t)pEntry->ad.data.u16);
}

static bool LowBatteryAlarm(SensorEntry_t *pEntry)
{
	return (GetFlag(pEntry->ad.flags, FLAG_LOW_BATTERY_ALARM) != 0);
}

static void ShadowTemperatureHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	int32_t temperature = GetTemperature(pEntry);
	if (CONFIG_USE_SINGLE_AWS_TOPIC) {
		/* The desired format is degrees when publishing to a single topic
		 * because that is how the BL654 Sensor data is formatted.
		 */
		temperature /= 100;
	}
	switch (pEntry->ad.recordType) {
	case SENSOR_EVENT_TEMPERATURE:
	case SENSOR_EVENT_ALARM_HIGH_TEMP_1:
	case SENSOR_EVENT_ALARM_HIGH_TEMP_2:
	case SENSOR_EVENT_ALARM_HIGH_TEMP_CLEAR:
	case SENSOR_EVENT_ALARM_LOW_TEMP_1:
	case SENSOR_EVENT_ALARM_LOW_TEMP_2:
	case SENSOR_EVENT_ALARM_LOW_TEMP_CLEAR:
	case SENSOR_EVENT_ALARM_DELTA_TEMP:
	case SENSOR_EVENT_ALARM_TEMPERATURE_RATE_OF_CHANGE:
		ShadowBuilder_AddSigned32(
			pMsg,
			MangleKey(pEntry->name, CONFIG_USE_SINGLE_AWS_TOPIC ?
							      "temperature" :
							      "tempCc"),
			temperature);
		break;
	default:
		break;
	}
}

static void ShadowEventHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	/* Many events are replicated in flags (and not processed here). */
	switch (pEntry->ad.recordType) {
	case SENSOR_EVENT_BATTERY_GOOD:
	case SENSOR_EVENT_BATTERY_BAD:
		ShadowBuilder_AddUint32(pMsg, "batteryVoltageMv",
					(uint32_t)pEntry->ad.data.u16);
		break;
	case SENSOR_EVENT_RESET:
		ShadowBuilder_AddPair(pMsg, "resetReason",
				      lcz_sensor_event_get_reset_reason_string(
					      pEntry->ad.data.u16),
				      false);
		break;
	default:
		break;
	}
}

/* These are additional events that the IG60 generates.
 * The purpose of these is have a persitent value in shadow because
 * the event log is limited.  If the app is closed, then this is one
 * method of preventing lost events.
 * The names are taken from the bt510_cli project and bt510.schema.json.
 */
static void ShadowIg60EventHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	int32_t t = GetTemperature(pEntry);
	switch (pEntry->ad.recordType) {
	case SENSOR_EVENT_ALARM_HIGH_TEMP_1:
		ShadowBuilder_AddSigned32(
			pMsg, IG60_GENERATED_EVENT_STR_ALARM_HIGH_TEMP_1, t);
		break;
	case SENSOR_EVENT_ALARM_HIGH_TEMP_2:
		ShadowBuilder_AddSigned32(
			pMsg, IG60_GENERATED_EVENT_STR_ALARM_HIGH_TEMP_2, t);
		break;
	case SENSOR_EVENT_ALARM_HIGH_TEMP_CLEAR:
		ShadowBuilder_AddSigned32(
			pMsg, IG60_GENERATED_EVENT_STR_ALARM_HIGH_TEMP_CLEAR,
			t);
		break;
	case SENSOR_EVENT_ALARM_LOW_TEMP_1:
		ShadowBuilder_AddSigned32(
			pMsg, IG60_GENERATED_EVENT_STR_ALARM_LOW_TEMP_1, t);
		break;
	case SENSOR_EVENT_ALARM_LOW_TEMP_2:
		ShadowBuilder_AddSigned32(
			pMsg, IG60_GENERATED_EVENT_STR_ALARM_LOW_TEMP_2, t);
		break;
	case SENSOR_EVENT_ALARM_LOW_TEMP_CLEAR:
		ShadowBuilder_AddSigned32(
			pMsg, IG60_GENERATED_EVENT_STR_ALARM_LOW_TEMP_CLEAR, t);
		break;
	case SENSOR_EVENT_ALARM_DELTA_TEMP:
		ShadowBuilder_AddSigned32(
			pMsg, IG60_GENERATED_EVENT_STR_ALARM_DELTA_TEMP, t);
		break;
	case SENSOR_EVENT_BATTERY_GOOD:
		ShadowBuilder_AddUint32(pMsg,
					IG60_GENERATED_EVENT_STR_BATTERY_GOOD,
					GetBattery(pEntry));
		break;
	case SENSOR_EVENT_BATTERY_BAD:
		ShadowBuilder_AddUint32(pMsg,
					IG60_GENERATED_EVENT_STR_BATTERY_BAD,
					GetBattery(pEntry));
		break;
	case SENSOR_EVENT_ADV_ON_BUTTON:
		ShadowBuilder_AddUint32(
			pMsg, IG60_GENERATED_EVENT_STR_ADVERTISE_ON_BUTTON,
			GetBattery(pEntry));
		break;
	default:
		break;
	}
}

static void ShadowFlagHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	uint16_t flags = pEntry->ad.flags;
	if (flags != pEntry->lastFlags) {
		ShadowBuilder_AddUint32(pMsg, "rtcSet",
					GetFlag(flags, FLAG_TIME_WAS_SET));
		ShadowBuilder_AddUint32(pMsg, "activeMode",
					GetFlag(flags, FLAG_ACTIVE_MODE));
		ShadowBuilder_AddUint32(pMsg, "anyAlarm",
					GetFlag(flags, FLAG_ANY_ALARM));
		ShadowBuilder_AddUint32(pMsg, "lowBatteryAlarm",
					GetFlag(flags, FLAG_LOW_BATTERY_ALARM));
		ShadowBuilder_AddUint32(pMsg, "highTemperatureAlarm",
					GetFlag(flags, FLAG_HIGH_TEMP_ALARM));
		ShadowBuilder_AddUint32(pMsg, "lowTemperatureAlarm",
					GetFlag(flags, FLAG_LOW_TEMP_ALARM));
		ShadowBuilder_AddUint32(pMsg, "deltaTemperatureAlarm",
					GetFlag(flags, FLAG_DELTA_TEMP_ALARM));
		ShadowBuilder_AddUint32(
			pMsg, "rateOfChangeTemperatureAlarm",
			GetFlag(flags, FLAG_RATE_OF_CHANGE_TEMP_ALARM));
		ShadowBuilder_AddUint32(pMsg, "movementAlarm",
					GetFlag(flags, FLAG_MOVEMENT_ALARM));
		ShadowBuilder_AddUint32(pMsg, "magnetState",
					GetFlag(flags, FLAG_MAGNET_STATE));

		pEntry->lastFlags = flags;
	}
}

static void ShadowLogHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	SensorLogEvent_t event = { .epoch = pEntry->ad.epoch,
				   .data = pEntry->ad.data.u16,
				   .recordType = pEntry->ad.recordType,
				   .idLsb = (uint8_t)pEntry->ad.id };

	SensorLog_Add(pEntry->pLog, &event);

	SensorLog_GenerateJson(pEntry->pLog, pMsg);
}

/* These special items exist on the gateway and not on the sensor */
static void ShadowSpecialHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	ShadowBuilder_AddPair(pMsg, "gatewayId",
			      (char *)attr_get_quasi_static(ATTR_ID_gatewayId),
			      false);

	ShadowBuilder_AddUint32(pMsg, "eventLogSize",
				SensorLog_GetSize(pEntry->pLog));
}

static void SensorAddrToString(SensorEntry_t *pEntry)
{
#if CONFIG_FWK_ASSERT_ENABLED || CONFIG_FWK_ASSERT_ENABLED_USE_ZEPHYR
	int count =
#endif
		snprintk(pEntry->addrString, SENSOR_ADDR_STR_SIZE,
			 "%02x%02x%02x%02x%02x%02x", pEntry->ad.addr.val[5],
			 pEntry->ad.addr.val[4], pEntry->ad.addr.val[3],
			 pEntry->ad.addr.val[2], pEntry->ad.addr.val[1],
			 pEntry->ad.addr.val[0]);
	FRAMEWORK_ASSERT(count == SENSOR_ADDR_STR_LEN);
}

static void GatewayShadowMaker(bool GreenlistProcessed)
{
	if (CONFIG_USE_SINGLE_AWS_TOPIC) {
		return;
	}

	if (!allowGatewayShadowGeneration) {
		return;
	}

	JsonMsg_t *pMsg = BP_TRY_TO_TAKE(
		FWK_BUFFER_MSG_SIZE(JsonMsg_t, SENSOR_GATEWAY_SHADOW_MAX_SIZE));
	if (pMsg == NULL) {
		return;
	}
	pMsg->header.msgCode = FMC_GATEWAY_OUT;
	pMsg->header.rxId = FWK_ID_CLOUD;
	pMsg->size = SENSOR_GATEWAY_SHADOW_MAX_SIZE;

	ShadowBuilder_Start(pMsg, SKIP_MEMSET);
	ShadowBuilder_StartGroup(pMsg, "state");
	/* Setting the desired group to null lets the cloud know
	 * that its request was processed.
	 */
	if (GreenlistProcessed) {
		ShadowBuilder_AddNull(pMsg, "desired");
	}
	ShadowBuilder_StartGroup(pMsg, "reported");
	ShadowBuilder_StartGroup(pMsg, "bt510");
	ShadowBuilder_StartArray(pMsg, "sensors");
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		SensorEntry_t *p = &sensorTable[i];
		if (p->inUse) {
			ShadowBuilder_AddSensorTableArrayEntry(pMsg,
							       p->addrString,
							       p->rxEpoch,
							       p->greenlisted);
		}
	}
	ShadowBuilder_EndArray(pMsg);
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_Finalize(pMsg);

	FRAMEWORK_MSG_SEND(pMsg);
}

/* Returns 1 if the value was changed from its current state. */
static size_t GreenlistByAddress(const char *pAddrString, bool NextState)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		if (AddrStringMatch(pAddrString, i)) {
			if (sensorTable[i].greenlisted != NextState) {
				Greenlist(&sensorTable[i], NextState);
				return 1;
			} else {
				return 0;
			}
		}
	}
	/* Don't add it to the table if it isn't greenlisted because
	 * the client may not care about this sensor.
	 */
	if (NextState) {
		/* The sensor wasn't found.  If we have just reset then
	 	 * the shadow may have values that aren't in our table.
		 */
		bt_addr_t addr = BtAddrStringToStruct(pAddrString);
		i = AddByAddress(&addr);
		if (i < CONFIG_SENSOR_TABLE_SIZE) {
			Greenlist(&sensorTable[i], true);
			return 1;
		}
	}
	return 0;
}

static void Greenlist(SensorEntry_t *pEntry, bool Enable)
{
	if (Enable) {
		if (greenCount < CONFIG_SENSOR_GREENLIST_SIZE) {
			pEntry->greenlisted = true;
			pEntry->subscribed = false;
			pEntry->getAcceptedSubscribed = false;
			pEntry->subscriptionDispatchTime =
				k_uptime_get() +
				(CONFIG_SENSOR_SUBSCRIPTION_DELAY_SECONDS *
				 MSEC_PER_SEC);
			if (pEntry->pLog == NULL) {
				pEntry->pLog = SensorLog_Allocate(
					CONFIG_SENSOR_LOG_MAX_SIZE);
			}
			greenCount += 1;
		} else {
			/* In this case, Bluegrass will repeatedly try to enable sensor.
			 * After ~10 seconds it will give up.
			 */
			pEntry->greenlisted = false;
			LOG_ERR("Unable to greenlist more than %d sensors.",
				CONFIG_SENSOR_GREENLIST_SIZE);
		}
	} else {
		pEntry->greenlisted = false;
		FreeEntryBuffers(pEntry);
		if (greenCount > 0) {
			greenCount -= 1;
		}
	}
}

/* If the cloud desires a configuration change, then send a connect request
 * when the sensor advertisement is seen.
 */
static void ConnectRequestHandler(size_t Index, bool Coded)
{
	FRAMEWORK_DEBUG_ASSERT(Index < CONFIG_SENSOR_TABLE_SIZE);
	SensorEntry_t *pEntry = &sensorTable[Index];

	if (pEntry->pCmd != NULL && !pEntry->configBusy) {
		if (LowBatteryAlarm(pEntry)) {
			LOG_WRN("Discarding configuration request (sensor low battery)");
			FreeCmdBuffers(pEntry);
		} else {
			SensorCmdMsg_t *pMsg = pEntry->pCmd;
			pMsg->header.msgCode = FMC_CONNECT_REQUEST;
			pMsg->header.rxId = FWK_ID_SENSOR_TASK;
			pMsg->tableIndex = Index;
			pMsg->attempts += 1;
			memcpy(&pMsg->addr.a.val, pEntry->ad.addr.val,
			       sizeof(bt_addr_t));
			pMsg->addr.type = BT_ADDR_LE_RANDOM;
			pMsg->useCodedPhy = Coded;
			strncpy(pMsg->name, pEntry->name,
				SENSOR_NAME_MAX_STR_LEN);

			/* sensor task is now responsible for this message */
			pEntry->configBusyVersion = pMsg->configVersion;
			pEntry->configBusy = true;
			pEntry->pCmd = NULL;
			FRAMEWORK_MSG_SEND(pMsg);
		}
	}
}

static void CreateDumpRequest(SensorEntry_t *pEntry)
{
	/* If an empty command is written by cloud, then send dump command. */
	const char *pCmd;
	if (strlen(queryCmd) != 0) {
		pCmd = queryCmd;
	} else {
		pCmd = SENSOR_CMD_DUMP;
	}

	size_t bufSize = strlen(pCmd) + 1;
	SensorCmdMsg_t *pMsg =
		BP_TRY_TO_TAKE(FWK_BUFFER_MSG_SIZE(SensorCmdMsg_t, bufSize));
	if (pMsg != NULL) {
		pMsg->header.msgCode = FMC_CONFIG_REQUEST;
		pMsg->header.txId = FWK_ID_SENSOR_TASK;
		pMsg->header.rxId = FWK_ID_SENSOR_TASK;
		pMsg->size = bufSize;
		pMsg->length = (bufSize > 0) ? (bufSize - 1) : 0;
		pMsg->dumpRequest = true;
		strncpy(pMsg->addrString, pEntry->addrString,
			SENSOR_ADDR_STR_LEN);
		strcpy(pMsg->cmd, pCmd);
		pEntry->dumpBusy = true;
		FRAMEWORK_MSG_SEND(pMsg);
	} else {
		LOG_ERR("Unable to allocate sensor dump");
	}
}

/* The IG60 configures the sensor when its configVersion == 0.
 * Match IG60's behavior to create uniform oob experience.
 */
static void CreateConfigRequest(SensorEntry_t *pEntry)
{
	FRAMEWORK_DEBUG_ASSERT(pEntry->subscribed);
	FRAMEWORK_DEBUG_ASSERT(pEntry->validRsp);
	FRAMEWORK_DEBUG_ASSERT(pEntry->rsp.configVersion == 0);

	const char *pCmd = SENSOR_CMD_SET_CONFIG_VERSION_1;
	size_t bufSize = strlen(pCmd) + 1;
	SensorCmdMsg_t *pMsg =
		BufferPool_Take(FWK_BUFFER_MSG_SIZE(SensorCmdMsg_t, bufSize));
	if (pMsg != NULL) {
		pMsg->header.msgCode = FMC_CONFIG_REQUEST;
		pMsg->header.txId = FWK_ID_SENSOR_TASK;
		pMsg->header.rxId = FWK_ID_SENSOR_TASK;
		pMsg->size = bufSize;
		pMsg->length = (bufSize > 0) ? (bufSize - 1) : 0;
		pMsg->configVersion = 1;
		pMsg->dumpRequest = false;
		pMsg->setEpochRequest = true;
		strncpy(pMsg->addrString, pEntry->addrString,
			SENSOR_ADDR_STR_LEN);
		strcpy(pMsg->cmd, pCmd);
		FRAMEWORK_MSG_SEND(pMsg);
	}
}

static bt_addr_t BtAddrStringToStruct(const char *pAddrString)
{
	/* copy string so that each chunk can be null terminated */
	char str[SENSOR_ADDR_STR_SIZE];
	memcpy(str, pAddrString, SENSOR_ADDR_STR_SIZE);
	/* point to last octet and build in reverse */
	bt_addr_t result;
	char *p = str + (2 * sizeof(bt_addr_t));
	size_t i;
	for (i = 0; i < sizeof(bt_addr_t); i++) {
		p -= 2;
		result.val[i] = strtol(p, NULL, 16);
		*p = 0;
	}
	FRAMEWORK_DEBUG_ASSERT(p == str);
	return result;
}
BUILD_ASSERT((SENSOR_ADDR_STR_LEN / 2) == sizeof(bt_addr_t), "Size Mismatch");

static uint32_t GetFlag(uint16_t Value, uint32_t Mask, uint8_t Position)
{
	uint32_t v = (uint32_t)Value & (Mask << Position);
	return (v >> Position);
}

static void PublishToGetAccepted(SensorEntry_t *pEntry)
{
	size_t size = sizeof(GET_ACCEPTED_MSG);
	JsonMsg_t *pMsg = BufferPool_Take(FWK_BUFFER_MSG_SIZE(JsonMsg_t, size));
	if (pMsg == NULL) {
		return;
	}

	pMsg->header.msgCode = FMC_SENSOR_PUBLISH;
	pMsg->header.rxId = FWK_ID_CLOUD;
	pMsg->size = size;
	char *fmt = SENSOR_GET_TOPIC_FMT_STR;
	snprintk(pMsg->topic, CONFIG_AWS_TOPIC_MAX_SIZE, fmt,
		 pEntry->addrString);
	strcpy(pMsg->buffer, GET_ACCEPTED_MSG);
	pMsg->length = strlen(pMsg->buffer);
	FRAMEWORK_MSG_SEND(pMsg);
}
