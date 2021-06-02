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
#include "attr_custom_validator.h"

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define CHECK_ENTRY() __ASSERT(entry != NULL, "Invalid Entry (index)");

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
extern const struct attr_table_entry ATTR_TABLE[ATTR_TABLE_SIZE];

extern atomic_t attr_modified[];

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int av_tx_power(const ate_t *const entry, void *pv, size_t vlen, bool do_write)
{
	ARG_UNUSED(vlen);
	CHECK_ENTRY();
	int32_t value = *((int32_t *)pv);

	/* Values supported by nRF52840 */
	bool valid = false;
	switch (value) {
	case -40:
	case -20:
	case -16:
	case -12:
	case -8:
	case -4:
	case 0:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
		valid = true;
		break;

	default:
		valid = false;
		break;
	}

	if (valid) {
		if (do_write && value != *((int32_t *)entry->pData)) {
			atomic_set_bit(attr_modified, attr_table_index(entry));
			*((int32_t *)entry->pData) = value;
		}
		return 1;
	}
	return 0;
}
