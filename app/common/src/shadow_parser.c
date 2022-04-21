/**
 * @file shadow_parser.c
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <logging/log.h>
LOG_MODULE_REGISTER(shadow_parser, CONFIG_SHADOW_PARSER_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#define JSMN_PARENT_LINKS
#define JSMN_HEADER
#include "jsmn.h"
#include "jsmn_json.h"

#include "shadow_parser.h"

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static sys_slist_t shadow_parser_list;

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void shadow_parser(const char *topic, const char *json)
{
	sys_snode_t *node;
	struct shadow_parser_agent *agent;
	struct topic_flags *flags;

	/* All modules with jsmn header files have access to the tokenization. */
	jsmn_start(json);
	if (!jsmn_valid()) {
		LOG_ERR("Unable to parse subscription %d", jsmn_tokens_found());
		return;
	}

	flags = shadow_parser_find_flags(topic);

	SYS_SLIST_FOR_EACH_NODE(&shadow_parser_list, node) {
    	agent = CONTAINER_OF(node, struct shadow_parser_agent, node);
		if (agent->parser != NULL) {
			jsmn_reset_index();
			agent->parser(topic, flags, json, agent->context);
		}
	}

	jsmn_end();
}

void shadow_parser_register_agent(struct shadow_parser_agent *agent)
{
	sys_slist_append(&shadow_parser_list, &agent->node);
}

int shadow_parser_find_state(void)
{
	jsmn_reset_index();

	return jsmn_find_type("state", JSMN_OBJECT, NO_PARENT);
}

bool shadow_parser_find_uint(uint32_t *value, const char *key)
{
	jsmn_reset_index();

	int location = jsmn_find_type(key, JSMN_PRIMITIVE, NO_PARENT);
	if (location > 0) {
		*value = jsmn_convert_uint(location);
		return true;
	} else {
		*value = 0;
		LOG_DBG("%s not found", log_strdup(key));
		return false;
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
__weak struct topic_flags *shadow_parser_find_flags(const char *topic)
{
	return NULL;
}
