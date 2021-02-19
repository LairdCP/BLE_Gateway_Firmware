/**
 * @file sensor_cmd.h
 * @brief JSON command strings for Laird BT sensors.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SENSOR_CMD_H__
#define __SENSOR_CMD_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define BT510_RESET_ACK_TO_DUMP_DELAY_TICKS K_SECONDS(10)
#define BT510_WRITE_TO_RESET_DELAY_TICKS K_MSEC(1500)

/* prefix + AWS string + suffix => JSON command that can be sent to sensor */
extern const char SENSOR_CMD_SET_PREFIX[];
extern const char SENSOR_CMD_SUFFIX[];
extern const char SENSOR_CMD_DUMP[];
extern const char SENSOR_CMD_REBOOT[];
extern const char SENSOR_CMD_ACCEPTED_SUB_STR[];
extern const char SENSOR_CMD_DEFAULT_QUERY[];
extern const char SENSOR_CMD_SET_CONFIG_VERSION_1[];
extern const char SENSOR_CMD_SET_EPOCH_FMT_STR[];

#define SENSOR_CMD_MAX_EPOCH_SIZE 10

#define BT510_MAJOR_VERSION_RESET_NOT_REQUIRED 4

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief Returns true if the sensor configuration requires a reset
 */
bool SensorCmd_RequiresReset(char *pCmd);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_CMD_H__ */
