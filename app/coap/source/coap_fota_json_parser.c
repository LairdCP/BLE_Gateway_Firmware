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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/util.h>

#define JSMN_PARENT_LINKS
#define JSMN_HEADER
#include "jsmn.h"

#include "coap_fota_json_parser.h"

/******************************************************************************/
/* Local Constant, Macro and type Definitions                                 */
/******************************************************************************/
#define MAX_CONVERSION_STR_SIZE 11
#define MAX_CONVERSION_STR_LEN (MAX_CONVERSION_STR_SIZE - 1)

/******************************************************************************/
/* Global                                                                     */
/******************************************************************************/
extern struct k_mutex jsmn_mutex;
extern jsmn_parser jsmn;
extern jsmntok_t tokens[CONFIG_JSMN_NUMBER_OF_TOKENS];

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static int tokens_found;
static int next_parent;
static int json_index;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void jsmn_start(const char *p);
static void jsmn_end(void);
static bool json_valid(void);
static int find_type(const char *p, const char *s, jsmntype_t type, int parent);

static uint32_t convert_uint(const char *p, int index);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int coap_fota_json_parser_get_size(const char *p, const char *name)
{
	int result = -1;

	jsmn_start(p);
	if (json_valid()) {
		find_type(p, "result", JSMN_OBJECT, next_parent);
		int location = find_type(p, name, JSMN_PRIMITIVE, next_parent);
		if (location > 0) {
			result = convert_uint(p, location + 1);
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
	if (json_valid()) {
		find_type(p, "result", JSMN_OBJECT, next_parent);
		int location = find_type(p, name, JSMN_STRING, next_parent);
		if (location > 0) {
			jsmntok_t *tok = &tokens[location + 1];
			size_t length =
				hex2bin(&p[tok->start], (tok->end - tok->start),
					hash, FSU_HASH_SIZE);
			result = (length == FSU_HASH_SIZE) ? 0 : -1;
		}
	}
	jsmn_end();

	return result;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void jsmn_start(const char *p)
{
	k_mutex_lock(&jsmn_mutex, K_FOREVER);

	jsmn_init(&jsmn);

	tokens_found = jsmn_parse(&jsmn, p, strlen(p), tokens,
				  CONFIG_JSMN_NUMBER_OF_TOKENS);

	if (tokens_found < 0) {
		LOG_ERR("jsmn status: %d", tokens_found);
	} else {
		LOG_DBG("jsmn tokens required: %d", tokens_found);
	}

	json_index = 1;
	next_parent = 0;
}

/* Check that there were enough tokens to parse string.
 * After parsing the first thing should be the JSON object { }.
 */
static bool json_valid(void)
{
	return ((tokens_found > 0) && (tokens[0].type == JSMN_OBJECT));
}

static void jsmn_end(void)
{
	k_mutex_unlock(&jsmn_mutex);
}

/**
 * @brief This function updates the global index to the next token when an
 * item + type is found.  Otherwise, the index is set to zero.
 *
 * @retval > 0 then the item was found at this index
 * @retval <= 0, then the item was not found
 */
static int find_type(const char *p, const char *s, jsmntype_t type, int parent)
{
	if (json_index == 0) {
		return 0;
	}

	/* Analyze a pair of tokens of the form <string>, <type> */
	size_t i = json_index;
	json_index = 0;
	for (; ((i + 1) < tokens_found); i++) {
		int length = tokens[i].end - tokens[i].start;
		if ((tokens[i].type == JSMN_STRING) &&
		    ((int)strlen(s) == length) &&
		    (strncmp(p + tokens[i].start, s, length) == 0) &&
		    (tokens[i + 1].type == type) &&
		    ((parent == 0) || (tokens[i].parent == parent))) {
			LOG_DBG("Found '%s' at index %d with parent %d", s, i,
				tokens[i].parent);
			next_parent = i + 1;
			json_index = i + 2;
			break;
		}
	}
	return (json_index - 2);
}

static uint32_t convert_uint(const char *p, int index)
{
	char str[MAX_CONVERSION_STR_SIZE];
	int length = tokens[index].end - tokens[index].start;
	memset(str, 0, sizeof(str));
	memcpy(str, &p[tokens[index].start], length);
	return strtoul(str, NULL, 10);
}
