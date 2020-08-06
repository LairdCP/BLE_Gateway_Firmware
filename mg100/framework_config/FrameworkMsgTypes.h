/**
 * @file FrameworkMessageTypes.h
 * @brief Project specific message types are defined here.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __FRAMEWORK_MSG_TYPES_H__
#define __FRAMEWORK_MSG_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/bluetooth.h>

#include "Framework.h"

/******************************************************************************/
/* Project Specific Message Types                                             */
/******************************************************************************/
typedef struct JsonMsg {
	FwkMsgHeader_t header;
	size_t size; /** number of bytes */
	size_t length; /** of the data */
	char topic[CONFIG_AWS_TOPIC_MAX_SIZE];
	char buffer[];
} JsonMsg_t;

/* Zephyr currently doesn't support extended advertisements. */
#define MAX_AD_SIZE 31

typedef struct Ad {
	size_t len;
	uint8_t data[MAX_AD_SIZE];
} Ad_t;

typedef struct AdvMsg {
	FwkMsgHeader_t header;
	bt_addr_le_t addr;
	int8_t rssi;
	uint8_t type;
	Ad_t ad;
} AdvMsg_t;
CHECK_FWK_MSG_SIZE(AdvMsg_t);

typedef struct BL654SensorMsg {
	FwkMsgHeader_t header;
	float temperatureC; /* xx.xxC format */
	float humidityPercent; /* xx.xx% format */
	float pressurePa; /* x.xPa format */
} BL654SensorMsg_t;

#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_MSG_TYPES_H__ */
