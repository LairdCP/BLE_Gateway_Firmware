/**
 * @file sensor_task.h
 * @brief Message Framework task for handling sensor advertisements.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SENSOR_TASK_H__
#define __SENSOR_TASK_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Creates and registers framework task.
 */
void SensorTask_Initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_TASK_H__ */
