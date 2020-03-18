//=============================================================================
//! @file ToString.h
//!
//! @brief Efficient string conversions from a Google project.
//! Fixed size (simple) hex string conversions that don't
//! include an "0x" prefix and include a null character.
//!
//=============================================================================

#ifndef TO_STRING_H
#define TO_STRING_H

//=============================================================================
// Includes
//=============================================================================

#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Global Constants, Macros and Type Definitions
//=============================================================================
#define MAXIMUM_LENGTH_OF_TO_STRING_OUTPUT 11 // includes the NUL character

//=============================================================================
// Global Data Definitions
//=============================================================================
// NA

//=============================================================================
// Global Function Prototypes
//=============================================================================

//-----------------------------------------------
//! @brief Converts Value into decimal string.
//! The output string will be at least 2 bytes.
//! @note Max output size is 11 bytes.
//!
u8_t ToString_Dec(char *pString, u32_t Value);

//-----------------------------------------------
//! @brief Converts Value into a hexadecimal string.
//! Output is 9 bytes.  NUL included.
//!
void ToString_Hex32(char *pString, u32_t Value);

//-----------------------------------------------
//! @brief Converts Value into a hexadecimal string.
//! Output is 5 bytes. NUL included.
//!
void ToString_Hex16(char *pString, uint16_t Value);

//-----------------------------------------------
//! @brief Converts Value into a hexadecimal string.
//! Output is 3 bytes. NUL included.
//!
void ToString_Hex8(char *pString, u8_t Value);

#ifdef __cplusplus
}
#endif

#endif

// end
