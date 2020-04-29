/**
 * @file FrameworkMsgConfiguration.h
 * @brief Defines the base message type used by the Framework.
 * Project specific message types should not be defined here.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __FRAMEWORK_MSG_CONFIGURATION_H__
#define __FRAMEWORK_MSG_CONFIGURATION_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>

#include "FrameworkIds.h"
#include "FrameworkMsgCodes.h"

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
typedef struct FwkMsgHeader {
	FwkMsgCode_t msgCode;
	FwkId_t rxId;
	FwkId_t txId;
	u8_t payloadByte; /* for alignment */
} FwkMsgHeader_t;
BUILD_ASSERT_MSG(sizeof(FwkMsgHeader_t) == 4, "Unexpected Header Size");

typedef struct FwkMsg {
	FwkMsgHeader_t header;
	/* Optional Payload Below */
} FwkMsg_t;

#define FWK_QUEUE_ENTRY_SIZE (sizeof(FwkMsg_t *))
#define FWK_QUEUE_ALIGNMENT 4 /* bytes */

#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_MSG_CONFIGURATION_H__ */
