/**
 * @file http_file_download.h
 * @brief HTTP file download (hfd) using download client
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __HTTP_FILE_DOWNLOAD_H__
#define __HTTP_FILE_DOWNLOAD_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <net/fota_download.h>

#include "file_system_utilities.h"

/******************************************************************************/
/* Constant, Macro and Type Definitions                                       */
/******************************************************************************/
struct hfd_context {
	int device_type;
	char device_list[CONFIG_HFD_DEVICE_LIST_MAX_STR_SIZE];
	char host[CONFIG_DOWNLOAD_CLIENT_MAX_HOSTNAME_SIZE];
	char host_file_name[CONFIG_DOWNLOAD_CLIENT_MAX_FILENAME_SIZE];
	size_t host_file_size;
	size_t offset;
	int sec_tag;
	char abs_file_name[FSU_MAX_ABS_PATH_SIZE];
	uint8_t expected_hash[FSU_HASH_SIZE];
   	uint8_t file_hash[FSU_HASH_SIZE];
};

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Downloads a file and blocks until finished.
 * Caller is resonsible for loading Root CA.
 * Caller is responsible for limiting number of active connections.
 *
 * @param context pointer to file context
 * @return int 0 on success, else negative error code
 */
int http_file_download(struct hfd_context *context);

/**
 * @brief Check if expected hash matches computed file hash.
 *
 * @param context pointer to file context (file hash modified by this function)
 * @return int negative error code on failure, size of file on success
 */
int http_file_download_valid_hash(struct hfd_context *context);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_FILE_DOWNLOAD_H__ */