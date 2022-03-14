/**
 * @file shadow_parser.h
 * @brief Parse JSON shadow using jsmn
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SHADOW_PARSER_H__
#define __SHADOW_PARSER_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <sys/slist.h>

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
/* Forward Declaration - Flags are defined per project */
struct topic_flags;

struct shadow_parser_agent {
	sys_snode_t node;
	void (*parser)(const char *topic, struct topic_flags *flags);
};

/* Check that flag pointer is valid */
#define SP_FLAG(x) ((flags == NULL) ? false : (flags->x))

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Use jsmn to parse the JSON string into tokens.  Then iterate though
 * the list of agents and allow them to search for tokens they care about.
 *
 * @param topic string
 * @param json string
 */
void shadow_parser(const char *topic, const char *json);

/**
 * @brief Find substrings in topic to generate flags.
 * Must be defined by application.
 * Called by shadow parser after jsmn mutex is obtained.
 *
 * @param topic pointer to topic
 * @retval flags pointer to flags
 */
struct topic_flags *shadow_parser_find_flags(const char *topic);

/**
 * @brief Register a shadow processor that will be called from the AWS
 * RX Thread when publish (subscription) is received.
 *
 * @param agent is used to process shadow
 */
void shadow_parser_register_agent(struct shadow_parser_agent *agent);

/**
 * @brief Find the index of "state"
 *
 * @note sets jsmn index to 1
 *
 * @return int index of state
 */
int shadow_parser_find_state(void);

/**
 * @brief Find primitive matching key and convert it to unsigned int
 *
 * @note sets jsmn index to 1
 *
 * @param value pointer to value found
 * @param key is the name of the string to look for
 * @return true if found
 * @return false otherwise
 */
bool shadow_parser_find_uint(uint32_t *value, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* __SHADOW_PARSER_H__ */
