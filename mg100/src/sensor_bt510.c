/**
 * @file sensor_bt510.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(sensor_bt510);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/

#include <string.h>
#include <zephyr.h>
#include <bluetooth/bluetooth.h>

#include "laird_bluetooth.h"
#include "mg100_common.h"
#include "qrtc.h"
#include "ad_find.h"
#include "shadow_builder.h"
#include "sensor_bt510.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/

#define MAX_KEY_STR_LEN 64
#define MANGLED_NAME_MAX_STR_LEN                                               \
	(BT510_SENSOR_NAME_MAX_SIZE + sizeof('-') + MAX_KEY_STR_LEN)
#define MANGLED_NAME_MAX_SIZE (MANGLED_NAME_MAX_STR_LEN + 1)

/* The +2 is an estimate to account for the JSON array wrapper */
#define SENSOR_GATEWAY_SHADOW_SIZE                                             \
	((BT510_SENSOR_TABLE_SIZE + 2) *                                       \
	 sizeof("[\"abcdabcdabcd\", 4294967296, false],"))
BUILD_ASSERT_MSG(SENSOR_GATEWAY_SHADOW_SIZE <= JSON_OUT_BUFFER_SIZE,
		 "JSON buffer too small");

#define LAIRD_MANUFACTURER_SPECIFIC_COMPANY_ID1 0x0077
#define LAIRD_MANUFACTURER_SPECIFIC_COMPANY_ID2 0x00E4
#define BT510_1M_PHY_AD_PROTOCOL_ID 0x0001
#define BT510_CODED_PHY_AD_PROTOCOL_ID 0x0002
#define BT510_1M_PHY_RSP_PROTOCOL_ID 0x0003

#define BT510_MAX_EVENT_ID 0xFFFF

/*
 * This is the format for the 1M PHY.
 */
#define BT510_MSD_AD_FIELD_LENGTH 0x1b
#define BT510_MSD_AD_PAYLOAD_LENGTH (BT510_MSD_AD_FIELD_LENGTH - 1)
BUILD_ASSERT_MSG(sizeof(Bt510AdEvent_t) == BT510_MSD_AD_PAYLOAD_LENGTH,
		 "BT510 Advertisement data size mismatch (check packing)");

#define BT510_MSD_RSP_FIELD_LENGTH 0x10
#define BT510_MSD_RSP_PAYLOAD_LENGTH (BT510_MSD_RSP_FIELD_LENGTH - 1)
BUILD_ASSERT_MSG(sizeof(Bt510Rsp_t) == BT510_MSD_RSP_PAYLOAD_LENGTH,
		 "BT510 Scan Response size mismatch (check packing)");

const u8_t BT510_AD_HEADER[] = {
	LSB_16(LAIRD_MANUFACTURER_SPECIFIC_COMPANY_ID1),
	MSB_16(LAIRD_MANUFACTURER_SPECIFIC_COMPANY_ID1),
	LSB_16(BT510_1M_PHY_AD_PROTOCOL_ID), MSB_16(BT510_1M_PHY_AD_PROTOCOL_ID)
};

const u8_t BT510_RSP_HEADER[] = {
	LSB_16(LAIRD_MANUFACTURER_SPECIFIC_COMPANY_ID2),
	MSB_16(LAIRD_MANUFACTURER_SPECIFIC_COMPANY_ID2),
	LSB_16(BT510_1M_PHY_RSP_PROTOCOL_ID),
	MSB_16(BT510_1M_PHY_RSP_PROTOCOL_ID)
};

typedef struct SensorTableEntry {
	bool inUse;
	bool updatedName;
	bool updatedRsp;
	char name[BT510_SENSOR_NAME_MAX_SIZE];
	char addrString[BT510_ADDR_STR_SIZE];
	Bt510AdEvent_t ad;
	Bt510Rsp_t rsp;
	s8_t rssi;
	u8_t lastRecordType;
	u32_t rxEpoch;
	bool whitelisted;
} SensorTable_t;

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static bool whitelistProcessed;
static bool gatewayShadowNeedsUpdate;
static size_t tableCount;
static SensorTable_t sensorTable[BT510_SENSOR_TABLE_SIZE];

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void ClearTable(void);
static AdHandle_t FindBt510Advertisement(u8_t *pAdv, size_t Length);
static AdHandle_t FindBt510ScanResponse(u8_t *pAdv, size_t Length);

static void AddEntry(const bt_addr_le_t *pAddr, AdHandle_t *pNameHandle,
		     AdHandle_t *pRspHandle);
static size_t FindTableIndex(const bt_addr_le_t *pAddr);
static size_t FindFirstFree(void);
static void AdEventHandler(AdHandle_t *pHandle, u8_t Rssi, u32_t Index);

static bool AddrMatch(const void *p, size_t Index);
static bool NameMatch(const char *p, size_t Index);
static bool RspMatch(const void *p, size_t Index);
static bool NewEvent(u16_t Id, size_t Index);

static void Bt510AddrToString(SensorTable_t *pEntry);

static void ShadowMaker(SensorTable_t *pEntry);
static void ShadowTemperatureHandler(JsonMsg_t *pMsg, SensorTable_t *pEntry);
static void ShadowLastEventHandler(JsonMsg_t *pMsg, SensorTable_t *pEntry);
static void ShadowEventHandler(JsonMsg_t *pMsg, SensorTable_t *pEntry);
static void ShadowBasicHandler(JsonMsg_t *pMsg, SensorTable_t *pEntry);
static void ShadowRspHandler(JsonMsg_t *pMsg, SensorTable_t *pEntry);
static void GatewayShadowMaker(void);

static char *MangleKey(const char *pKey, const char *pName);
static void Whitelist(const char *pAddrString, bool NextState);

static s32_t GetTemperature(SensorTable_t *pEntry);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void SensorBt510_Initialize(void)
{
	ClearTable();
}

/* If a new event has occurred then generate a message to send sensor event
 * data to AWS.
 */
void SensorBt510_AdvertisementHandler(const bt_addr_le_t *pAddr, s8_t rssi,
				      u8_t type, Ad_t *pAd)

{
	ARG_UNUSED(type);

	/* Take name from scan response and use it to populate table. */
	AdHandle_t rspHandle = FindBt510ScanResponse(pAd->data, pAd->len);
	if (rspHandle.pPayload != NULL) {
		AdHandle_t nameHandle = AdFind_Name(pAd->data, pAd->len);
		if (nameHandle.pPayload != NULL) {
			AddEntry(pAddr, &nameHandle, &rspHandle);
		}
		/* If scan response data was received then there won't be event data */
		return;
	}

	/* If sensor name/addr isn't in table of BT510 devices,
	 * then the data can't be used. */
	u32_t tableIndex = FindTableIndex(pAddr);
	if (tableIndex < BT510_SENSOR_TABLE_SIZE) {
		AdHandle_t adHandle =
			FindBt510Advertisement(pAd->data, pAd->len);
		if (adHandle.pPayload != NULL) {
			FRAMEWORK_ASSERT(
				memcmp(sensorTable[tableIndex].ad.addr.val,
				       pAddr->a.val, sizeof(bt_addr_t)) == 0);
			AdEventHandler(&adHandle, rssi, tableIndex);
		}
	}
}

size_t SensorBt510_Count(void)
{
	return tableCount;
}

void SensorBt510_ProcessWhitelistRequest(SensorWhitelistMsg_t *pMsg)
{
	size_t i;
	for (i = 0; i < pMsg->sensorCount; i++) {
		Whitelist(pMsg->sensors[i].addrString,
			  pMsg->sensors[i].whitelist);
	}
	whitelistProcessed = true;
	gatewayShadowNeedsUpdate = true;
}

void SensorBt510_GenerateGatewayShadow(void)
{
	if (gatewayShadowNeedsUpdate) {
		GatewayShadowMaker();
		gatewayShadowNeedsUpdate = false;
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
	for (i = 0; i < BT510_SENSOR_TABLE_SIZE; i++) {
		memset(&sensorTable[i], 0, sizeof(SensorTable_t));
		sensorTable[i].ad.id = BT510_MAX_EVENT_ID;
	}
	tableCount = 0;
}

static void AdEventHandler(AdHandle_t *pHandle, u8_t Rssi, u32_t Index)
{
	Bt510AdEvent_t *p = (Bt510AdEvent_t *)pHandle->pPayload;
	bool newEvent = NewEvent(p->id, Index);
	if (newEvent) {
		LOG_DBG("New Event for %s", sensorTable[Index].name);
		sensorTable[Index].lastRecordType =
			sensorTable[Index].ad.recordType;
		memcpy(&sensorTable[Index].ad, pHandle->pPayload,
		       sizeof(Bt510AdEvent_t));
		sensorTable[Index].rssi = Rssi;
		sensorTable[Index].rxEpoch = Qrtc_GetEpoch();

		ShadowMaker(&sensorTable[Index]);
	}
}

/* The BT510 advertisement can be recognized by the manufacturer
 * specific data type with LAIRD as the company ID.
 * It is further qualified by having a length of 27 and matching protocol ID.
 */
static AdHandle_t FindBt510Advertisement(u8_t *pAdv, size_t Length)
{
	AdHandle_t result = AdFind_Type(pAdv, Length, BT_DATA_MANUFACTURER_DATA,
					BT_DATA_INVALID);

	if (result.pPayload != NULL) {
		if ((result.size == BT510_MSD_AD_PAYLOAD_LENGTH)) {
			if (memcmp(result.pPayload, BT510_AD_HEADER,
				   sizeof(BT510_AD_HEADER)) == 0) {
				return result;
			}
		}
	}
	result.pPayload = NULL;
	return result;
}

static AdHandle_t FindBt510ScanResponse(u8_t *pAdv, size_t Length)
{
	AdHandle_t result = AdFind_Type(pAdv, Length, BT_DATA_MANUFACTURER_DATA,
					BT_DATA_INVALID);

	if (result.pPayload != NULL) {
		if ((result.size == BT510_MSD_RSP_PAYLOAD_LENGTH)) {
			if (memcmp(result.pPayload, BT510_RSP_HEADER,
				   sizeof(BT510_RSP_HEADER)) == 0) {
				return result;
			}
		}
	}
	result.pPayload = NULL;
	return result;
}

static void AddEntry(const bt_addr_le_t *pAddr, AdHandle_t *pNameHandle,
		     AdHandle_t *pRspHandle)
{
	if (pNameHandle->pPayload == NULL) {
		return;
	}

	if (pRspHandle->pPayload == NULL) {
		return;
	}

	/* The first free entry will be used after entire table is searched. */
	bool add = false;
	bool updateRsp = false;
	bool updateName = false;
	SensorTable_t *pEntry = NULL;
	size_t i = FindTableIndex(pAddr);
	if (i < BT510_SENSOR_TABLE_SIZE) {
		pEntry = &sensorTable[i];
		if (!NameMatch(pNameHandle->pPayload, i)) {
			updateName = true;
		}
		if (!RspMatch(pRspHandle->pPayload, i)) {
			updateRsp = true;
		}
	} else {
		i = FindFirstFree();
		if (i < BT510_SENSOR_TABLE_SIZE) {
			pEntry = &sensorTable[i];
			add = true;
		}
	}

	if ((pEntry != NULL) && (add || updateRsp || updateName)) {
		pEntry->inUse = true;
		if (add || updateRsp) {
			pEntry->updatedRsp = true;
			memcpy(&pEntry->rsp, pRspHandle->pPayload,
			       pRspHandle->size);
		}
		if (add || updateName) {
			pEntry->updatedName = true;
			memset(pEntry->name, 0, BT510_SENSOR_NAME_MAX_SIZE);
			strncpy(pEntry->name, pNameHandle->pPayload,
				MIN(BT510_SENSOR_NAME_MAX_STR_LEN,
				    pNameHandle->size));
		}
		if (add) {
			tableCount += 1;
			gatewayShadowNeedsUpdate = true;
			/* The address is duplicated in the advertisement payload because
			 * some operating systems don't provide the Bluetooth address to
			 * the application.  The address is copied into the AD field
			 * because the two formats are the same.
			 */
			memcpy(pEntry->ad.addr.val, pAddr->a.val,
			       sizeof(bt_addr_t));
			Bt510AddrToString(pEntry);
			LOG_INF("Added BT510 sensor %s %s", pEntry->addrString,
				pEntry->name);
		}
	}
}

/* Find index of advertiser's address in the sensor table */
static size_t FindTableIndex(const bt_addr_le_t *pAddr)
{
	size_t i;
	for (i = 0; i < BT510_SENSOR_TABLE_SIZE; i++) {
		if (sensorTable[i].inUse) {
			if (AddrMatch(pAddr->a.val, i)) {
				return i;
			}
		}
	}
	return BT510_SENSOR_TABLE_SIZE;
}

static size_t FindFirstFree(void)
{
	size_t i;
	for (i = 0; i < BT510_SENSOR_TABLE_SIZE; i++) {
		if (!sensorTable[i].inUse) {
			return i;
		}
	}
	return BT510_SENSOR_TABLE_SIZE;
}

static bool AddrMatch(const void *p, size_t Index)
{
	return (memcmp(p, sensorTable[Index].ad.addr.val, sizeof(bt_addr_t)) ==
		0);
}

static bool NameMatch(const char *p, size_t Index)
{
	return (strncmp(p, sensorTable[Index].name,
			BT510_SENSOR_NAME_MAX_STR_LEN) == 0);
}

static bool RspMatch(const void *p, size_t Index)
{
	return (memcmp(p, &sensorTable[Index].rsp, sizeof(Bt510Rsp_t)) == 0);
}

static bool NewEvent(u16_t Id, size_t Index)
{
	return (Id != sensorTable[Index].ad.id);
}

static void ShadowMaker(SensorTable_t *pEntry)
{
	/* AWS will disconnect if data is sent for devices that have not
	 * been whitelisted. */
	if (!BT510_USES_SINGLE_AWS_TOPIC) {
		if (!pEntry->whitelisted) {
			return;
		}
	}

	JsonMsg_t *pMsg = BufferPool_TryToTake(sizeof(JsonMsg_t));
	if (pMsg == NULL) {
		return;
	}

	pMsg->header.msgCode = FMC_BT510_EVENT;
	pMsg->header.rxId = FWK_ID_AWS;

	ShadowBuilder_Start(pMsg, SKIP_MEMSET);
	if (BT510_USES_SINGLE_AWS_TOPIC) {
		ShadowBuilder_StartGroup(pMsg, "state");
		ShadowBuilder_StartGroup(pMsg, "reported");
		ShadowTemperatureHandler(pMsg, pEntry);
		/* Sending RSSI prevents an empty buffer when
		 * temperature isn't present. */
		ShadowBuilder_AddSigned32(pMsg, MangleKey(pEntry->name, "rssi"),
					  pEntry->rssi);
		ShadowBuilder_EndGroup(pMsg);
		ShadowBuilder_EndGroup(pMsg);
	} else {
		ShadowBuilder_StartGroup(pMsg, "reported");
		ShadowBasicHandler(pMsg, pEntry);
		ShadowRspHandler(pMsg, pEntry);
		ShadowLastEventHandler(pMsg, pEntry);
		ShadowTemperatureHandler(pMsg, pEntry);
		ShadowEventHandler(pMsg, pEntry);
		ShadowBuilder_EndGroup(pMsg);
	}
	ShadowBuilder_Finalize(pMsg);

	/* The part of the topic that changes must match
	 * the format of the address field generated by ShadowGatewayMaker. */
	snprintk(pMsg->topic, TOPIC_MAX_SIZE, "$aws/things/%s/shadow/update",
		 pEntry->addrString);

	FRAMEWORK_MSG_TRY_TO_SEND(pMsg);
}

/**
 * @brief Create unique names for each key so that everything can be
 * sent to a single topic.
 */
static char *MangleKey(const char *pName, const char *pKey)
{
#if BT510_USES_SINGLE_AWS_TOPIC
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

/* Add the properties that are always included. */
static void ShadowBasicHandler(JsonMsg_t *pMsg, SensorTable_t *pEntry)
{
	pEntry->updatedName = false;
	ShadowBuilder_AddPair(pMsg, "bluetoothAddress", pEntry->addrString,
			      SB_IS_STRING);
	ShadowBuilder_AddPair(pMsg, "sensorName", pEntry->name, SB_IS_STRING);
	ShadowBuilder_AddSigned32(pMsg, "rssi", pEntry->rssi);
	ShadowBuilder_AddUint32(pMsg, "networkId", pEntry->ad.networkId);
	ShadowBuilder_AddUint32(pMsg, "flags", pEntry->ad.flags);
	ShadowBuilder_AddUint32(pMsg, "resetCount", pEntry->ad.resetCount);
}

/**
 * @brief Build JSON for items that are in the Scan Response
 * and don't change that often (when device is added to table).
 */
static void ShadowRspHandler(JsonMsg_t *pMsg, SensorTable_t *pEntry)
{
	if (pEntry->updatedRsp) {
		pEntry->updatedRsp = false;
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
}

/* Clears certain properties after they occur. */
static void ShadowLastEventHandler(JsonMsg_t *pMsg, SensorTable_t *pEntry)
{
	if (pEntry->ad.recordType == pEntry->lastRecordType) {
		return;
	}

	switch (pEntry->lastRecordType) {
	case SENSOR_EVENT_MOVEMENT:
		ShadowBuilder_AddFalse(pMsg, "movement");
		break;
	case SENSOR_EVENT_ALARM_HIGH_TEMP_CLEAR:
		ShadowBuilder_AddFalse(pMsg, "highTemperatureAlarmClear1");
		ShadowBuilder_AddFalse(pMsg, "highTemperatureAlarmClear2");
		break;
	case SENSOR_EVENT_ALARM_LOW_TEMP_CLEAR:
		ShadowBuilder_AddFalse(pMsg, "lowTemperatureAlarmClear1");
		ShadowBuilder_AddFalse(pMsg, "lowTemperatureAlarmClear2");
		break;
	case SENSOR_EVENT_ADV_ON_BUTTON:
		ShadowBuilder_AddFalse(pMsg, "advOnButton");
		break;
	case SENSOR_EVENT_RESET:
		ShadowBuilder_AddFalse(pMsg, "reset");
	default:
		break;
	}
}

/* The generic data field is unsigned but the temperature is signed.
 * Get temperature from advertisement (assumes event contains temperature).
 * retval temperature in hundredths of degree C
 */
static s32_t GetTemperature(SensorTable_t *pEntry)
{
	return (s32_t)((s16_t)pEntry->ad.data);
}

static void ShadowTemperatureHandler(JsonMsg_t *pMsg, SensorTable_t *pEntry)
{
	s32_t temperature = GetTemperature(pEntry);
	if (BT510_USES_SINGLE_AWS_TOPIC) {
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
			MangleKey(pEntry->name, BT510_USES_SINGLE_AWS_TOPIC ?
							"temperature" :
							"tempCc"),
			temperature);
		break;
	default:
		break;
	}
}

static void ShadowEventHandler(JsonMsg_t *pMsg, SensorTable_t *pEntry)
{
	s32_t temperature = GetTemperature(pEntry);
	switch (pEntry->ad.recordType) {
	case SENSOR_EVENT_MAGNET:
		ShadowBuilder_AddPair(
			pMsg, "magnet",
			(pEntry->ad.data == MAGNET_NEAR) ? "false" : "true",
			SB_IS_NOT_STRING);
		break;
	case SENSOR_EVENT_MOVEMENT:
		ShadowBuilder_AddTrue(pMsg, "movement");
		break;
	case SENSOR_EVENT_ALARM_HIGH_TEMP_1:
		ShadowBuilder_AddSigned32(pMsg, "highAlarmTemp1", temperature);
		break;
	case SENSOR_EVENT_ALARM_HIGH_TEMP_2:
		ShadowBuilder_AddSigned32(pMsg, "highAlarmTemp2", temperature);
		break;
	case SENSOR_EVENT_ALARM_HIGH_TEMP_CLEAR:
		ShadowBuilder_AddTrue(pMsg, "highTemperatureAlarmClear1");
		ShadowBuilder_AddTrue(pMsg, "highTemperatureAlarmClear2");
		break;
	case SENSOR_EVENT_ALARM_LOW_TEMP_1:
		ShadowBuilder_AddSigned32(pMsg, "lowTemperatureAlarm1",
					  temperature);
		break;
	case SENSOR_EVENT_ALARM_LOW_TEMP_2:
		ShadowBuilder_AddSigned32(pMsg, "lowTemperatureAlarm2",
					  temperature);
		break;
	case SENSOR_EVENT_ALARM_LOW_TEMP_CLEAR:
		ShadowBuilder_AddTrue(pMsg, "lowTemperatureAlarmClear1");
		ShadowBuilder_AddTrue(pMsg, "lowTemperatureAlarmClear2");
		break;
	case SENSOR_EVENT_BATTERY_GOOD:
	case SENSOR_EVENT_BATTERY_BAD:
		ShadowBuilder_AddUint32(pMsg, "batteryMv",
					(u32_t)pEntry->ad.data);
		break;
	case SENSOR_EVENT_ADV_ON_BUTTON:
		ShadowBuilder_AddTrue(pMsg, "advOnButton");
		break;
	case SENSOR_EVENT_RESET:
		ShadowBuilder_AddTrue(pMsg, "reset");
		ShadowBuilder_AddUint32(pMsg, "resetReason",
					(u32_t)pEntry->ad.data);
		break;
	default:
		break;
	}
}

static void Bt510AddrToString(SensorTable_t *pEntry)
{
	int count = snprintk(pEntry->addrString, BT510_ADDR_STR_SIZE,
			     "%02X%02X%02X%02X%02X%02X", pEntry->ad.addr.val[5],
			     pEntry->ad.addr.val[4], pEntry->ad.addr.val[3],
			     pEntry->ad.addr.val[2], pEntry->ad.addr.val[1],
			     pEntry->ad.addr.val[0]);
	FRAMEWORK_ASSERT(count == BT510_ADDR_STR_LEN);
}

static void GatewayShadowMaker(void)
{
	if (BT510_USES_SINGLE_AWS_TOPIC) {
		return;
	}

	JsonMsg_t *pMsg = BufferPool_TryToTake(sizeof(JsonMsg_t));
	if (pMsg == NULL) {
		return;
	}

	pMsg->header.msgCode = FMC_BT510_GATEWAY_OUT;
	pMsg->header.rxId = FWK_ID_AWS;

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
	for (i = 0; i < BT510_SENSOR_TABLE_SIZE; i++) {
		SensorTable_t *p = &sensorTable[i];
		if (p->inUse) {
			ShadowBuilder_AddSensorTableArrayEntry(
				pMsg, p->addrString, p->rxEpoch, p->whitelisted);
		}
	}
	ShadowBuilder_EndArray(pMsg);
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_EndGroup(pMsg);
	ShadowBuilder_Finalize(pMsg);

	FRAMEWORK_MSG_SEND(pMsg);
}

static void Whitelist(const char *pAddrString, bool NextState)
{
	size_t i;
	for (i = 0; i < BT510_SENSOR_TABLE_SIZE; i++) {
		if (strncmp(sensorTable[i].addrString, pAddrString,
			    BT510_ADDR_STR_LEN) == 0) {
			sensorTable[i].whitelisted = NextState;
			return;
		}
	}
}
