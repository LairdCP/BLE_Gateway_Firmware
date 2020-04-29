/**
 * @file hexcode.c
 * @brief Hex encoding functionality
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "hexcode.h"

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void HexEncode(u8_t *pInput, u32_t nLength, u8_t *pOutput, bool bUpperCase,
	       bool bWithNullTermination)
{
	uint32_t nPos = 0;
	while (nPos < nLength) {
		pOutput[nPos] = (pInput[nPos / 2] & 0xf0) / 16 +
				HEX_ENCODE_NUMERIC_ADDITION;
		pOutput[nPos + 1] =
			(pInput[nPos / 2] & 0x0f) + HEX_ENCODE_NUMERIC_ADDITION;
		if (pOutput[nPos] > '9') {
			pOutput[nPos] +=
				(bUpperCase == true ?
					 HEX_ENCODE_UPPER_CASE_ALPHA_ADDITION :
					 HEX_ENCODE_LOWER_CASE_ALPHA_ADDITION);
		}
		if (pOutput[nPos + 1] > '9') {
			pOutput[nPos + 1] +=
				(bUpperCase == true ?
					 HEX_ENCODE_UPPER_CASE_ALPHA_ADDITION :
					 HEX_ENCODE_LOWER_CASE_ALPHA_ADDITION);
		}
		nPos += 2;
	}

	if (bWithNullTermination == true) {
		pOutput[nPos] = 0;
	}
}
