/**
 * @file single_peripheral.c
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
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
#include <bluetooth/services/dfu_smp.h>

#include "lcz_bluetooth.h"
#include "lcz_bt_security.h"
#include "single_peripheral.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
static const struct bt_data AD[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, DFU_SMP_UUID_SERVICE),
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

static int encrypt_link(void);
static void disconnect_req_handler(struct k_work *work);

static int register_security_callbacks(void);
static void passkey_display(struct bt_conn *conn, unsigned int passkey);
static void passkey_confirm(struct bt_conn *conn, unsigned int passkey);
static void pairing_complete(struct bt_conn *conn, bool bonded);
static void cancel(struct bt_conn *conn);
static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct {
	bool initialized;
	bool advertising;
	bool start;
	bool paired;
	bool bonded;
	struct bt_conn *conn_handle;
	struct bt_conn_cb conn_callbacks;
	struct k_timer timer;
	struct k_work adv_work;
	struct k_delayed_work conn_work;
} sp;

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
		k_work_submit(&sp.adv_work);
	}
}

void single_peripheral_stop_advertising(void)
{
	if (!sp.initialized) {
		LOG_ERR("Single Peripheral not initialized");
	} else {
		sp.start = false;
		k_work_submit(&sp.adv_work);
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
		k_work_init(&sp.adv_work, start_stop_adv);
		k_delayed_work_init(&sp.conn_work, disconnect_req_handler);
		sp.initialized = true;

		r = register_security_callbacks();

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
	if (!lbt_peripheral_role(conn)) {
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

		encrypt_link();
	}
}

/* L3 is selected, but the shell (display) may not be enabled/connected. */
static int encrypt_link(void)
{
	int status = bt_conn_set_security(sp.conn_handle, BT_SECURITY_L3);

	LOG_DBG("%d", status);

	return status;
}

static void sp_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (!lbt_peripheral_role(conn)) {
		return;
	}

	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected central: %s (reason %u)", log_strdup(addr),
		reason);
	bt_conn_unref(conn);
	sp.conn_handle = NULL;
	sp.paired = false;
	sp.bonded = false;

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

static int register_security_callbacks(void)
{
	int r;

	static const struct bt_conn_auth_cb callbacks = {
		.passkey_display = passkey_display,
		.passkey_confirm = passkey_confirm,
		.cancel = cancel,
		.pairing_complete = pairing_complete,
		.pairing_failed = pairing_failed
	};

	r = lcz_bt_security_register_cb(&callbacks);
	if (r < 0) {
		LOG_ERR("Unable to register security callbacks");
	}

	return r;
}

static void passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	if (conn == sp.conn_handle) {
		LOG_WRN("pairing passkey: %d", passkey);
	}
}

/* The gateway only advertises after the button is pressed.
 *
 * The gateway doesn't require a button press or the value to be viewed on the
 * terminal - it bypasses the MITM protection.
 */
static void passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	int r;

	if (conn == sp.conn_handle) {
		LOG_WRN("passkey: %d", passkey);
		r = bt_conn_auth_passkey_confirm(conn);
		LOG_DBG("status: %d", r);
	}
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	if (conn == sp.conn_handle) {
		sp.paired = true;
		sp.bonded = bonded;
	}
}

static void cancel(struct bt_conn *conn)
{
	if (conn == sp.conn_handle) {
		LOG_DBG("Pairing cancelled");
	}
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	ARG_UNUSED(reason);

	if (conn == sp.conn_handle) {
		/* Don't call disconnect in callback from BT thread because it
		 * prevents stack from cleaning up nicely.
		 */
		k_delayed_work_submit(&sp.conn_work, K_MSEC(500));
		sp.paired = false;
		sp.bonded = false;
	}
}

static void disconnect_req_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	int r = bt_conn_disconnect(sp.conn_handle, BT_HCI_ERR_AUTH_FAIL);
	LOG_DBG("Security failed disconnect status: %d", r);
}