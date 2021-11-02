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

typedef struct Ad {
	size_t len;
	uint8_t data[CONFIG_SENSOR_MAX_AD_SIZE];
} Ad_t;

typedef struct AdvMsg {
	FwkMsgHeader_t header;
	bt_addr_le_t addr;
	int8_t rssi;
	uint8_t type;
	Ad_t ad;
} AdvMsg_t;
CHECK_FWK_MSG_SIZE(AdvMsg_t);

typedef struct ESSSensorMsg {
	FwkMsgHeader_t header;
	float temperatureC; /* xx.xxC format */
	float humidityPercent; /* xx.xx% format */
	float pressurePa; /* x.xPa format */
} ESSSensorMsg_t;

#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_MSG_TYPES_H__ */
