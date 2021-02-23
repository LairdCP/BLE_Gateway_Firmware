/**
 * @file http_fota_task.h
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __HTTP_FOTA_TASK_H__
#define __HTTP_FOTA_TASK_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief Creates and registers framework task.
 */
int http_fota_task_initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_FOTA_TASK_H__ */
