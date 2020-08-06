/**
 * @file single_peripheral.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(single_peripheral);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <bluetooth/bluetooth.h>

#include "laird_bluetooth.h"

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void sp_disconnected(struct bt_conn *conn, uint8_t reason);
static void sp_connected(struct bt_conn *conn, uint8_t err);
static int start_advertising(void);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, 0x36, 0xa3, 0x4d, 0x40, 0xb6, 0x70,
		      0x69, 0xa6, 0xb1, 0x4e, 0x84, 0x9e, 0x60, 0x7c, 0x78,
		      0x43),
};

static struct bt_conn *sp_conn = NULL;

static struct bt_conn_cb sp_conn_callbacks = {
	.connected = sp_connected,
	.disconnected = sp_disconnected,
};

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void single_peripheral_initialize(void)
{
	bt_conn_cb_register(&sp_conn_callbacks);

	start_advertising();
}

struct bt_conn *single_peripheral_get_conn(void)
{
	return sp_conn;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void sp_connected(struct bt_conn *conn, uint8_t err)
{
	/* Did a central connect to us? */
	if (!lbt_slave_role(conn)) {
		return;
	}

	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_ERR("Failed to connect to central %s (%u)",
			log_strdup(addr), err);
		bt_conn_unref(conn);
		sp_conn = NULL;
	} else {
		LOG_INF("Connected central: %s", log_strdup(addr));
		sp_conn = bt_conn_ref(conn);

		/* stop advertising so another central cannot connect */
		bt_le_adv_stop();
	}
}

static void sp_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (!lbt_slave_role(conn)) {
		return;
	}

	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected central: %s (reason %u)", log_strdup(addr),
		reason);
	bt_conn_unref(conn);
	sp_conn = NULL;
	start_advertising();
}

static int start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL,
				  0);
	LOG_INF("Advertising start (%d)", err);
	return err;
}
