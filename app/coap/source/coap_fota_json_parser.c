/**
 * @file coap_fota_json_parser.c
 * @brief Uses jsmn to parse JSON.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(coap_fota_json_parser, LOG_LEVEL_INF);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>
#include <sys/util.h>

#define JSMN_PARENT_LINKS
#define JSMN_HEADER
#include "jsmn.h"
#include "jsmn_json.h"

#include "coap_fota_json_parser.h"

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int coap_fota_json_parser_get_size(const char *p, const char *name)
{
	int result = -1;

	jsmn_start(p);
	if (jsmn_valid()) {
		jsmn_find_type("result", JSMN_OBJECT, NEXT_PARENT);
		int location =
			jsmn_find_type(name, JSMN_PRIMITIVE, NEXT_PARENT);
		if (location > 0) {
			result = jsmn_convert_uint(location);
		}
	}
	jsmn_end();

	return result;
}

int coap_fota_json_parser_get_hash(uint8_t hash[FSU_HASH_SIZE], const char *p,
				   const char *name)
{
	int result = -1;
	memset(hash, 0, FSU_HASH_SIZE);

	/* Example
	* "result": {
	* "hash": "5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef",
	* "algorithm": "sha256",
	* "range": "bytes=0-500",
	* "protocol-version": 1
	* }
	*/
	jsmn_start(p);
	if (jsmn_valid()) {
		jsmn_find_type("result", JSMN_OBJECT, NEXT_PARENT);
		int location = jsmn_find_type(name, JSMN_STRING, NEXT_PARENT);
		if (location > 0) {
			size_t length = hex2bin(jsmn_string(location),
						jsmn_strlen(location), hash,
						FSU_HASH_SIZE);
			result = (length == FSU_HASH_SIZE) ? 0 : -1;
		}
	}
	jsmn_end();

	return result;
}
