/**
 * @file hl7800_http_fota.h
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __HL7800_HTTP_FOTA_H__
#define __HL7800_HTTP_FOTA_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stddef.h>
#include <zephyr.h>

#include "http_fota_task.h"

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Initializes the download client for the hl7800 image
 *
 * @param client_callback pointer to the function to execute for updates
 *
 * @retval a negative value for an error, 0 for success
 */
int hl7800_download_client_init(fota_download_callback_t client_callback);

/**
 * @brief Starts downloading the hl7800 image from the stored offset
 *
 * @param pCtx pointer to the fota context
 * @param host pointer to the host string
 * @param file pointer to the file name string
 * @param sec_tag security tag for the connection
 * @param apn apn for the connection
 * @param fragment_size fragment size for the connection
 *
 * @retval a negative value for an error, 0 for success
 */
int hl7800_download_start(fota_context_t *pCtx, const char *host,
            const char *file, int sec_tag, const char *apn,
            size_t fragment_size);

/**
 * @brief Initiates the modem update from a downloaded image
 *
 * @param pCtx pointer to the fota context
 *
 * @retval a negative value for an error, 0 for success
 */
int hl7800_initiate_modem_update(fota_context_t *pCtx);

#ifdef __cplusplus
}
#endif

#endif /* __HL7800_HTTP_FOTA_H__ */