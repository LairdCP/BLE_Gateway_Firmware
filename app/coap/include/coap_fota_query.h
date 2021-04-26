/**
 * @file coap_fota_query.h
 * @brief Query/Context for requesting FOTA information from CoAP bridge.
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __COAP_FOTA_MSGS_H__
#define __COAP_FOTA_MSGS_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#include "file_system_utilities.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
/* Empty strings and negative numbers will not be added to CoAP query */
typedef struct coap_fota_query {
	int32_t dtls;
	uint16_t port;
	int32_t block_size;
	uint8_t domain[CONFIG_COAP_FOTA_MAX_PARAMETER_SIZE];
	const uint8_t *path;
	const uint8_t *product;
	const uint8_t *image;
	const uint8_t *fs_path;
	uint8_t version[CONFIG_FSU_MAX_VERSION_SIZE];
	uint8_t filename[CONFIG_FSU_MAX_FILE_NAME_SIZE];

	uint8_t computed_hash[FSU_HASH_SIZE];
	/* set by coap query hash function */
	uint8_t expected_hash[FSU_HASH_SIZE];
	/* set by filesystem query */
	int32_t offset;
	/* set by coap query size */
	int32_t size;

	/* not part of API - internal/housekeeping use only */
	bool block_xfer;
} coap_fota_query_t;

static inline bool coap_fota_resumed_download(coap_fota_query_t *q)
{
	if ((q->offset) != 0 && (q->offset != q->size)) {
		return true;
	} else {
		return false;
	}
}

/* Allow path to contain multiple pieces without using array of pointers */
#define COAP_FOTA_QUERY_URI_PATH_DELIMITER '/'

#ifdef __cplusplus
}
#endif

#endif /* __COAP_FOTA_MSGS_H__ */
