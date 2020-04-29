/**
 * @file qrtc.h
 * @brief Quasi Real Time Clock that uses offset and system ticks.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __QRTC_H__
#define __QRTC_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Resets epoch to zero and sets epochWasSet to false.
 */
void Qrtc_Init(void);

/**
 * @brief Set the epoch
 *
 * @param Epoch in seconds from Jan 1, 1970
 *
 * @retval recomputed value for testing
 */
u32_t Qrtc_SetEpoch(u32_t Epoch);

/**
 * @brief Set the epoch using time structure.
 *
 * @param pTm is a pointer to a time structure
 * @param OffsetSeconds is the offset in seconds from UTC that the time structure
 * contains.
 *
 * @note The cellular modem provides local time and an offset and it is much
 * easier to add the offset to the epoch than to adjust the time structure.
 *
 * @retval epoch for testing
 */
u32_t Qrtc_SetEpochFromTm(struct tm *pTm, s32_t OffsetSeconds);

/**
 * @retval Seconds since Jan 1, 1970.
 */
u32_t Qrtc_GetEpoch(void);

/**
 * @retval true if the epoch has been set, otherwise false
 */
bool Qrtc_EpochWasSet(void);

#ifdef __cplusplus
}
#endif

#endif /* __QRTC_H__ */
