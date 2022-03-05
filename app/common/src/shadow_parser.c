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
#include "shadow_parser.h"

#define JSMN_PARENT_LINKS
#define JSMN_HEADER
#include "jsmn.h"
#include "jsmn_json.h"

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

	jsmn_start(json);
	if (!jsmn_valid()) {
		LOG_ERR("Unable to parse subscription %d", jsmn_tokens_found());
		return;
	}

	SYS_SLIST_FOR_EACH_NODE(&shadow_parser_list, node) {
    	agent = CONTAINER_OF(node, struct shadow_parser_agent, node);
		if (agent->parser != NULL) {
			agent->parser(topic, json);
		}
	}

	jsmn_end();
}

void shadow_parser_register_agent(struct shadow_parser_agent *agent)
{
	sys_slist_append(&shadow_parser_list, &agent->node);
}

bool shadow_parser_topic_is_get_accepted(const char *topic)
{
	return (strstr(topic, CONFIG_SHADOW_PARSER_GET_ACCEPTED_SUB_STR) != NULL);
}

bool shadow_parser_topic_is_gateway(const char *topic)
{
	return (strstr(topic, CONFIG_SHADOW_PARSER_GATEWAY_TOPIC_SUB_STR) != NULL);
}
