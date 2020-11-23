/**
 * @file coap_fota.h
 * @brief Communicates with the CoAP bridge to perform firmware updates
 * over the cellular connection.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __COAP_FOTA_H__
#define __COAP_FOTA_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#include "FrameworkIncludes.h"

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Initialize CoAP FOTA module.
 */
void coap_fota_init(void);

/**
 * @brief Get the size of a firmware image.
 *
 * @param p is a pointer to a query
 *
 * @retval negative error code, 0 on success
 * @note size is updated on success
 */
int coap_fota_get_firmware_size(coap_fota_query_t *p);

/**
 * @brief Get firmware from CoAP bridge.
 *
 * @param p is a pointer to a query
 *
 * @retval negative error code, 0 on success
 */
int coap_fota_get_firmware(coap_fota_query_t *p);

/**
 * @brief Get hash (or partial hash) of file
 *
 * @param p is a pointer to a query
 *
 * @retval negative error code, 0 on success
 * @note updates expected hash in query
 */
int coap_fota_get_hash(coap_fota_query_t *p);

#ifdef __cplusplus
}
#endif

#endif /* __COAP_FOTA_H__ */
