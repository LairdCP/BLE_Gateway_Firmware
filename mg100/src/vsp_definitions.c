/**
 * @file vsp_definitions.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "laird_bluetooth.h"
#include "vsp_definitions.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define VSP_BASE_UUID_128(_x_)                                                 \
	BT_UUID_INIT_128(0x7c, 0x16, 0xa5, 0x5e, 0xba, 0x11, 0xcb, 0x92, 0x0c, \
			 0x49, 0x7f, 0xb8, LSB_16(_x_), MSB_16(_x_), 0x9a,     \
			 0x56);

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
const struct bt_uuid_128 VSP_TX_UUID = VSP_BASE_UUID_128(0x2000);
const struct bt_uuid_128 VSP_RX_UUID = VSP_BASE_UUID_128(0x2001);
const struct bt_uuid_16 VSP_TX_CCC_UUID = BT_UUID_INIT_16(0x2902);
