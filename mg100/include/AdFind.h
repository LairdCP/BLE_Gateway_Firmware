/**
 * @file AdFind.h
 * @brief Find TLV (type, length, value) structures in advertisements.
 */

/*
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AD_FIND_H
#define AD_FIND_H

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

#define BT_DATA_INVALID 0x00

typedef struct AdHandleTag {
	u8_t *pPayload;
	size_t size;
} AdHandle_t;

//=============================================================================
// Global Data Definitions
//=============================================================================

//=============================================================================
// Global Function Prototypes
//=============================================================================
//-----------------------------------------------
//! @brief Finds a TLV in advertisement
//!
//! @param pAdv pointer to advertisement data
//! @param Length length of the data
//! @param Type1 type of TLV to find
//! @param Type2 second type of tlv to find, set to BT_DATA_INVALID when not used.
//! Parsing will stop on first type found.
//!
//! @retval AdHandle_t - pointer to payload if found otherwise NULL
//!

AdHandle_t AdFind_Type(u8_t *pAdv, size_t Length, u8_t Type1, u8_t Type2);

//-----------------------------------------------
//! @brief Finds a short or complete name in advertisement.
//!
//! @retval AdHandle_t pointer to payload if found otherwise NULL
//!
AdHandle_t AdFind_Name(u8_t *pAdv, size_t Length);

#ifdef __cplusplus
}
#endif

#endif

// end
