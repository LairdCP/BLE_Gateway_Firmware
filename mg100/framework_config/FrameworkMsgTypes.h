//=============================================================================
//! @file FrameworkMessageTypes.h
//!
//! @brief Project specific message types are defined here.
//!
//! @copyright Copyright 2020 Laird
//!            All Rights Reserved.
//=============================================================================

#ifndef FRAMEWORK_MSG_TYPES_H
#define FRAMEWORK_MSG_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Includes
//=============================================================================
#include <bluetooth/bluetooth.h>
#include "FrameworkMsgConfiguration.h"

//=============================================================================
// Global Constants, Macros and Type Definitions
//=============================================================================

//
// The size of the buffer pool messages limits framework message sizes.
//
#ifndef BUFFER_POOL_MINSZ
#define BUFFER_POOL_MINSZ 4
#endif

#ifndef BUFFER_POOL_MAXSZ
#define BUFFER_POOL_MAXSZ 4096
#endif

#ifndef BUFFER_POOL_NMAX
#define BUFFER_POOL_NMAX 8
#endif

#ifndef BUFFER_POOL_ALIGN
#define BUFFER_POOL_ALIGN 4
#endif

#define CHECK_FWK_MSG_SIZE(x)                                                  \
	BUILD_ASSERT_MSG(sizeof(x) <= BUFFER_POOL_MAXSZ,                       \
			 "Buffer Pool Max Message size is too small")

//=============================================================================
// Project Specific Message Types
//=============================================================================

#define JSON_OUT_BUFFER_SIZE (640)
#define JSON_IN_BUFFER_SIZE (3072)
#define TOPIC_MAX_SIZE (64)

typedef struct JsonMsg {
	FwkMsgHeader_t header;
	size_t size;
	char buffer[JSON_OUT_BUFFER_SIZE];
	char topic[TOPIC_MAX_SIZE];
} JsonMsg_t;
CHECK_FWK_MSG_SIZE(JsonMsg_t);

typedef struct JsonGatewayInMsg {
	FwkMsgHeader_t header;
	size_t size;
	char buffer[JSON_IN_BUFFER_SIZE];
} JsonGatewayInMsg_t;
CHECK_FWK_MSG_SIZE(JsonGatewayInMsg_t);

// Zephyr currently doesn't support extended advertisements.
#define MAX_AD_SIZE 31

typedef struct Ad {
	size_t len;
	u8_t data[MAX_AD_SIZE];
} Ad_t;

typedef struct AdvMsg {
	FwkMsgHeader_t header;
	bt_addr_le_t addr;
	s8_t rssi;
	u8_t type;
	Ad_t ad;
} AdvMsg_t;
CHECK_FWK_MSG_SIZE(AdvMsg_t);

typedef struct BL654SensorMsg {
	FwkMsgHeader_t header;
	float temperatureC; // xx.xxC format
	float humidityPercent; // xx.xx% format
	float pressurePa; // x.xPa format
} BL654SensorMsg_t;

//=============================================================================
// Global Data Definitions
//=============================================================================

//=============================================================================
// Global Function Prototypes
//=============================================================================

#ifdef __cplusplus
}
#endif

#endif

// end
