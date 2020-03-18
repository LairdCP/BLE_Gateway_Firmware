//=============================================================================
//! @file FrameworkMsgConfiguration.h
//!
//! @brief Defines the base message type used by the Framework.
//! Project specific message types should not be defined here.
//!
//! @copyright Copyright 2020 Laird
//!            All Rights Reserved.
//=============================================================================

#ifndef FRAMEWORK_MSG_CONFIGURATION_H
#define FRAMEWORK_MSG_CONFIGURATION_H

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Includes
//=============================================================================
#include <zephyr/types.h>

#include "FrameworkIds.h"
#include "FrameworkMsgCodes.h"

//=============================================================================
// Global Constants, Macros and Type Definitions
//=============================================================================

typedef struct FwkMsgHeader {
	FwkMsgCode_t msgCode;
	FwkId_t rxId;
	FwkId_t txId;
	u8_t payloadByte; // for alignment
} FwkMsgHeader_t;
BUILD_ASSERT_MSG(sizeof(FwkMsgHeader_t) == 4, "Unexpected Header Size");

typedef struct FwkMsg {
	FwkMsgHeader_t header;
	// Optional Payload Below
} FwkMsg_t;

#define FWK_QUEUE_ENTRY_SIZE (sizeof(FwkMsg_t *))
#define FWK_QUEUE_ALIGNMENT 4 // bytes

//=============================================================================
// Global Data Definitions
//=============================================================================
// NA

//=============================================================================
// Global Function Prototypes
//=============================================================================
// NA

#ifdef __cplusplus
}
#endif

#endif

// end
