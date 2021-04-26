/**
 * @file attr_table_private.h
 *
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __ATTRIBUTE_TABLE_PRIVATE_H__
#define __ATTRIBUTE_TABLE_PRIVATE_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <zephyr/types.h>
#include <stddef.h>

#include "attr_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief Copy defaults (before loading from flash)
 */
void attr_table_initialize(void);

/**
 * @brief set non-backup values to default
 */
void attr_table_factory_reset(void);

/**
 * @brief map id to table entry
 *
 * @param id of attribute
 * @return const struct attr_table_entry* const
 */
const struct attr_table_entry *const attr_map(attr_id_t id);

/**
 * @brief Calculate index of entry
 *
 * @param entry
 * @return attr_index_t
 */
attr_index_t attr_table_index(const struct attr_table_entry *const entry);

#ifdef __cplusplus
}
#endif

#endif /* __ATTRIBUTE_TABLE_PRIVATE_H__ */
