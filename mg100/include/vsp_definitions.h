/**
 * @file vsp_definitions.h
 * @brief Laird Connectivity Virtual Serial Port Service Definitions
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __VSP_DEFINTIONS_H__
#define __VSP_DEFINTIONS_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/

/* Client data is written to the RX fifo and read (notified) from the TX fifo */
extern const struct bt_uuid_128 VSP_TX_UUID;
extern const struct bt_uuid_128 VSP_RX_UUID;
extern const struct bt_uuid_16 VSP_TX_CCC_UUID;

#ifdef __cplusplus
}
#endif

#endif /* __VSP_DEFINTIONS_H__ */
