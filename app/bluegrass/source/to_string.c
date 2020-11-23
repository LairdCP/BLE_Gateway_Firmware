/**
 * @file to_string.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 * Copyright (c) Google
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "to_string.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifndef NUL
#define NUL 0
#endif

static const char DIGITS[] = "0001020304050607080910111213141516171819"
			     "2021222324252627282930313233343536373839"
			     "4041424344454647484950515253545556575859"
			     "6061626364656667686970717273747576777879"
			     "8081828384858687888990919293949596979899";

#define TO_CHAR(n) (((n) > 9) ? ('A' + (n)-10) : ('0' + (n)))

/******************************************************************************/
/* Local Functions                                                            */
/******************************************************************************/
static inline uint8_t NumberOfBase10Digits(uint32_t Value)
{
	if (Value < 10)
		return 1;
	if (Value < 100)
		return 2;
	if (Value < 1000)
		return 3;
	if (Value < 10000)
		return 4;
	if (Value < 100000)
		return 5;
	if (Value < 1000000)
		return 6;
	if (Value < 10000000)
		return 7;
	if (Value < 100000000)
		return 8;
	if (Value < 1000000000)
		return 9;
	return 10;
}

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
uint8_t ToString_Dec(char *pString, uint32_t Value)
{
	uint32_t remainder = Value;
	uint8_t const length = NumberOfBase10Digits(Value);
	uint8_t index = length - 1;

	pString[length] = NUL;

	while (remainder >= 100) {
		uint8_t d = (remainder % 100) * 2;
		remainder /= 100;
		pString[index] = DIGITS[d + 1];
		pString[index - 1] = DIGITS[d];
		index -= 2;
	}

	if (remainder < 10) {
		pString[index] = '0' + remainder;
	} else {
		uint8_t d = remainder * 2;
		pString[index] = DIGITS[d + 1];
		pString[index - 1] = DIGITS[d];
	}

	return length;
}

void ToString_Hex32(char *pString, uint32_t Value)
{
	pString[0] = TO_CHAR((Value >> 28) & 0x0F);
	pString[1] = TO_CHAR((Value >> 24) & 0x0F);
	pString[2] = TO_CHAR((Value >> 20) & 0x0F);
	pString[3] = TO_CHAR((Value >> 16) & 0x0F);
	pString[4] = TO_CHAR((Value >> 12) & 0x0F);
	pString[5] = TO_CHAR((Value >> 8) & 0x0F);
	pString[6] = TO_CHAR((Value >> 4) & 0x0F);
	pString[7] = TO_CHAR((Value >> 0) & 0x0F);
	pString[8] = NUL;
}

void ToString_Hex16(char *pString, uint16_t Value)
{
	pString[0] = TO_CHAR((Value >> 12) & 0x0F);
	pString[1] = TO_CHAR((Value >> 8) & 0x0F);
	pString[2] = TO_CHAR((Value >> 4) & 0x0F);
	pString[3] = TO_CHAR((Value >> 0) & 0x0F);
	pString[4] = NUL;
}

void ToString_Hex8(char *pString, uint8_t Value)
{
	pString[0] = TO_CHAR((Value >> 4) & 0x0F);
	pString[1] = TO_CHAR((Value >> 0) & 0x0F);
	pString[2] = NUL;
}
