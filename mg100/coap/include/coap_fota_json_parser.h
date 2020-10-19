/**
 * @file coap_fota_json_parser.h
 * @brief Parse messages from the CoAP bridge using jsmn.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __COAP_FOTA_JSON_PARSER_H__
#define __COAP_FOTA_JSON_PARSER_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "file_system_utilities.h"

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Process the size response from the CoAP bridge
 *
 * @param p is a pointer to JSON string.
 * @param name is the string name for the size parameter
 *
 * @retval -1 if there is an error, otherwise the size of the file in bytes.
 */
int coap_fota_json_parser_get_size(const char *p, const char *size_str);

/**
 * @brief Process the size response from the CoAP bridge
 *
 * @param hash is the hash array
 * @param p is a pointer to JSON string.
 * @param name is the string name for the hash parameter
 *
 * @retval -1 if there is an error, otherwise 0
 */
int coap_fota_json_parser_get_hash(uint8_t hash[FSU_HASH_SIZE], const char *p,
				   const char *name);

#ifdef __cplusplus
}
#endif

#endif /* __COAP_FOTA_JSON_PARSER_H__ */
