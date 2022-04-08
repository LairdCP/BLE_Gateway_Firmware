/**
 * @file jsmn_json.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(jsmn, CONFIG_JSMN_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_PARENT_LINKS
#include "jsmn.h"
#define JSMN_SKIP_CHECKS
#include "jsmn_json.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define MAX_DEC_CONVERSION_STR_SIZE 11
#define MAX_HEX_CONVERSION_STR_SIZE 8

#define ASSERT_BAD_INDEX() __ASSERT(false, "Invalid Index")

const char EMPTY_STRING[] = "";

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
K_MUTEX_DEFINE(jsmn_mutex);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct {
	jsmn_parser parser;
	jsmntok_t tokens[CONFIG_JSMN_NUMBER_OF_TOKENS];
	int tokens_found;
	int next_parent;
	int index;
	int saved_index;
	int saved_parent;
	const char *json;
} jsmn;

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void jsmn_start(const char *p)
{
	k_mutex_lock(&jsmn_mutex, K_FOREVER);

	jsmn_init(&jsmn.parser);

	/* Strip off metadata from AWS because it is too much to process. */
	/* This assumes order to the JSON ... */
	char *ignore = strstr(p, ",\"metadata\":");
	if (ignore != NULL) {
		*ignore = '}';
		*(ignore + 1) = 0;
	}

	jsmn.json = p;
	jsmn.tokens_found =
		jsmn_parse(&jsmn.parser, jsmn.json, strlen(jsmn.json),
			   jsmn.tokens, CONFIG_JSMN_NUMBER_OF_TOKENS);

	if (jsmn.tokens_found < 0) {
		LOG_ERR("jsmn status: %d", jsmn.tokens_found);
	} else {
		LOG_DBG("jsmn jsmn.tokens required: %d", jsmn.tokens_found);
	}

	(void)jsmn_reset_index();
}

void jsmn_end(void)
{
	jsmn.json = "";
	k_mutex_unlock(&jsmn_mutex);
}

/* Check that there were enough jsmn.tokens to parse string.
 * After parsing the first thing should be the JSON object { }.
 */
bool jsmn_valid(void)
{
	return ((jsmn.tokens_found > 0) &&
		(jsmn.tokens[0].type == JSMN_OBJECT));
}

const char *jsmn_json(void)
{
	return jsmn.json;
}

int jsmn_tokens_found(void)
{
	return jsmn.tokens_found;
}

int jsmn_find_type(const char *s, jsmntype_t type, parent_type_t parent_type)
{
	int location = 0;
	if (jsmn.index == 0) {
		return location;
	}

	/* Analyze a pair of jsmn.tokens of the form <string>, <type> */
	size_t i = jsmn.index;
	jsmn.index = 0;
	for (; ((i + 1) < jsmn.tokens_found); i++) {
		int length = jsmn.tokens[i].end - jsmn.tokens[i].start;
		if ((jsmn.tokens[i].type == JSMN_STRING) &&
		    ((int)strlen(s) == length) &&
		    (strncmp(jsmn.json + jsmn.tokens[i].start, s, length) ==
		     0) &&
		    (jsmn.tokens[i + 1].type == type) &&
		    ((parent_type == NO_PARENT) ||
		     (jsmn.tokens[i].parent == jsmn.next_parent))) {
			LOG_DBG("Found '%s' at index %d with parent %d", s, i,
				jsmn.tokens[i].parent);
			jsmn.next_parent = i + 1;
			jsmn.index = i + 2;
			break;
		}
	}
	location = jsmn.index - 2 + 1; /* location of the data */
	return location;
}

int jsmn_index(void)
{
	return jsmn.index;
}

void jsmn_reset_index(void)
{
	jsmn.index = 1;
	jsmn.next_parent = 0;
}

void jsmn_save_index(void)
{
	jsmn.saved_index = jsmn.index;
	jsmn.saved_parent = jsmn.next_parent;
}

void jsmn_restore_index(void)
{
	jsmn.index = jsmn.saved_index;
	jsmn.next_parent = jsmn.saved_parent;
}

uint32_t jsmn_convert_uint(int index)
{
	if (index > jsmn.tokens_found) {
		ASSERT_BAD_INDEX();
		return 0;
	}

	/* Pieces of the JSON message are not null terminated. */
	char str[MAX_DEC_CONVERSION_STR_SIZE];
	size_t length = jsmn_strlen(index);
	if (length < sizeof(str)) {
		memset(str, 0, sizeof(str));
		memcpy(str, jsmn_string(index), length);
		return MIN(UINT32_MAX, strtoul(str, NULL, 10));
	} else {
		return 0;
	}
}

uint32_t jsmn_convert_hex(int index)
{
	if (index > jsmn.tokens_found) {
		ASSERT_BAD_INDEX();
		return 0;
	}

	char str[MAX_HEX_CONVERSION_STR_SIZE];
	size_t length = jsmn_strlen(index);
	if (length < sizeof(str)) {
		memset(str, 0, sizeof(str));
		memcpy(str, jsmn_string(index), length);
		return strtoul(str, NULL, 16);
	} else {
		return 0;
	}
}

jsmntype_t jsmn_type(int index)
{
	if (index > jsmn.tokens_found) {
		ASSERT_BAD_INDEX();
		return JSMN_UNDEFINED;
	}

	return jsmn.tokens[index].type;
}

int jsmn_size(int index)
{
	if (index > jsmn.tokens_found) {
		ASSERT_BAD_INDEX();
		return 0;
	}

	return jsmn.tokens[index].size;
}

int jsmn_strlen(int index)
{
	if (index > jsmn.tokens_found) {
		ASSERT_BAD_INDEX();
		return 0;
	}

	jsmntok_t *tok = &jsmn.tokens[index];
	return (tok->end - tok->start);
}

const char *jsmn_string(int index)
{
	if (index > jsmn.tokens_found) {
		ASSERT_BAD_INDEX();
		return EMPTY_STRING;
	}

	return &jsmn.json[jsmn.tokens[index].start];
}
