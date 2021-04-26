/**
 * @file attr_validator.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <device.h>
#include <string.h>
#include <ctype.h>

#include "attr_table.h"
#include "attr_table_private.h"

#include "attr_validator.h"

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define CHECK_ENTRY() __ASSERT(entry != NULL, "Invalid Entry (index)");

/**
 * @brief Check if value is in the valid rage if min != max.
 *
 * @note Range is limited to 32-bits by attr_min_max
 */
#define VALID_RANGE()                                                          \
	((value >= entry->min.ux) && (value <= entry->max.ux)) ||              \
		(entry->min.ux == entry->max.ux)

#define VALID_BOOL() (value == true || value == false)

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
extern const struct attr_table_entry ATTR_TABLE[ATTR_TABLE_SIZE];

extern atomic_t attr_modified[];

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int av_string(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	CHECK_ENTRY();
	__ASSERT((entry->size == entry->max.ux + 1), "Unexpected string size");
	int r = -EPERM;

	/* -1 to account for NULL */
	if (entry->size > vlen) {
		size_t current_vlen = strlen(entry->pData);
		if (do_write && (vlen >= entry->min.ux) &&
		    ((current_vlen != vlen) ||
		     (memcmp(entry->pData, pv, vlen) != 0))) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			memset(entry->pData, 0, entry->size);
			strncpy(entry->pData, pv, vlen);
		}
		r = 0;
	}
	return r;
}

int av_array(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	CHECK_ENTRY();
	int r = -EPERM;

	/* For arrays, the entire value must be set. */
	if (entry->size == vlen) {
		if (do_write && (memcmp(entry->pData, pv, vlen) != 0)) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			memcpy(entry->pData, pv, vlen);
		}
		r = 0;
	}
	return r;
}

int av_uint64(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	uint64_t value = *(uint64_t *)pv;

	if (do_write && value != *((uint64_t *)entry->pData)) {
		atomic_set_bit(attr_modified, attr_table_index(entry));
		*((uint64_t *)entry->pData) = value;
	}
	return 0;
}

int av_uint32(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	uint32_t value = *(uint32_t *)pv;

	if (VALID_RANGE()) {
		if (do_write && value != *((uint32_t *)entry->pData)) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((uint32_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_uint16(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	uint32_t value = (uint32_t)(*(uint16_t *)pv);

	if (VALID_RANGE()) {
		if (do_write && value != *((uint16_t *)entry->pData)) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((uint16_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_uint8(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	uint32_t value = (uint32_t)(*(uint8_t *)pv);

	if (VALID_RANGE()) {
		if (do_write && value != *((uint8_t *)entry->pData)) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((uint8_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_bool(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	uint32_t value = (uint32_t)(*(bool *)pv);

	if (VALID_BOOL()) {
		if (do_write && value != *((bool *)entry->pData)) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((bool *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_int64(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int64_t value = *(int64_t *)pv;

	if (do_write && value != *((int64_t *)entry->pData)) {
		atomic_set_bit(attr_modified, attr_table_index(entry));
		*((int64_t *)entry->pData) = value;
	}
	return 0;
}

int av_int32(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	int32_t value = *(int32_t *)pv;

	if (((value >= entry->min.sx) && (value <= entry->max.sx)) ||
	    (entry->min.sx == entry->max.sx)) {
		if (do_write && value != *((int32_t *)entry->pData)) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((int32_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_int16(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	int32_t value = (int32_t)(*(int16_t *)pv);

	if (((value >= entry->min.sx) && (value <= entry->max.sx)) ||
	    (entry->min.sx == entry->max.sx)) {
		if (do_write && value != *((int16_t *)entry->pData)) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((int16_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_int8(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	int32_t value = (int32_t)(*(int8_t *)pv);

	if (((value >= entry->min.sx) && (value <= entry->max.sx)) ||
	    (entry->min.sx == entry->max.sx)) {
		if (do_write && value != *((int8_t *)entry->pData)) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((int8_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_float(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	float value = *((float *)pv);

	if (((value >= entry->min.fx) && (value <= entry->max.fx)) ||
	    (entry->min.fx == entry->max.fx)) {
		if (do_write && value != *((float *)entry->pData)) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((float *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

/**
 * @brief Control Point Validators
 * Don't check if value is the same because this is a control point.
 */
int av_cp32(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	uint32_t value = *(uint32_t *)pv;

	if (VALID_RANGE()) {
		if (do_write) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((uint32_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_cp16(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	uint32_t value = (uint32_t)(*(uint16_t *)pv);

	if (VALID_RANGE()) {
		if (do_write) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((uint16_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_cp8(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	uint32_t value = (uint32_t)(*(uint8_t *)pv);

	if (VALID_RANGE()) {
		if (do_write) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((uint8_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_cpi32(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	int32_t value = *(int32_t *)pv;

	if (((value >= entry->min.sx) && (value <= entry->max.sx)) ||
	    (entry->min.sx == entry->max.sx)) {
		if (do_write) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((int32_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_cpi16(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	int32_t value = (int32_t)(*(int16_t *)pv);

	if (((value >= entry->min.sx) && (value <= entry->max.sx)) ||
	    (entry->min.sx == entry->max.sx)) {
		if (do_write) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((int16_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_cpi8(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	int32_t value = (int32_t)(*(int8_t *)pv);

	if (((value >= entry->min.sx) && (value <= entry->max.sx)) ||
	    (entry->min.sx == entry->max.sx)) {
		if (do_write) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((int8_t *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}

int av_cpb(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();

	int r = -EPERM;
	uint32_t value = (uint32_t)(*(bool *)pv);

	if (VALID_BOOL()) {
		if (do_write) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((bool *)entry->pData) = value;
		}
		r = 0;
	}
	return r;
}