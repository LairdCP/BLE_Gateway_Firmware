/**
 * @file lcz_fs_mgmt_intercept.h
 * @brief Intercept functions called during SMP file system command processing.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __CT_FS_INTERCEPT_H__
#define __CT_FS_INTERCEPT_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
typedef struct fs_mgmt_ctxt {
	/** Whether an upload is currently in progress. */
	bool uploading;

	/** Expected offset of next upload request. */
	size_t off;

	/** Total length of file currently being uploaded. */
	size_t len;

	/** Pointer to file data being uploaded */
	uint8_t *file_data;

	/** Size of data chunk being sent */
	uint32_t data_len;
} fs_mgmt_ctxt_t;

#define CT_FS_INTERCEPT_NV_PATH "/nv/"
#define CT_FS_INTERCEPT_TEST_PUB_PATH "/sys/testpub.cmd"

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Intercepts writes to NV
 *
 * @retval 0 on success
 */
int ct_fs_intercept_nv(char *path, fs_mgmt_ctxt_t *fs_mgmt_ctxt);

/**
 * @brief Intercepts writes that are used to cause a test publish to AWS.
 *
 * @retval 0 on success
 */
int ct_fs_intercept_test_publish(void);

#ifdef __cplusplus
}
#endif

#endif /* __CT_FS_INTERCEPT_H__ */
