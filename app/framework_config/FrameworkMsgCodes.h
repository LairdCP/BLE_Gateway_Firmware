/**
 * @file FrameworkMsgCodes.h
 * @brief Defines the couple of messages reserved by Framework.
 * Application message types can also be defined here.
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __FRAMEWORK_MSG_CODES_H__
#define __FRAMEWORK_MSG_CODES_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "Framework.h"

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
enum FwkMsgCodeEnum {
	/* The first application specific message should be assigned the value
	 * FMC_APPLICATION_SPECIFIC_START.  There are codes reserved by framework.
	 */
	FMC_ADV = FMC_APPLICATION_SPECIFIC_START,
	FMC_SENSOR_PUBLISH,
	FMC_ESS_SENSOR_EVENT,
	FMC_GATEWAY_OUT,
	FMC_SENSOR_TICK,
	FMC_GREENLIST_REQUEST,
	FMC_CONFIG_REQUEST,
	FMC_CONNECT_REQUEST,
	FMC_START_DISCOVERY,
	FMC_DISCOVERY_COMPLETE,
	FMC_DISCOVERY_FAILED,
	FMC_RESPONSE,
	FMC_DISCONNECT,
	FMC_SEND_RESET,
	FMC_SUBSCRIBE,
	FMC_SUBSCRIBE_ACK,
	FMC_SENSOR_SHADOW_INIT,
	FMC_AWS_HEARTBEAT,
	FMC_AWS_DECOMMISSION,

	FMC_AWS_CONNECTED,
	FMC_AWS_DISCONNECTED,
	FMC_AWS_GET_ACCEPTED_RECEIVED,

	FMC_FOTA_START_REQ,
	FMC_FOTA_START_ACK,
	FMC_FOTA_DONE,

	FMC_BLUEGRASS_READY,
	FMC_NETWORK_CONNECTED,
	FMC_NETWORK_DISCONNECTED,
	FMC_CLOUD_CONNECTED,
	FMC_CLOUD_DISCONNECTED,
	FMC_CLOUD_REBOOT,

	/* Last value (DO NOT DELETE) */
	NUMBER_OF_FRAMEWORK_MSG_CODES
};
BUILD_ASSERT(sizeof(enum FwkMsgCodeEnum) <= sizeof(FwkMsgCode_t),
	     "Too many message codes");

#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_MSG_CODES_H__ */
