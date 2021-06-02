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
/* Includes                                                                   */
/******************************************************************************/
#include <stddef.h>
#include <zephyr.h>

#include "http_fota_shadow.h"

/******************************************************************************/
/* Global Constant, Macro and Type Definitions                                 */
/******************************************************************************/
typedef enum fota_fsm_state_t {
	FOTA_FSM_ABORT = -2,
	FOTA_FSM_ERROR = -1,
	FOTA_FSM_IDLE = 0,
	FOTA_FSM_END,
	FOTA_FSM_SUCCESS,
	FOTA_FSM_MODEM_WAIT,
	FOTA_FSM_WAIT,
	FOTA_FSM_START,
	FOTA_FSM_START_DOWNLOAD,
	FOTA_FSM_WAIT_FOR_DOWNLOAD_COMPLETE,
	FOTA_FSM_DELETE_EXISTING_FILE,
	FOTA_FSM_WAIT_FOR_SWITCHOVER,
	FOTA_FSM_INITIATE_UPDATE
} fota_fsm_state_t;

typedef struct fota_context {
	enum fota_image_type type;
	fota_fsm_state_t state;
	bool using_transport;
	uint32_t delay;
	char *file_path;
	struct k_sem *wait_download;
	bool download_error;
} fota_context_t;

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
