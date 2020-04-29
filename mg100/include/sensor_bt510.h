/**
 * @file sensor_bt510.h
 * @brief Functions for parsing advertisements from BT510 sensor.
 *
 * Once configured the BT510 sends all state information in advertisements.
 * This allows connectionless operation.
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SENSOR_BT510_H__
#define __SENSOR_BT510_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stdbool.h>
#include <bluetooth/bluetooth.h>

#include "Framework.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define BT510_SENSOR_TABLE_SIZE 15

/* PSM mode doesn't easily support receiving information from cloud.
 * When using a single topic BT510 temperature data is sent to the
 * Pinnacle (gateway) topic for any sensor in the table.
 *
 * When not using a single topic each sensor must be whitelisted before
 * it is allowed to send data to the cloud.
 */
#define BT510_USES_SINGLE_AWS_TOPIC 1

#define BT510_ADDR_STR_SIZE 13
#define BT510_ADDR_STR_LEN (BT510_ADDR_STR_SIZE - 1)

#define BT510_SENSOR_NAME_MAX_SIZE 12
#define BT510_SENSOR_NAME_MAX_STR_LEN (BT510_SENSOR_NAME_MAX_SIZE - 1)

/* Format of the Manufacturer Specific Data using 1M PHY in Advertisement */
struct Bt510AdEventTag {
	u16_t companyId;
	u16_t protocolId;
	u16_t networkId;
	u16_t flags;
	bt_addr_t addr;
	u8_t recordType;
	u16_t id;
	u32_t epoch;
	u16_t data;
	u16_t dataReserved;
	u8_t resetCount;
} __packed;
typedef struct Bt510AdEventTag Bt510AdEvent_t;

/* Format of the  Manufacturer Specific Data using 1M PHY in Scan Response */
struct Bt510RspTag {
	u16_t companyId;
	u16_t protocolId;
	u16_t productId;
	u8_t firmwareVersionMajor;
	u8_t firmwareVersionMinor;
	u8_t firmwareVersionPatch;
	u8_t firmwareType;
	u8_t configVersion;
	u8_t bootloaderVersionMajor;
	u8_t bootloaderVersionMinor;
	u8_t bootloaderVersionPatch;
	u8_t hardwareMinorVersion;
} __packed;
typedef struct Bt510RspTag Bt510Rsp_t;

typedef enum MAGNET_STATE { MAGNET_NEAR = 0, MAGNET_FAR } MagnetState_t;

typedef enum SENSOR_EVENT {
	SENSOR_EVENT_RESERVED = 0,
	SENSOR_EVENT_TEMPERATURE = 1,
	SENSOR_EVENT_MAGNET = 2, /* or proximity */
	SENSOR_EVENT_MOVEMENT = 3,
	SENSOR_EVENT_ALARM_HIGH_TEMP_1 = 4,
	SENSOR_EVENT_ALARM_HIGH_TEMP_2 = 5,
	SENSOR_EVENT_ALARM_HIGH_TEMP_CLEAR = 6,
	SENSOR_EVENT_ALARM_LOW_TEMP_1 = 7,
	SENSOR_EVENT_ALARM_LOW_TEMP_2 = 8,
	SENSOR_EVENT_ALARM_LOW_TEMP_CLEAR = 9,
	SENSOR_EVENT_ALARM_DELTA_TEMP = 10,
	SENSOR_EVENT_ALARM_TEMPERATURE_RATE_OF_CHANGE = 11,
	SENSOR_EVENT_BATTERY_GOOD = 12,
	SENSOR_EVENT_ADV_ON_BUTTON = 13,
	SENSOR_EVENT_RESERVED_14 = 14,
	SENSOR_EVENT_IMPACT = 15,
	SENSOR_EVENT_BATTERY_BAD = 16,
	SENSOR_EVENT_RESET = 17,

	NUMBER_OF_SENSOR_EVENTS
} SensorEventType_t;
BUILD_ASSERT_MSG(sizeof(SensorEventType_t) <= sizeof(u8_t),
		 "Sensor Event enum too large");

struct sensor {
	char addrString[BT510_ADDR_STR_SIZE];
	bool whitelist;
};

typedef struct SensorWhitelistMsg {
	FwkMsgHeader_t header;
	struct sensor sensors[BT510_SENSOR_TABLE_SIZE];
	size_t sensorCount;
} SensorWhitelistMsg_t;
CHECK_FWK_MSG_SIZE(SensorWhitelistMsg_t);

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @note Functions must be called from the same thread.
 */

/**
 * @brief Initializes sensor table.
 */
void SensorBt510_Initialize(void);

/**
 * @brief Advertisement parser
 */
void SensorBt510_AdvertisementHandler(const bt_addr_le_t *pAddr, s8_t rssi,
				      u8_t type, Ad_t *pAd);

/**
 * @brief Only whitelisted sensors are allowed to send their data to the cloud.
 */
void SensorBt510_ProcessWhitelistRequest(SensorWhitelistMsg_t *pMsg);

/**
 * @brief Generate BT510 portion of the shadow if the sensor table has changed.
 */
void SensorBt510_GenerateGatewayShadow(void);

/**
 * @retval number of sensors in table.
 */
size_t SensorBt510_Count(void);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_BT510_H__ */
