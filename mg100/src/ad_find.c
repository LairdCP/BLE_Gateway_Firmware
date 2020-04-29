/**
 * @file ad_find.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/bluetooth.h>

#include "ad_find.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
/* In the TLV structure, the minimum value index is 2
 * (0-length, 1-type, 2-value).
 */
#define MIN_VALUE_INDEX 2

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
AdHandle_t AdFind_Type(u8_t *pAdv, size_t Length, u8_t Type1, u8_t Type2)
{
	AdHandle_t result = { NULL, 0 };
	size_t i = 0;
	while (i < Length) {
		/* Quasi-validate the length so the code can't get stuck. */
		result.size = pAdv[i];
		if ((result.size >= MIN_VALUE_INDEX) &&
		    ((i + result.size) <= Length)) {
			u8_t elementType = pAdv[i + 1];
			if ((elementType == Type1) &&
			    (Type1 != BT_DATA_INVALID)) {
				result.pPayload = pAdv + i + MIN_VALUE_INDEX;
				/* subtract length of type field. */
				result.size -= 1;
				return result;
			} else if ((elementType == Type2) &&
				   (Type2 != BT_DATA_INVALID)) {
				result.pPayload = pAdv + i + MIN_VALUE_INDEX;
				/* subtract length of type field. */
				result.size -= 1;
				return result;
			}
			/* skip one extra byte because length field not included in length */
			i += result.size + 1;
		} else {
			/* break out of loop */
			i += Length;
		}
	}

	return result;
}

AdHandle_t AdFind_Name(u8_t *pAdv, size_t Length)
{
	return AdFind_Type(pAdv, Length, BT_DATA_NAME_SHORTENED,
			   BT_DATA_NAME_COMPLETE);
}
