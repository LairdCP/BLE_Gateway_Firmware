/**
 * @file qrtc.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <stdlib.h>
#include "qrtc.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
struct qrtc {
	bool epochWasSet;
	u32_t offset;
};

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct qrtc qrtc;
K_MUTEX_DEFINE(qrtcMutex);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void UpdateOffset(u32_t Epoch);
static u32_t GetUptimeSeconds(void);
static u32_t ConvertTimeToEpoch(time_t Time);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void Qrtc_Init(void)
{
	qrtc.offset = 0;
	qrtc.epochWasSet = false;
}

u32_t Qrtc_SetEpoch(u32_t Epoch)
{
	UpdateOffset(Epoch);
	return Qrtc_GetEpoch();
}

u32_t Qrtc_SetEpochFromTm(struct tm *pTm, s32_t OffsetSeconds)
{
	time_t rawTime = mktime(pTm);
	u32_t epoch = ConvertTimeToEpoch(rawTime);
	/* (local + offset) = UTC */
	if (abs(OffsetSeconds) < epoch) {
		epoch -= OffsetSeconds;
		UpdateOffset(epoch);
	}
	return Qrtc_GetEpoch();
}

u32_t Qrtc_GetEpoch(void)
{
	return (GetUptimeSeconds() + qrtc.offset);
}

bool Qrtc_EpochWasSet(void)
{
	return qrtc.epochWasSet;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
/**
 * @brief Generate and save an offset using the current uptime to
 * make a quasi-RTC because there isn't hardware support.
 */
static void UpdateOffset(u32_t Epoch)
{
	k_mutex_lock(&qrtcMutex, K_FOREVER);
	u32_t uptime = GetUptimeSeconds();
	if (Epoch >= uptime) {
		qrtc.offset = Epoch - uptime;
		qrtc.epochWasSet = true;
	}
	k_mutex_unlock(&qrtcMutex);
}

static u32_t GetUptimeSeconds(void)
{
	s64_t uptimeMs = k_uptime_get();
	if (uptimeMs < 0) {
		return 0;
	}
	return (u32_t)(uptimeMs / MSEC_PER_SEC);
}

static u32_t ConvertTimeToEpoch(time_t Time)
{
	// Time is a long long int in Zephyr.
	if (Time < 0) {
		return 0;
	} else if (Time >= UINT32_MAX) {
		return 0;
	} else {
		return (u32_t)Time;
	}
}
