/**
 * @file single_peripheral.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(single_peripheral, CONFIG_SINGLE_PERIPHERAL_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>
#include <init.h>

#include "laird_bluetooth.h"
#include "single_peripheral.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
static const struct bt_data AD[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, 0x36, 0xa3, 0x4d, 0x40, 0xb6, 0x70,
		      0x69, 0xa6, 0xb1, 0x4e, 0x84, 0x9e, 0x60, 0x7c, 0x78,
		      0x43),
};

struct single_peripheral {
	bool initialized;
	bool advertising;
	bool start;
	struct bt_conn *conn_handle;
	struct bt_conn_cb conn_callbacks;
	struct k_timer timer;
	struct k_work work;
};

#define ADV_DURATION CONFIG_SINGLE_PERIPHERAL_ADV_DURATION_SECONDS

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int single_peripheral_initialize(const struct device *device);
static void sp_disconnected(struct bt_conn *conn, uint8_t reason);
static void sp_connected(struct bt_conn *conn, uint8_t err);
static void stop_adv_timer_callback(struct k_timer *timer_id);
static void start_stop_adv(struct k_work *work);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct single_peripheral sp;

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
SYS_INIT(single_peripheral_initialize, APPLICATION,
	 CONFIG_SINGLE_PERIPHERAL_INIT_PRIORITY);

struct bt_conn *single_peripheral_get_conn(void)
{
	return sp.conn_handle;
}

void single_peripheral_start_advertising(void)
{
	if (!sp.initialized) {
		LOG_ERR("Single Peripheral not initialized");
	} else {
		sp.start = true;
		k_work_submit(&sp.work);
	}
}

void single_peripheral_stop_advertising(void)
{
	if (!sp.initialized) {
		LOG_ERR("Single Peripheral not initialized");
	} else {
		sp.start = false;
		k_work_submit(&sp.work);
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int single_peripheral_initialize(const struct device *device)
{
	ARG_UNUSED(device);
	int r = 0;

	if (!sp.initialized) {
		sp.conn_callbacks.connected = sp_connected;
		sp.conn_callbacks.disconnected = sp_disconnected;
		bt_conn_cb_register(&sp.conn_callbacks);
		k_timer_init(&sp.timer, stop_adv_timer_callback, NULL);
		k_work_init(&sp.work, start_stop_adv);

		sp.initialized = true;

#ifdef CONFIG_SINGLE_PERIPHERAL_ADV_ON_INIT
		single_peripheral_start_advertising();
#endif

	} else {
		r = -EPERM;
	}

	if (r < 0) {
		LOG_ERR("Initialization error");
	}
	return r;
}

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
		sp.conn_handle = NULL;
	} else {
		LOG_INF("Connected central: %s", log_strdup(addr));
		sp.conn_handle = bt_conn_ref(conn);

		/* Stop advertising so another central cannot connect. */
		single_peripheral_stop_advertising();
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
	sp.conn_handle = NULL;

	/* Restart advertising because disconnect may have been unexpected. */
	single_peripheral_start_advertising();
}

/* Workqueue allows start/stop to be called from interrupt context. */
static void start_stop_adv(struct k_work *work)
{
	ARG_UNUSED(work);
	int err = 0;

	if (sp.start) {
		if (sp.conn_handle != NULL) {
			LOG_INF("Cannot start advertising while connected");
			err = -EPERM;
		} else if (!sp.advertising) {
			err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, AD,
					      ARRAY_SIZE(AD), NULL, 0);

			LOG_INF("Advertising start status: %d", err);

			if (err >= 0) {
				sp.advertising = true;
			}

		} else {
			LOG_INF("Advertising duration timer restarted");
		}

		if (err >= 0 && sp.start && (ADV_DURATION != 0)) {
			k_timer_start(&sp.timer, K_SECONDS(ADV_DURATION),
				      K_NO_WAIT);
		}
		sp.start = false;

	} else {
		k_timer_stop(&sp.timer);
		err = bt_le_adv_stop();
		LOG_INF("Advertising stop status: %d", err);
		sp.advertising = false;
	}
}

static void stop_adv_timer_callback(struct k_timer *dummy)
{
	ARG_UNUSED(dummy);

	single_peripheral_stop_advertising();
}
