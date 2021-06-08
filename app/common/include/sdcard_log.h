/**
 * @file sdcard_log.h
 * @brief This file implements a framework for logging data to the sdcard.
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SDCARD_LOG_H__
#define __SDCARD_LOG_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#if defined(CONFIG_SCAN_FOR_BT510) || defined(CONFIG_BL654_SENSOR)
#include "FrameworkIncludes.h"
#include "lcz_sensor_adv_format.h"
#endif

#ifdef CONFIG_CONTACT_TRACING
#include "rpc_params.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define SDCARD_LOG_DEFAULT_MAX_LENGTH 32

#ifdef CONFIG_CONTACT_TRACING
typedef struct log_get_state_s {
	rpc_params_log_get_t rpc_params;
	uint32_t cur_seek;
	uint32_t bytes_remaining;
	uint32_t bytes_ready;
} log_get_state_t;
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
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
 * @param void * data - the pointer to the data to write,
 * int length - the length of the data.
 *
 * @retval int - Write status - Values < 0 are errors, 0 = success.
 */
int sdCardLogBatteryData(void *data, int length);

#ifdef CONFIG_SCAN_FOR_BT510
/**
 * @brief this function writes data to the log. It will append data to
 *    the end of the file until the log limit is reached. when
 *    the limit is reached, log data will overwrite data starting
 *    at the beginning of the file. if there isn't enough free space
 *    to allow for the full length it will also start overwriting at the
 *    beginning of the file.
 *
 * @param void * data - the pointer to the data to write,
 * int length - the length of the data.
 *
 * @retval int - Write status - Values < 0 are errors, 0 = success.
 */
int sdCardLogAdEvent(LczSensorAdEvent_t *event);
#endif

#ifdef CONFIG_BL654_SENSOR
/**
 * @brief this function writes data to the log. It will append data to
 *    the end of the file until the log limit is reached. when
 *    the limit is reached, log data will overwrite data starting
 *    at the beginning of the file. if there isn't enough free space
 *    to allow for the full length it will also start overwriting at the
 *    beginning of the file.
 *
 * @param void * data - the pointer to the data to write,
 * int length - the length of the data.
 *
 * @retval int - Write status - Values < 0 are errors, 0 = success.
 */
int sdCardLogBL654Data(BL654SensorMsg_t *msg);
#endif
/**
 * @brief this function is called by the gateway JSON parser to set
 * the maximum log size.
 *
 * @param int Value - the maximum log size in MB.
 *
 * @retval negative error code, 0 on success
 */
int sdCardLogUpdateMaxSize(int Value);

/**
 * @brief this function is called by the gateway JSON parser to get
 * the maximum log size.
 *
 * @param none
 *
 * @retval int Value - the maximum log size in in MB.
 */
int sdCardLogGetMaxSize(void);

/**
 * @brief this function is called by the gateway JSON parser
 * to get the current log size.
 *
 * @param none
 *
 * @retval int Value - the current log size in MB.
 */
int sdCardLogGetSize(void);

/**
 * @brief this function is called by the gateway JSON parser
 * to get the free space on the SD card.
 *
 * @param none
 *
 * @retval int Value - the freespace in MB.
 */
int sdCardLogGetFree(void);

/**
 * @brief list the contents of a directory on the SD card
 *
 * @param path path to list, e.g. "/SD:/"
 *
 * @retval int zero on success
 */
int sdCardLsDir(const char *path);

#ifdef CONFIG_CONTACT_TRACING
int sdCardLogCleanup(void);

int sdCardLogLsDirToString(const char *path, char *buf, int maxlen);

int sdCardLogGet(char *pbuf, log_get_state_t *lstate, uint32_t maxlen);

void sdCardLogTest(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __SDCARD_LOG_H__ */
