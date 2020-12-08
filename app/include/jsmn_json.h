/**
 * @file jsmn_json.h
 * @brief Wrap jsmn JSON parser so that it can be used by multiple modules.
 *
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __JSMN_JSON_H__
#define __JSMN_JSON_H__

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stdbool.h>

/**
 * @note The jasmine library is a header file.  Therefore users of this file
 * must include the following or it won't work properly with multiple modules.
 * The jsmn_json.c file can't have this check.
 *
 * #define JSMN_PARENT_LINKS
 * #define JSMN_HEADER
 * #include "jsmn.h"
 */
#ifndef JSMN_SKIP_CHECKS
#ifndef JSMN_PARENT_LINKS
#error "Unsupported JSMN configuration"
#endif
#ifndef JSMN_HEADER
#error "Unsupported JSMN configuration"
#endif
#ifndef JSMN_H
#error "Unsupported JSMN configuration"
#endif
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/* Use next parent if the heirarchy matters. */
typedef enum parent_type { NO_PARENT = 0, NEXT_PARENT } parent_type_t;

/**
 * @brief Take jsmn mutex.  Tokenize JSON.
 *
 * @note It is assumed any user of this module will call this first.
 *
 * @param p is a pointer to JSON
 */
void jsmn_start(const char *p);

/**
 * @brief Release jsmn mutex.
 */
void jsmn_end(void);

/**
 * @brief Accessor function
 *
 * @retval true if the JSON was tokenized properly.
 */
bool jsmn_valid(void);

/**
 * @brief Accessor function
 *
 * @retval less than or equal to zero on error, otherwise number of tokens.
 */
int jsmn_tokens_found(void);

/**
 * @brief This function updates the global index to the next token when an
 * item + type is found.  Otherwise, the index is set to zero.
 *
 * @param s is the string to find
 * @param type is the type of JSON element to find.
 * @param parent_type_t is an enemuration that controls whether or not
 * heirarchy matters
 *
 * @retval > 0 then then the location of the data is returned.
 * @retval <= 0, then the item was not found
 */
int jsmn_find_type(const char *s, jsmntype_t type, parent_type_t parent_type);

/**
 * @brief Accessor function
 *
 * @retval the current token index
 */
int jsmn_index(void);

/**
 * @brief Helper function that resets index and parent
 */
void jsmn_reset_index(void);

/**
 * @brief Helper function that saves index and parent.
 */
void jsmn_save_index(void);

/**
 * @brief Helper function that restores index and parent from saved values.
 */
void jsmn_restore_index(void);

/**
 * @brief Converts string to uint
 *
 * @note If the string is larger than a 11 digits, then 0 is returned.
 */
uint32_t jsmn_convert_uint(int index);

/**
 * @brief Converts hex string to uint
 *
 * @note If the string is larger than a 8 digits, then 0 is returned.
 */
uint32_t jsmn_convert_hex(int index);

/**
 * @brief Accessor function
 *
 * @retval The type of the token at the specified index.
 */
jsmntype_t jsmn_type(int index);

/**
 * @brief Accessor function
 *
 * @retval The size of the token at the specified index.
 */
int jsmn_size(int index);

/**
 * @brief Accessor function
 *
 * @retval The size of the string at the specified token index.
 */
int jsmn_strlen(int index);

/**
 * @brief Accessor function
 *
 * @retval A pointer to a string the specified token.
 * Undefined if token @ index is not a string.
 */
const char *jsmn_string(int index);

#ifdef __cplusplus
}
#endif

#endif /* __JSMN_JSON_H__ */
