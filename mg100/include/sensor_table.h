/**
 * @file sensor_table.h
 * @brief Functions for parsing advertisements from BT510 sensor.
 *
 * Once configured the BT510 sends all state information in advertisements.
 * This allows connectionless operation.
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SENSOR_TABLE_H__
#define __SENSOR_TABLE_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stdbool.h>
#include <bluetooth/bluetooth.h>

#include "sensor_adv_format.h"
#include "sensor_log.h"
#include "FrameworkIncludes.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
typedef struct SensorWhitelist {
	char addrString[SENSOR_ADDR_STR_SIZE];
	bool whitelist;
} SensorWhitelist_t;

typedef struct SensorWhitelistMsg {
	FwkMsgHeader_t header;
	SensorWhitelist_t sensors[CONFIG_SENSOR_TABLE_SIZE];
	size_t sensorCount;
} SensorWhitelistMsg_t;
CHECK_FWK_MSG_SIZE(SensorWhitelistMsg_t);

typedef struct SensorShadowInitMsg {
	FwkMsgHeader_t header;
	char addrString[SENSOR_ADDR_STR_SIZE];
	SensorLogEvent_t events[CONFIG_SENSOR_LOG_MAX_SIZE];
	size_t eventCount;
} SensorShadowInitMsg_t;
CHECK_FWK_MSG_SIZE(SensorShadowInitMsg_t);

/* The same message is used for subscription request and acknowledgement */
typedef struct SubscribeMsg {
	FwkMsgHeader_t header;
	bool subscribe;
	bool success; /* used for ack only */
	size_t tableIndex;
	size_t length;
	char topic[CONFIG_TOPIC_MAX_SIZE];
} SubscribeMsg_t;

typedef struct SensorCmdMsg {
	FwkMsgHeader_t header;
	u32_t attempts;
	bt_addr_le_t addr;
	s64_t dispatchTime;
	bool dumpRequest;
	bool resetRequest;
	bool setEpochRequest;
	u32_t configVersion;
	u32_t passkey;
	char name[SENSOR_NAME_MAX_SIZE];
	char addrString[SENSOR_ADDR_STR_SIZE];
	size_t tableIndex;
	size_t size; /** number of bytes */
	size_t length; /** of the data */
	char cmd[]; /** JSON string */
} SensorCmdMsg_t;

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @note Functions must be called from the same thread.
 */

/**
 * @brief Initializes sensor table.
 */
void SensorTable_Initialize(void);

/**
 * @brief Advertisement parser
 */
void SensorTable_AdvertisementHandler(const bt_addr_le_t *pAddr, s8_t rssi,
				      u8_t type, Ad_t *pAd);

/**
 * @brief Only whitelisted sensors are allowed to send their data to the cloud.
 */
void SensorTable_ProcessWhitelistRequest(SensorWhitelistMsg_t *pMsg);

/**
 * @brief Whitlisted sensors can subscribe to receive config data from AWs.
 *
 * @note This also handles un-subscription.
 */
void SensorTable_SubscriptionHandler(void);

/**
 * @brief Update subscription status in sensor table,
 * If sensor has been seen, then update shadow.
 */
void SensorTable_SubscriptionAckHandler(SubscribeMsg_t *pMsg);

/**
 * @brief Add configuration information to the sensor table for processing at
 * a later time (the next time the sensor is seen).
 *
 * @retval DISPATCH_DO_NOT_FREE if the sensor was found
 * @retval DISPATCH_OK if the sensor was found but config version is the same
 * @retval DISPATCH_ERROR if the sensor was not found
 */
DispatchResult_t SensorTable_AddConfigRequest(SensorCmdMsg_t *pMsg);

/**
 * @brief Put the config request back into the sensor table (because it
 * couldn't be processed).
 *
 * @retval DISPATCH_DO_NOT_FREE because the message is now the responsibility
 * of the sensor table and should not be freed.
 */
DispatchResult_t SensorTable_RetryConfigRequest(SensorCmdMsg_t *pMsg);

/**
 * @brief Inform the sensor table that a config request has completed.  It
 * will generate a dump request if the previous request came from AWS.
 */
void SensorTable_AckConfigRequest(SensorCmdMsg_t *pMsg);

/**
 * @brief Format and forward dump response to AWS.
 */
void SensorTable_CreateShadowFromDumpResponse(FwkBufMsg_t *pRsp,
					      const char *pAddrStr);

/**
 * @brief Generate sensor portion of the shadow if
 * the sensor table has changed.
 */
void SensorTable_GenerateGatewayShadow(void);

/**
 * @brief Inform the sensor task that its last gateway shadow
 * generation has reached AWS.
 */
void SensorTable_GatewayShadowAck(void);

/**
 * @brief When disconnected from AWS all sensors must have their state set
 * to unsubscribed.
 */
void SensorTable_UnsubscribeAll(void);

/**
 * @brief After reset or disconnect read shadow to re-populate event log.
 *
 * @note This also handles un-subscription.
 */
void SensorTable_GetAcceptedSubscriptionHandler(void);

/**
 * @brief Request sensor shadow until it is received.
 */
void SensorTable_InitShadowHandler(void);

/**
 * @brief After publishing a message to get accepted, then sensor table
 * can be repopulated with what is in the shadow.
 */
void SensorTable_ProcessShadowInitMsg(SensorShadowInitMsg_t *pMsg);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_TABLE_H__ */
