/**
 * @file coap_fota_task.h
 * @brief This task controls firmware update over the air (LTE) using the CoAP
 * bridge.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __COAP_FOTA_TASK_H__
#define __COAP_FOTA_TASK_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Creates and registers framework task.
 */
int coap_fota_task_initialize(void);

#ifdef __cplusplus
}
#endif

#endif
