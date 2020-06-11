/**
 * @file sensor_table.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(sensor_table);

#define VERBOSE_AD_LOG(...)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <zephyr.h>
#include <bluetooth/bluetooth.h>

#include "laird_bluetooth.h"
#include "mg100_common.h"
#include "qrtc.h"
#include "ad_find.h"
#include "shadow_builder.h"
#include "sensor_cmd.h"
#include "sensor_adv_format.h"
#include "sensor_event.h"
#include "sensor_log.h"
#include "bt510_flags.h"
#include "lte.h"
#include "sdcard_log.h"
#include "sensor_table.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
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
#define CONFIG_SENSOR_TTL_SECONDS (60 * 60 * 2)
#endif

#define JSON_DEFAULT_BUF_SIZE (1536)

/* An empty message can be sent, but a value is sent for test purposes.  */
#define GET_ACCEPTED_MSG "{\"message\":\"hi\"}"

#define SHADOW_BUF_SIZE                                                        \
	(JSON_DEFAULT_BUF_SIZE +                                               \
	 (CONFIG_SENSOR_LOG_MAX_SIZE * SENSOR_LOG_ENTRY_JSON_STR_SIZE))
CHECK_BUFFER_SIZE(FWK_BUFFER_MSG_SIZE(JsonMsg_t, SHADOW_BUF_SIZE));

BUILD_ASSERT_MSG(((sizeof(SENSOR_SUBSCRIPTION_TOPIC_FMT_STR) +
		   SENSOR_ADDR_STR_LEN) < CONFIG_TOPIC_MAX_SIZE),
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
	Bt510AdEvent_t ad;
	Bt510Rsp_t rsp;
	s8_t rssi;
	u8_t lastRecordType;
	u32_t rxEpoch;
	bool whitelisted;
	bool subscribed;
	u32_t ttl;
	void *pCmd;
	void *pSecondCmd;
	bool configBusy;
	u32_t configBusyVersion;
	bool dumpBusy;
	bool firstDumpComplete;
	u32_t adCount;
	u16_t lastFlags;
	SensorLog_t *pLog;
	bool getAcceptedSubscribed;
	bool shadowInitReceived;
} SensorEntry_t;

#define RSSI_UNKNOWN -127

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static bool whitelistProcessed;
static u32_t gatewayShadowUpdateRequests;
static size_t tableCount;
static SensorEntry_t sensorTable[CONFIG_SENSOR_TABLE_SIZE];
static char queryCmd[CONFIG_SENSOR_QUERY_CMD_MAX_SIZE];
static u64_t ttlUptime;
static struct lte_status *pLte;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void ClearTable(void);
static void ClearEntry(SensorEntry_t *pEntry);
static void FreeEntry(SensorEntry_t *pEntry);

static bool FindBt510Advertisement(AdHandle_t *pHandle);
static bool FindBt510ScanResponse(AdHandle_t *pHandle);

static size_t AddByScanResponse(const bt_addr_le_t *pAddr,
				AdHandle_t *pNameHandle, AdHandle_t *pRspHandle,
				s8_t Rssi);
static size_t AddByAddress(const bt_addr_t *pAddr);
static void AddEntry(SensorEntry_t *pEntry, const bt_addr_t *pAddr, s8_t Rssi);
static size_t FindTableIndex(const bt_addr_le_t *pAddr);
static size_t FindFirstFree(void);
static void AdEventHandler(AdHandle_t *pHandle, s8_t Rssi, u32_t Index);

static bool AddrMatch(const void *p, size_t Index);
static bool AddrStringMatch(const char *str, size_t Index);
static bool NameMatch(const char *p, size_t Index);
static bool RspMatch(const void *p, size_t Index);
static bool NewEvent(u16_t Id, size_t Index);

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
static void GatewayShadowMaker(void);
static void TimeToLiveHandler(void);

static char *MangleKey(const char *pKey, const char *pName);
static u32_t WhitelistByAddress(const char *pAddrString, bool NextState);
static void Whitelist(SensorEntry_t *pEntry, bool NextState);

static s32_t GetTemperature(SensorEntry_t *pEntry);
static u32_t GetBattery(SensorEntry_t *pEntry);

static void ConnectRequestHandler(size_t Index);
static void CreateDumpRequest(SensorEntry_t *pEntry, s64_t DelayMs);
static void CreateConfigRequest(SensorEntry_t *pEntry);

static u32_t GetFlag(u16_t Value, u32_t Mask, u8_t Position);

static void PublishToGetAccepted(SensorEntry_t *pEntry);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void SensorTable_Initialize(void)
{
	ClearTable();
	strncpy(queryCmd, SENSOR_CMD_DEFAULT_QUERY,
		CONFIG_SENSOR_QUERY_CMD_MAX_SIZE - 1);
	pLte = lteGetStatus();
}

/* If a new event has occurred then generate a message to send sensor event
 * data to AWS.
 */
void SensorTable_AdvertisementHandler(const bt_addr_le_t *pAddr, s8_t rssi,
				      u8_t type, Ad_t *pAd)

{
	ARG_UNUSED(type);

	/* Filter on presense of manufacturer specific data */
	AdHandle_t manHandle =
		AdFind_Type(pAd->data, pAd->len, BT_DATA_MANUFACTURER_DATA,
			    BT_DATA_INVALID);
	if (manHandle.pPayload == NULL) {
		return;
	}

	size_t tableIndex = CONFIG_SENSOR_TABLE_SIZE;
	/* Take name from scan response and use it to populate table. */
	if (FindBt510ScanResponse(&manHandle)) {
		AdHandle_t nameHandle = AdFind_Name(pAd->data, pAd->len);
		if (nameHandle.pPayload != NULL) {
			tableIndex = AddByScanResponse(pAddr, &nameHandle,
						       &manHandle, rssi);
		}
		/* If scan response data was received then there won't be event data,
		 * but a connect request may still need to be issued */
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
			AdEventHandler(&manHandle, rssi, tableIndex);
		}
	}

	if (tableIndex < CONFIG_SENSOR_TABLE_SIZE) {
		ConnectRequestHandler(tableIndex);
		sensorTable[tableIndex].adCount += 1;
		VERBOSE_AD_LOG("'%s' %u", sensorTable[tableIndex].name,
			       sensorTable[tableIndex].adCount);
	}
}

void SensorTable_ProcessWhitelistRequest(SensorWhitelistMsg_t *pMsg)
{
	u32_t changed = 0;
	size_t i;
	for (i = 0; i < pMsg->sensorCount; i++) {
		changed += WhitelistByAddress(pMsg->sensors[i].addrString,
					      pMsg->sensors[i].whitelist);
	}

	/* This is used to filter out duplicate requests from the cloud. */
	if (changed > 0) {
		whitelistProcessed = true;
		gatewayShadowUpdateRequests += 1;
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
					p->name, pMsg->configVersion);
				/* Delete any oustanding command. */
				if (p->pSecondCmd != NULL) {
					BufferPool_Free(p->pSecondCmd);
				}
				p->pSecondCmd = pMsg;
				return DISPATCH_DO_NOT_FREE;
			}
		} else {
			if (pMsg->dumpRequest) {
				LOG_WRN("State request for '%s'", p->name);
			} else {
				LOG_WRN("New config for sensor '%s' Version: %u",
					p->name, pMsg->configVersion);
			}
			p->pCmd = pMsg;
			return DISPATCH_DO_NOT_FREE;
		}
	}

	LOG_WRN("Config not accepted (version unchanged)");
	return DISPATCH_OK;
}

DispatchResult_t SensorTable_RetryConfigRequest(SensorCmdMsg_t *pMsg)
{
	FRAMEWORK_ASSERT(pMsg != NULL);
	FRAMEWORK_ASSERT(pMsg->tableIndex < CONFIG_SENSOR_TABLE_SIZE);

	if (pMsg->tableIndex < CONFIG_SENSOR_TABLE_SIZE) {
		SensorEntry_t *pEntry = &sensorTable[pMsg->tableIndex];
		pEntry->configBusy = false;
		pEntry->pCmd = pMsg;
	}
	return DISPATCH_DO_NOT_FREE;
}

void SensorTable_AckConfigRequest(SensorCmdMsg_t *pMsg)
{
	FRAMEWORK_ASSERT(pMsg != NULL);
	FRAMEWORK_ASSERT(pMsg->tableIndex < CONFIG_SENSOR_TABLE_SIZE);

	if (pMsg->tableIndex < CONFIG_SENSOR_TABLE_SIZE) {
		SensorEntry_t *pEntry = &sensorTable[pMsg->tableIndex];
		/* After AWS config was written and sensor was reset,
		 * send dump request to read state. */
		pEntry->configBusy = false;
		if (pEntry->pSecondCmd != NULL) {
			pEntry->pCmd = pEntry->pSecondCmd;
			pEntry->pSecondCmd = NULL;
		} else if (pMsg->dumpRequest) {
			pEntry->dumpBusy = false;
			pEntry->firstDumpComplete = true;
		} else {
			CreateDumpRequest(pEntry,
					  BT510_RESET_ACK_TO_DUMP_DELAY_TICKS);
		}
	}

	BufferPool_Free(pMsg);
}

void SensorTable_GenerateGatewayShadow(void)
{
	if (gatewayShadowUpdateRequests > 0) {
		GatewayShadowMaker();
	}
}

/* Using a count for the update requests prevents an update from getting lost
 * between the time it takes to generate a request and receive the ack.
 * The same data may be sent one extra time.
 */
void SensorTable_GatewayShadowAck(void)
{
	if (gatewayShadowUpdateRequests == 1) {
		gatewayShadowUpdateRequests = 0;
	} else if (gatewayShadowUpdateRequests > 1) {
		gatewayShadowUpdateRequests = 1;
	}

	TimeToLiveHandler();
}

void SensorTable_UnsubscribeAll(void)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		sensorTable[i].subscribed = false;
		sensorTable[i].getAcceptedSubscribed = false;
	}
}

void SensorTable_SubscriptionHandler(void)
{
	char *fmt = SENSOR_SUBSCRIPTION_TOPIC_FMT_STR;
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		SensorEntry_t *pEntry = &sensorTable[i];
		/* Waiting until AD and RSP are valid makes things easier for config */
		if (pEntry->validAd && pEntry->validRsp &&
		    (pEntry->whitelisted != pEntry->subscribed)) {
			SubscribeMsg_t *pMsg =
				BufferPool_Take(sizeof(SubscribeMsg_t));
			if (pMsg != NULL) {
				pMsg->header.msgCode = FMC_SUBSCRIBE;
				pMsg->header.rxId = FWK_ID_AWS;
				pMsg->header.txId = FWK_ID_SENSOR_TASK;
				pMsg->subscribe = pEntry->whitelisted;
				pMsg->tableIndex = i;
				pMsg->length =
					snprintk(pMsg->topic,
						 CONFIG_TOPIC_MAX_SIZE, fmt,
						 pEntry->addrString);
				FRAMEWORK_MSG_SEND(pMsg);
				/* For now, assume the subscription will work. */
				pEntry->subscribed = pEntry->whitelisted;
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
		    !pEntry->shadowInitReceived) {
			SubscribeMsg_t *pMsg =
				BufferPool_Take(sizeof(SubscribeMsg_t));
			if (pMsg != NULL) {
				pMsg->header.msgCode = FMC_SUBSCRIBE;
				pMsg->header.rxId = FWK_ID_AWS;
				pMsg->header.txId = FWK_ID_SENSOR_TASK;
				pMsg->subscribe = true;
				pMsg->tableIndex = i;
				pMsg->length =
					snprintk(pMsg->topic,
						 CONFIG_TOPIC_MAX_SIZE, fmt,
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
			FRAMEWORK_ASSERT(pMsg->events[i].epoch != 0);
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
					if (p->rsp.configVersion == 0) {
						CreateConfigRequest(p);
					} else if (!p->firstDumpComplete) {
						CreateDumpRequest(p, 0);
					}
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
	JsonMsg_t *pMsg = BufferPool_Take(FWK_BUFFER_MSG_SIZE(JsonMsg_t, size));
	if (pMsg == NULL) {
		return;
	}
	pMsg->header.msgCode = FMC_SENSOR_PUBLISH;
	pMsg->header.rxId = FWK_ID_AWS;
	pMsg->size = size;

	ShadowBuilder_Start(pMsg, SKIP_MEMSET);
	ShadowBuilder_StartGroup(pMsg, "state");
	/* Clear desired because AWS gets all state information. */
	ShadowBuilder_AddNull(pMsg, "desired");
	/* Add the entire response.  AWS app will ignore jsonrpc, id field,
	 * and status fields. */
	ShadowBuilder_AddString(pMsg, "reported", pRsp->buffer);
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_Finalize(pMsg);

	char *fmt = SENSOR_UPDATE_TOPIC_FMT_STR;
	snprintk(pMsg->topic, CONFIG_TOPIC_MAX_SIZE, fmt, pAddrStr);
	FRAMEWORK_MSG_SEND(pMsg);
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
	FreeEntry(pEntry);
	memset(pEntry, 0, sizeof(SensorEntry_t));
}

static void FreeEntry(SensorEntry_t *pEntry)
{
	if (pEntry->pCmd != NULL) {
		BufferPool_Free(pEntry->pCmd);
		pEntry->pCmd = NULL;
	}
	if (pEntry->pSecondCmd != NULL) {
		BufferPool_Free(pEntry->pSecondCmd);
		pEntry->pSecondCmd = NULL;
	}
	if (pEntry->pLog != NULL) {
		SensorLog_Free(pEntry->pLog);
		pEntry->pLog = NULL;
	}
}

static void AdEventHandler(AdHandle_t *pHandle, s8_t Rssi, u32_t Index)
{
	sensorTable[Index].ttl = CONFIG_SENSOR_TTL_SECONDS;

	Bt510AdEvent_t *p = (Bt510AdEvent_t *)pHandle->pPayload;
	if (NewEvent(p->id, Index)) {
		sensorTable[Index].validAd = true;
		LOG_DBG("New Event for [%u] '%s' (%s) RSSI: %d", Index,
			sensorTable[Index].name, sensorTable[Index].addrString,
			Rssi);
		sensorTable[Index].lastRecordType =
			sensorTable[Index].ad.recordType;
		memcpy(&sensorTable[Index].ad, pHandle->pPayload,
		       sizeof(Bt510AdEvent_t));
		sensorTable[Index].rssi = Rssi;
		sensorTable[Index].rxEpoch = Qrtc_GetEpoch();
		ShadowMaker(&sensorTable[Index]);
		/* The cloud uses the RX epoch (in the table) for filtering. */
		gatewayShadowUpdateRequests += 1;

		sdCardLogAdEvent(p);
	}
}

/* The BT510 advertisement can be recognized by the manufacturer
 * specific data type with LAIRD as the company ID.
 * It is further qualified by having a length of 27 and matching protocol ID.
 */
static bool FindBt510Advertisement(AdHandle_t *pHandle)
{
	if (pHandle->pPayload != NULL) {
		if ((pHandle->size == BT510_MSD_AD_PAYLOAD_LENGTH)) {
			if (memcmp(pHandle->pPayload, BT510_AD_HEADER,
				   sizeof(BT510_AD_HEADER)) == 0) {
				return true;
			}
		}
	}
	return false;
}

static bool FindBt510ScanResponse(AdHandle_t *pHandle)
{
	if (pHandle->pPayload != NULL) {
		if ((pHandle->size == BT510_MSD_RSP_PAYLOAD_LENGTH)) {
			if (memcmp(pHandle->pPayload, BT510_RSP_HEADER,
				   sizeof(BT510_RSP_HEADER)) == 0) {
				return true;
			}
		}
	}
	return false;
}

static size_t AddByScanResponse(const bt_addr_le_t *pAddr,
				AdHandle_t *pNameHandle, AdHandle_t *pRspHandle,
				s8_t Rssi)
{
	if (pNameHandle->pPayload == NULL) {
		return CONFIG_SENSOR_TABLE_SIZE;
	}

	if (pRspHandle->pPayload == NULL) {
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
		if (!RspMatch(pRspHandle->pPayload, i)) {
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
			memcpy(&pEntry->rsp, pRspHandle->pPayload,
			       pRspHandle->size);
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

static void AddEntry(SensorEntry_t *pEntry, const bt_addr_t *pAddr, s8_t Rssi)
{
	tableCount += 1;
	gatewayShadowUpdateRequests += 1;
	pEntry->inUse = true;
	pEntry->rssi = Rssi;
	memcpy(pEntry->ad.addr.val, pAddr->val, sizeof(bt_addr_t));
	/* The address is duplicated in the advertisement payload because
	 * some operating systems don't provide the Bluetooth address to
	 * the application.  The address is copied into the AD field
	 * because the two formats are the same.
	 */
	SensorAddrToString(pEntry);
	LOG_INF("Added BT510 sensor %s '%s' RSSI: %d", pEntry->addrString,
		pEntry->name, pEntry->rssi);
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

static bool RspMatch(const void *p, size_t Index)
{
	return (memcmp(p, &sensorTable[Index].rsp, sizeof(Bt510Rsp_t)) == 0);
}

static bool NewEvent(u16_t Id, size_t Index)
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
	 * been whitelisted. */
	if (!CONFIG_USE_SINGLE_AWS_TOPIC) {
		if (!pEntry->whitelisted || !pEntry->shadowInitReceived) {
			return;
		}
	}

	JsonMsg_t *pMsg = BufferPool_Take(
		FWK_BUFFER_MSG_SIZE(JsonMsg_t, SHADOW_BUF_SIZE));
	if (pMsg == NULL) {
		return;
	}

	pMsg->header.msgCode = FMC_SENSOR_PUBLISH;
	pMsg->header.rxId = FWK_ID_AWS;
	pMsg->size = SHADOW_BUF_SIZE;

	ShadowBuilder_Start(pMsg, SKIP_MEMSET);
	ShadowBuilder_StartGroup(pMsg, "state");
	ShadowBuilder_StartGroup(pMsg, "reported");
	if (CONFIG_USE_SINGLE_AWS_TOPIC) {
		ShadowTemperatureHandler(pMsg, pEntry);
		/* Sending RSSI prevents an empty buffer when
		 * temperature isn't present. */
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
	 * the format of the address field generated by ShadowGatewayMaker. */
	char *fmt = SENSOR_UPDATE_TOPIC_FMT_STR;
	snprintk(pMsg->topic, CONFIG_TOPIC_MAX_SIZE, fmt, pEntry->addrString);

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
		ShadowBuilder_AddUint32(pMsg, "hardwareMinorVersion",
					pEntry->rsp.hardwareMinorVersion);
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
static s32_t GetTemperature(SensorEntry_t *pEntry)
{
	return (s32_t)((s16_t)pEntry->ad.data);
}

static u32_t GetBattery(SensorEntry_t *pEntry)
{
	return (u32_t)((u16_t)pEntry->ad.data);
}

static void ShadowTemperatureHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	s32_t temperature = GetTemperature(pEntry);
	if (CONFIG_USE_SINGLE_AWS_TOPIC) {
		/* The desired format is degrees when publishing to a single topic
		 * because that is how the BL654 Sensor data is formatted. */
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
					(u32_t)pEntry->ad.data);
		break;
	case SENSOR_EVENT_RESET:
		ShadowBuilder_AddPair(
			pMsg, "resetReason",
			lbt_get_nrf52_reset_reason_string(pEntry->ad.data),
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
	s32_t t = GetTemperature(pEntry);
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
	u16_t flags = pEntry->ad.flags;
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
				   .data = pEntry->ad.data,
				   .recordType = pEntry->ad.recordType,
				   .idLsb = (u8_t)pEntry->ad.id };

	SensorLog_Add(pEntry->pLog, &event);

	SensorLog_GenerateJson(pEntry->pLog, pMsg);
}

/* These special items exist on the gateway and not on the sensor */
static void ShadowSpecialHandler(JsonMsg_t *pMsg, SensorEntry_t *pEntry)
{
	ShadowBuilder_AddPair(pMsg, "gatewayId", pLte->IMEI, false);
	ShadowBuilder_AddUint32(pMsg, "eventLogSize",
				SensorLog_GetSize(pEntry->pLog));
}

static void SensorAddrToString(SensorEntry_t *pEntry)
{
#if CONFIG_FWK_ASSERT_ENABLED || CONFIG_FWK_ASSERT_ENABLED_USE_ZEPHYR
	int count =
#endif
		snprintk(pEntry->addrString, SENSOR_ADDR_STR_SIZE,
			 "%02X%02X%02X%02X%02X%02X", pEntry->ad.addr.val[5],
			 pEntry->ad.addr.val[4], pEntry->ad.addr.val[3],
			 pEntry->ad.addr.val[2], pEntry->ad.addr.val[1],
			 pEntry->ad.addr.val[0]);
	FRAMEWORK_ASSERT(count == SENSOR_ADDR_STR_LEN);
}

static void GatewayShadowMaker(void)
{
	if (CONFIG_USE_SINGLE_AWS_TOPIC) {
		return;
	}

	JsonMsg_t *pMsg = BufferPool_Take(
		FWK_BUFFER_MSG_SIZE(JsonMsg_t, SENSOR_GATEWAY_SHADOW_MAX_SIZE));
	if (pMsg == NULL) {
		return;
	}
	pMsg->header.msgCode = FMC_GATEWAY_OUT;
	pMsg->header.rxId = FWK_ID_AWS;
	pMsg->size = SENSOR_GATEWAY_SHADOW_MAX_SIZE;

	ShadowBuilder_Start(pMsg, SKIP_MEMSET);
	ShadowBuilder_StartGroup(pMsg, "state");
	/* Setting the desired group to null lets the cloud know
	 * that its request was processed. */
	if (whitelistProcessed) {
		whitelistProcessed = false;
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
							       p->whitelisted);
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
static u32_t WhitelistByAddress(const char *pAddrString, bool NextState)
{
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		if (AddrStringMatch(pAddrString, i)) {
			if (sensorTable[i].whitelisted != NextState) {
				Whitelist(&sensorTable[i], NextState);
				return 1;
			} else {
				return 0;
			}
		}
	}
	/* Don't add it to the table if it isn't whitelisted because
	 * the client may not care about this sensor. */
	if (NextState) {
		/* The sensor wasn't found.  If we have just reset then
	 	 * the shadow may have values that aren't in our table. */
		bt_addr_t addr = BtAddrStringToStruct(pAddrString);
		i = AddByAddress(&addr);
		if (i < CONFIG_SENSOR_TABLE_SIZE) {
			Whitelist(&sensorTable[i], true);
			return 1;
		}
	}
	return 0;
}

static void Whitelist(SensorEntry_t *pEntry, bool NextState)
{
	pEntry->whitelisted = NextState;
	if (pEntry->whitelisted) {
		if (pEntry->pLog == NULL) {
			pEntry->pLog =
				SensorLog_Allocate(CONFIG_SENSOR_LOG_MAX_SIZE);
		}
	} else {
		FreeEntry(pEntry);
	}
}

/* If the cloud desires a configuration change, then send a connect request
 * when the sensor advertisement is seen. */
static void ConnectRequestHandler(size_t Index)
{
	FRAMEWORK_DEBUG_ASSERT(Index < CONFIG_SENSOR_TABLE_SIZE);
	SensorEntry_t *pEntry = &sensorTable[Index];

	if (pEntry->pCmd != NULL && !pEntry->configBusy) {
		SensorCmdMsg_t *pMsg = pEntry->pCmd;
		if (pMsg->dispatchTime <= k_uptime_get()) {
			pMsg->header.msgCode = FMC_CONNECT_REQUEST;
			pMsg->header.rxId = FWK_ID_SENSOR_TASK;

			pMsg->tableIndex = Index;
			pMsg->attempts += 1;
			memcpy(&pMsg->addr.a.val, pEntry->ad.addr.val,
			       sizeof(bt_addr_t));
			pMsg->addr.type = BT_ADDR_LE_RANDOM;
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

static void CreateDumpRequest(SensorEntry_t *pEntry, s64_t DelayMs)
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
		BufferPool_Take(FWK_BUFFER_MSG_SIZE(SensorCmdMsg_t, bufSize));
	if (pMsg != NULL) {
		pMsg->header.msgCode = FMC_CONFIG_REQUEST;
		pMsg->header.txId = FWK_ID_SENSOR_TASK;
		pMsg->header.rxId = FWK_ID_SENSOR_TASK;
		pMsg->size = bufSize;
		pMsg->length = bufSize - 1;
		pMsg->dumpRequest = true;
		pMsg->dispatchTime = k_uptime_get() + DelayMs;
		strncpy(pMsg->addrString, pEntry->addrString,
			SENSOR_ADDR_STR_LEN);
		strcpy(pMsg->cmd, pCmd);
		pEntry->dumpBusy = true;
		FRAMEWORK_MSG_SEND(pMsg);
	}
}

/* The IG60 configures the sensor when its configVersion == 0.
 * Match IG60's behavior to create uniform experience.
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
		pMsg->length = bufSize - 1;
		pMsg->configVersion = 1;
		pMsg->dumpRequest = false;
		pMsg->setEpochRequest = true;
		strncpy(pMsg->addrString, pEntry->addrString,
			SENSOR_ADDR_STR_LEN);
		strcpy(pMsg->cmd, pCmd);
		pMsg->resetRequest = SensorCmd_RequiresReset(pMsg->cmd);
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
BUILD_ASSERT_MSG((SENSOR_ADDR_STR_LEN / 2) == sizeof(bt_addr_t),
		 "Size Mismatch");

static u32_t GetFlag(u16_t Value, u32_t Mask, u8_t Position)
{
	u32_t v = (u32_t)Value & (Mask << Position);
	return (v >> Position);
}

/* If a sensor hasn't been seen (its ttl count is zero),
 * then remove it from the table.  Don't remove sensor
 * if it has been whitelisted by AWS.
 */
static void TimeToLiveHandler(void)
{
	u64_t deltaS = k_uptime_delta(&ttlUptime) / K_SECONDS(1);
	size_t i;
	for (i = 0; i < CONFIG_SENSOR_TABLE_SIZE; i++) {
		SensorEntry_t *p = &sensorTable[i];
		if (p->inUse) {
			p->ttl = (p->ttl > deltaS) ? (p->ttl - deltaS) : 0;
			if (p->ttl == 0 && !p->whitelisted) {
				LOG_WRN("Removing '%s' sensor %s from table",
					log_strdup(p->name),
					log_strdup(p->addrString));
				ClearEntry(p);
				FRAMEWORK_DEBUG_ASSERT(tableCount > 0);
				tableCount -= 1;
			}
		}
	}
}

static void PublishToGetAccepted(SensorEntry_t *pEntry)
{
	size_t size = sizeof(GET_ACCEPTED_MSG);
	JsonMsg_t *pMsg = BufferPool_Take(FWK_BUFFER_MSG_SIZE(JsonMsg_t, size));
	if (pMsg == NULL) {
		return;
	}

	pMsg->header.msgCode = FMC_SENSOR_PUBLISH;
	pMsg->header.rxId = FWK_ID_AWS;
	pMsg->size = size;
	char *fmt = SENSOR_GET_TOPIC_FMT_STR;
	snprintk(pMsg->topic, CONFIG_TOPIC_MAX_SIZE, fmt, pEntry->addrString);
	strcpy(pMsg->buffer, GET_ACCEPTED_MSG);
	pMsg->length = strlen(pMsg->buffer);
	FRAMEWORK_MSG_SEND(pMsg);
}
