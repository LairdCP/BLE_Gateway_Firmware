/**
 * @file control_task.h
 * @brief The control task uses the main thread.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __CONTROL_TASK_H__
#define __CONTROL_TASK_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Initialize control task and all other application tasks.
 */
void control_task_initialize(void);

/**
 * @brief Run main/control thread.
 */
void control_task_thread(void);

#ifdef __cplusplus
}
#endif

#endif /* __CONTROL_TASK_H__ */
