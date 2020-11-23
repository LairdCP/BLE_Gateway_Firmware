/**
 * @file jsmn_share.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>

#define JSMN_PARENT_LINKS
#include "jsmn.h"

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
K_MUTEX_DEFINE(jsmn_mutex);

jsmn_parser jsmn;
jsmntok_t tokens[CONFIG_JSMN_NUMBER_OF_TOKENS];
