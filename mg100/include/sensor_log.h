/**
 * @file sensor_log.h
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SENSOR_LOG_H__
#define __SENSOR_LOG_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#include "FrameworkIncludes.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
typedef struct SensorLogEvent {
	u32_t epoch;
	u16_t data;
	u8_t recordType;
	u8_t idLsb;
} SensorLogEvent_t;

typedef struct SensorLog SensorLog_t;

/* ["1234",4294967295,"1234"] */
#define SENSOR_LOG_ENTRY_JSON_STR_SIZE 26

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Allocates a sensor log object from the heap.
 *
 * @retval pointer to object
 */
SensorLog_t *SensorLog_Allocate(size_t Size);

/**
 * @brief Free object (return memory to system heap).
 * @note Sensor log contains a pointer to an allocated buffer.
 */
void SensorLog_Free(SensorLog_t *pLog);

/**
 * @brief Add element to sensor log.  If log is full, then first element
 * is overwritten.
 */
void SensorLog_Add(SensorLog_t *pLog, SensorLogEvent_t *pEvent);

/**
 * @brief Add sensor log to JSON message.
 */
void SensorLog_GenerateJson(SensorLog_t *pLog, JsonMsg_t *pMsg);

/**
 * @brief Get the maximum number of entries in the log.
 */
size_t SensorLog_GetSize(SensorLog_t *pLog);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_LOG_H__ */
