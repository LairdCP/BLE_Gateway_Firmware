/**
 * @file attr_defs.h
 * @brief  Attribute definitions
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __ATTR_DEFS_H__
#define __ATTR_DEFS_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
typedef uint16_t attr_index_t;
typedef uint16_t attr_id_t;

#define ATTR_INVALID_ID (UINT16_MAX - 1)

enum attr_type {
	ATTR_TYPE_UNKNOWN = 0,
	ATTR_TYPE_ANY,
	ATTR_TYPE_BOOL,
	ATTR_TYPE_U8,
	ATTR_TYPE_U16,
	ATTR_TYPE_U32,
	ATTR_TYPE_U64,
	ATTR_TYPE_S8,
	ATTR_TYPE_S16,
	ATTR_TYPE_S32,
	ATTR_TYPE_S64,
	ATTR_TYPE_FLOAT,
	ATTR_TYPE_STRING,
	ATTR_TYPE_BYTE_ARRAY,
	ATTR_TYPE_ATOMIC,
};

enum attr_size {
	ATTR_SIZE_UNKNOWN = 0,
	ATTR_SIZE_BOOL = sizeof(bool),
	ATTR_SIZE_U8 = sizeof(uint8_t),
	ATTR_SIZE_U16 = sizeof(uint16_t),
	ATTR_SIZE_U32 = sizeof(uint32_t),
	ATTR_SIZE_U64 = sizeof(uint64_t),
	ATTR_SIZE_S8 = sizeof(int8_t),
	ATTR_SIZE_S16 = sizeof(int16_t),
	ATTR_SIZE_S32 = sizeof(int32_t),
	ATTR_SIZE_S64 = sizeof(int64_t),
	ATTR_SIZE_FLOAT = sizeof(float),
};

struct attr_min_max {
	union {
		uint32_t ux;
		int32_t sx;
		float fx;
	};
};

typedef const char *const get_string_function(int value);

typedef struct attr_table_entry ate_t;

struct attr_table_entry {
	const attr_id_t id;
	const char *const name;
	void *const pData;
	const void *const pDefault;
	const size_t size;
	get_string_function *gsf;
	const enum attr_type type;
	const bool savable;
	const bool writable;
	const bool readable;
	const bool lockable;
	const bool broadcast;
	const bool deprecated;
	int (*const validator)(const ate_t *const, void *, size_t, bool);
	int (*const prepare)(void);
	const struct attr_min_max min;
	const struct attr_min_max max;
};

#ifdef __cplusplus
}
#endif

#endif /* __ATTR_DEFS_H__ */
