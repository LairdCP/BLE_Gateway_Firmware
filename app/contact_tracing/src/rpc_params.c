/**
 * @file rpc_params.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define CONFIG_RPC_PARAMS_LOG_LEVEL 4
#include <logging/log.h>
LOG_MODULE_REGISTER(rpc_params, CONFIG_RPC_PARAMS_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <zephyr/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_PARENT_LINKS
#define JSMN_HEADER
#include "jsmn.h"
#include "jsmn_json.h"

#include "rpc_params.h"

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static char rpc_method[CONFIG_RPC_PARAMS_METHOD_MAX_SIZE];

/* Stores the param values parsed from an rpc method call.
 * Format of the data will map to one of the rpc_params_* structures.
 */
static uint8_t rpc_param_buf[CONFIG_RPC_PARAMS_BUF_MAX_SIZE];

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int rpc_params_parse(void);
static int rpc_parse(int location);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void rpc_params_gateway_parser(bool get_accepted_topic)
{
	int location;

	jsmn_reset_index();

	jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	if (get_accepted_topic) {
		/* get an outstanding command (not the last command ['reported']) */
		jsmn_find_type("desired", JSMN_OBJECT, NEXT_PARENT);
	}
	jsmn_find_type("rpc", JSMN_OBJECT, NEXT_PARENT);
	jsmn_save_index();
	location = jsmn_find_type("m", JSMN_STRING, NEXT_PARENT);
	if (jsmn_index() != 0) {
		jsmn_restore_index();
		rpc_parse(location);
	}
}

char *rpc_params_get_method(void)
{
	return rpc_method;
}

void *rpc_params_get(void)
{
	return (void *)rpc_param_buf;
}

void rpc_params_clear_method(void)
{
	memset(rpc_method, 0, sizeof(rpc_method));
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/

/*
 * Parse the RPC params from the module global JSON contents
 * currently being processed.
 */
static int rpc_params_parse(void)
{
	int r = 0;
	int location = 0;

	memset(rpc_param_buf, 0, sizeof(rpc_param_buf));

	jsmn_find_type("p", JSMN_OBJECT, NEXT_PARENT);
	jsmn_save_index();

	do {
		/* log_get */
		if (strstr(rpc_method, "log_get") != NULL) {
			rpc_params_log_get_t *params =
				(rpc_params_log_get_t *)rpc_param_buf;

			/* filename */
			jsmn_restore_index();
			location =
				jsmn_find_type("f", JSMN_STRING, NEXT_PARENT);
			if (location > 0) {
				JSMN_STRNCPY(params->filename, location);
			} else {
				LOG_ERR("Invalid filename");
				r = -1;
				break;
			}

			/* whence */
			jsmn_restore_index();
			location =
				jsmn_find_type("w", JSMN_STRING, NEXT_PARENT);
			if (location > 0) {
				JSMN_STRNCPY(params->whence, location);
			} else {
				LOG_ERR("Invalid whence");
				r = -1;
				break;
			}

			/* offset */
			jsmn_restore_index();
			location = jsmn_find_type("o", JSMN_PRIMITIVE,
						  NEXT_PARENT);
			if (location > 0) {
				params->offset = jsmn_convert_uint(location);
			} else {
				LOG_ERR("Invalid offset");
				r = -1;
				break;
			}

			/* length */
			jsmn_restore_index();
			location = jsmn_find_type("l", JSMN_PRIMITIVE,
						  NEXT_PARENT);
			if (location > 0) {
				params->length = jsmn_convert_uint(location);
			} else {
				LOG_ERR("Invalid length");
				r = -1;
				break;
			}

			/* If offset is < length and whence is "end", set offset to
			* length so proper # of bytes are read from end of file.
			*/
			if (strncmp(params->whence, "end", 3) == 0) {
				if (params->length > params->offset) {
					params->offset = params->length;
				}
			}

		} else if (strstr(rpc_method, "reboot") != NULL) {
			/* nothing to process */
			r = 0;

		} else if (strstr(rpc_method, "log_dir") != NULL) {
			/* nothing to process */
			r = 0;

		} else if (strstr(rpc_method, "exec") != NULL) {
			rpc_params_exec_t *params =
				(rpc_params_exec_t *)rpc_param_buf;

			/* cmd */
			location =
				jsmn_find_type("c", JSMN_STRING, NEXT_PARENT);
			if (location > 0) {
				JSMN_STRNCPY(params->cmd, location);
			} else {
				LOG_ERR("Unable to find command");
				r = -1;
				break;
			}

			if (strlen(params->cmd) <= 0) {
				LOG_ERR("Command cannot be an empty string");
				r = -1;
				break;
			}
		} else {
			LOG_ERR("Unknown RPC command");
			r = -1;
		}
	} while (0);

	/* clear params on parse error */
	if (r < 0) {
		memset(rpc_param_buf, 0, sizeof(rpc_param_buf));
	}

	return r;
}

/*
 * Parse the RPC method from the module global JSON contents
 * currently being processed.
 */
static int rpc_parse(int location)
{
	int r = -EPERM;

	if (jsmn_strlen(location) < sizeof(rpc_method)) {
		rpc_params_clear_method();
		JSMN_STRNCPY(rpc_method, location);
		LOG_DBG("rpc.m: %s", log_strdup(rpc_method));
		r = rpc_params_parse();
		if (r < 0) {
			LOG_ERR("Unable to parse RPC command");
			rpc_params_clear_method();
		}
	}
	return r;
}
