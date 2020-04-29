/**
 * @file to_string.h
 * @brief Efficient string conversions from a Google project.
 * Fixed size (simple) hex string conversions that do not
 * include an "0x" prefix and include a null character.
 *
 * Copyright (c) 2020 Laird Connectivity
 * Copyright (c) Google
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __TO_STRING_H__
#define __TO_STRING_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define MAXIMUM_LENGTH_OF_TO_STRING_OUTPUT 11 /* Includes the NUL character */

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief Converts Value into decimal string.
 * The output string will be at least 2 bytes.
 * @note Max output size is 11 bytes.
 */
u8_t ToString_Dec(char *pString, u32_t Value);

/**
 * @brief Converts Value into a hexadecimal string.
 * Output is 9 bytes.  NUL included.
 */
void ToString_Hex32(char *pString, u32_t Value);

/**
 * @brief Converts Value into a hexadecimal string.
 * Output is 5 bytes. NUL included.
 */
void ToString_Hex16(char *pString, uint16_t Value);

/**
 * @brief Converts Value into a hexadecimal string.
 * Output is 3 bytes. NUL included.
 */
void ToString_Hex8(char *pString, u8_t Value);

#ifdef __cplusplus
}
#endif

#endif /* __TO_STRING_H__ */
