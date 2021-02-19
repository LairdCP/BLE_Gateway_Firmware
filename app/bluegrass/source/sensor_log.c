/**
 * @file sensor_log.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_INF
LOG_MODULE_REGISTER(sensor_log);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "FrameworkIncludes.h"
#include "shadow_builder.h"
#include "sensor_log.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
struct SensorLog {
	size_t size;
	size_t writeIndex;
	bool wrapped;
	SensorLogEvent_t *pData;
};

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static size_t GetNumberOfEntries(SensorLog_t *pLog);
static void IncrementIndices(SensorLog_t *pLog);
static void IncrementIndex(size_t *pIndex, size_t Max);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
SensorLog_t *SensorLog_Allocate(size_t Size)
{
	SensorLog_t *p = NULL;
	if (Size > 0) {
		size_t s = sizeof(SensorLog_t);
		p = k_malloc(s);
		p->writeIndex = 0;
		p->wrapped = false;
		p->size = Size;
		size_t bytes = Size * sizeof(SensorLogEvent_t);
		p->pData = k_malloc(bytes);
		memset(p->pData, 0, bytes);
	}
	return p;
}

void SensorLog_Free(SensorLog_t *pLog)
{
	k_free(pLog->pData);
	k_free(pLog);
}

void SensorLog_Add(SensorLog_t *pLog, SensorLogEvent_t *pEvent)
{
	if (pLog == NULL) {
		return;
	}

	memcpy(&pLog->pData[pLog->writeIndex], pEvent,
	       sizeof(SensorLogEvent_t));
	IncrementIndices(pLog);
}

void SensorLog_GenerateJson(SensorLog_t *pLog, JsonMsg_t *pMsg)
{
	if (pLog == NULL) {
		return;
	}

	size_t entries = GetNumberOfEntries(pLog);
	LOG_DBG("Sensor Log has %d entries", entries);
	if (entries == 0) {
		return;
	}

	ShadowBuilder_StartArray(pMsg, "eventLog");
	size_t readIndex = pLog->wrapped ? pLog->writeIndex : 0;
	size_t i;
	for (i = 0; i < entries; i++) {
		ShadowBuilder_AddEventLogEntry(pMsg, &pLog->pData[readIndex]);
		IncrementIndex(&readIndex, pLog->size);
	}
	ShadowBuilder_EndArray(pMsg);
}

size_t SensorLog_GetSize(SensorLog_t *pLog)
{
	return (pLog == NULL) ? 0 : pLog->size;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static size_t GetNumberOfEntries(SensorLog_t *pLog)
{
	return pLog->wrapped ? pLog->size : pLog->writeIndex;
}

static void IncrementIndices(SensorLog_t *pLog)
{
	if (pLog->writeIndex == (pLog->size - 1)) {
		pLog->wrapped = true;
	}
	IncrementIndex(&pLog->writeIndex, pLog->size);
}

static void IncrementIndex(size_t *pIndex, size_t Max)
{
	(*pIndex)++;
	if (*pIndex >= Max) {
		*pIndex = 0;
	}
}
