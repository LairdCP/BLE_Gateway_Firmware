/**
 * @file sdcard_log.h
 * @brief this file implements a framework for logging data to the sdcard.
 * 
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SDCARD_LOG_H__
#define __SDCARD_LOG_H__

/* (Remove Empty Sections) */
/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>
#include "FrameworkIncludes.h"
#ifdef CONFIG_BLUEGRASS
# include "sensor_adv_format.h"
# include "sensor_log.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define SDCARD_LOG_DEFAULT_MAX_LENGTH	32

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
struct sdcard_status {
	int currLogSize;
	int maxLogSize;
	int freeSpace;
};
/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief this function initializes the sdcard logging sub-system.
 *
 * @param none
 *
 * @retval struct sdcard_status * - returns the sdcard shadow parameter data.
 */
struct sdcard_status *sdCardLogGetStatus(void);

/**
 * @brief this function initializes the sdcard logging sub-system.
 *
 * @param none
 *
 * @retval int - Card status: 1 = card present, 0 = card not present
 */
int sdCardLogInit(void);

/**
 * @brief this function writes data to the log. It will append data to
 *    the end of the file until the log limit is reached. when
 *    the limit is reached, log data will overwrite data starting
 *    at the beginning of the file. if there isn't enough free space
 *    to allow for the full length it will also start overwriting at the
 *    beginning of the file.
 *
 * @param void * data - the pointer to the data to write, int length - the length of the data.
 *
 * @retval int - Write status - Values < 0 are errors, 0 = success.
 */
int sdCardLogBatteryData(void * data, int length);

#ifdef CONFIG_BLUEGRASS
/**
 * @brief this function writes data to the log. It will append data to
 *    the end of the file until the log limit is reached. when
 *    the limit is reached, log data will overwrite data starting
 *    at the beginning of the file. if there isn't enough free space
 *    to allow for the full length it will also start overwriting at the
 *    beginning of the file.
 *
 * @param void * data - the pointer to the data to write, int length - the length of the data.
 *
 * @retval int - Write status - Values < 0 are errors, 0 = success.
 */
int sdCardLogAdEvent(Bt510AdEvent_t * event);

/**
 * @brief this function writes data to the log. It will append data to
 *    the end of the file until the log limit is reached. when
 *    the limit is reached, log data will overwrite data starting
 *    at the beginning of the file. if there isn't enough free space
 *    to allow for the full length it will also start overwriting at the
 *    beginning of the file.
 *
 * @param void * data - the pointer to the data to write, int length - the length of the data.
 *
 * @retval int - Write status - Values < 0 are errors, 0 = success.
 */
int sdCardLogBL654Data(BL654SensorMsg_t * msg);
#endif
/**
 * @brief this function is called by the gateway JSON parser to set the maximum log size.
 *
 * @param int Value - the maximum log size in MB.
 *
 * @retval none
 */
bool UpdateMaxLogSize(int Value);

/**
 * @brief this function is called by the gateway JSON parser to get the maximum log size.
 *
 * @param none
 *
 * @retval int Value - the maximum log size in in MB.
 */
int GetMaxLogSize();

/**
 * @brief this function is called by the gateway JSON parser to get the current log size.
 *
 * @param none
 *
 * @retval int Value - the current log size in MB.
 */
int GetLogSize();

/**
 * @brief this function is called by the gateway JSON parser to get the sdcard's free spce.
 *
 * @param none
 *
 * @retval int Value - the freespace in MB.
 */
int GetFSFree();

#ifdef __cplusplus
}
#endif

#endif /* __SDCARD_LOG_H__ */
