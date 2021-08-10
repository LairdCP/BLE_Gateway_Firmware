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
#include "attr.h"
#include "led_configuration.h"
#include "errno_str.h"
#include "single_peripheral.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
static const struct bt_data AD[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, DFU_SMP_UUID_SERVICE),
};

#if defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
static const struct lcz_led_blink_pattern LED_ADVERTISING_PATTERN = {
	.on_time = CONFIG_DEFAULT_LED_ON_TIME_FOR_1_SECOND_BLINK,
	.off_time = CONFIG_DEFAULT_LED_OFF_TIME_FOR_1_SECOND_BLINK,
	.repeat_count = REPEAT_INDEFINITELY
};
#endif

#define ADV_DURATION CONFIG_SINGLE_PERIPHERAL_ADV_DURATION_SECONDS

#define PAIRING_COMPLETE_TIMEOUT_MS 30000
#define PAIRING_FAILURE_DISCONNECT_DELAY_MS 500

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int single_peripheral_initialize(const struct device *device);
static void sp_disconnected(struct bt_conn *conn, uint8_t reason);
static void sp_connected(struct bt_conn *conn, uint8_t err);
static void sp_le_param_updated(struct bt_conn *conn, uint16_t interval,
				uint16_t latency, uint16_t timeout);
static void stop_adv_timer_callback(struct k_timer *timer_id);
static void start_stop_adv(struct k_work *work);

#ifdef CONFIG_MCUMGR_SMP_BT_AUTHEN
static void disconnect_req(const char *reason);
static void security_failed_handler(struct k_work *work);
static void pairing_timeout_handler(struct k_work *work);

static int setup_security(void);
static void pairing_complete(struct bt_conn *conn, bool bonded);
static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason);
#endif

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
	struct k_work_delayable conn_work;
	struct k_work_delayable pair_work;
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

bool single_peripheral_security_busy(void)
{
	if (IS_ENABLED(CONFIG_MCUMGR_SMP_BT_AUTHEN)) {
		return sp.advertising || (sp.conn_handle != NULL && !sp.paired);
	} else {
		return false;
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
		sp.conn_callbacks.le_param_updated = sp_le_param_updated;
		bt_conn_cb_register(&sp.conn_callbacks);
		k_timer_init(&sp.timer, stop_adv_timer_callback, NULL);
		k_work_init(&sp.adv_work, start_stop_adv);
#ifdef CONFIG_MCUMGR_SMP_BT_AUTHEN
		k_work_init_delayable(&sp.conn_work, security_failed_handler);
		k_work_init_delayable(&sp.pair_work, pairing_timeout_handler);
#endif
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
	if (!lbt_peripheral_role(conn)) {
		return;
	}

	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_ERR("Central device failed to connect %s (%u %s)",
			log_strdup(addr), err, lbt_get_hci_err_string(err));
		bt_conn_unref(conn);
		sp.conn_handle = NULL;
	} else {
		LOG_INF("Connected central: %s", log_strdup(addr));
		sp.conn_handle = bt_conn_ref(conn);

		/* Stop advertising so another central cannot connect. */
		single_peripheral_stop_advertising();

		/* If CONFIG_MCUMGR_SMP_BT_AUTHEN is true,
		 * then it will cause encryption to be started when
		 * the SMP service is accessed.
		 */

		/* Close connection if pairing isn't completed. */
		if (IS_ENABLED(CONFIG_MCUMGR_SMP_BT_AUTHEN)) {
			k_work_reschedule(&sp.pair_work,
					  K_MSEC(PAIRING_COMPLETE_TIMEOUT_MS));
		}

#if defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
		/* Turn LED on to indicate in a connection */
		lcz_led_turn_on(BLUETOOTH_ADVERTISING_LED);
#endif
	}
}

static void sp_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (!lbt_peripheral_role(conn)) {
		return;
	}

	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected central: %s (reason: %u %s)", log_strdup(addr),
		reason, lbt_get_hci_err_string(reason));
	bt_conn_unref(conn);
	sp.conn_handle = NULL;
	sp.paired = false;
	sp.bonded = false;

	/* Restart advertising because disconnect may have been unexpected. */
	single_peripheral_start_advertising();
}

static void sp_le_param_updated(struct bt_conn *conn, uint16_t interval,
				uint16_t latency, uint16_t timeout)
{
	if (!lbt_peripheral_role(conn)) {
		return;
	}

	LOG_DBG("Connection Parameters: "
		"Interval %d ms, Latency %d s, Timeout %d ms",
		interval * 100 / 125, latency, timeout * 10);
}

/* Workqueue allows start/stop to be called from interrupt context. */
static void start_stop_adv(struct k_work *work)
{
	ARG_UNUSED(work);
	int err = 0;

	if (sp.start) {
		if (sp.conn_handle != NULL) {
			/* Technically possible - not desired for this application. */
			LOG_INF("Cannot start advertising while connected");
			err = -EPERM;
		} else if (!sp.advertising) {
			/* Security callbacks can be overridden by sensor task
			 * when peripheral is idle.
			 */
#ifdef CONFIG_MCUMGR_SMP_BT_AUTHEN
			err = setup_security();
#endif

			if (err == 0) {
				err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, AD,
						      ARRAY_SIZE(AD), NULL, 0);

				LOG_INF("Advertising start status: %d", err);

				if (err >= 0) {
					sp.advertising = true;

#if defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
					/* Blink LED to indicate advertising */
					lcz_led_blink(BLUETOOTH_ADVERTISING_LED,
						      &LED_ADVERTISING_PATTERN);
#endif
				}
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

#if defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
		if (err == 0) {
			/* Turn LED off to indicate not advertising */
			lcz_led_turn_off(BLUETOOTH_ADVERTISING_LED);
		}
#endif
	}
}

static void stop_adv_timer_callback(struct k_timer *dummy)
{
	ARG_UNUSED(dummy);

	single_peripheral_stop_advertising();
}

#ifdef CONFIG_MCUMGR_SMP_BT_AUTHEN
static int setup_security(void)
{
	int r;

	static const struct bt_conn_auth_cb callbacks = {
		.pairing_complete = pairing_complete,
		.pairing_failed = pairing_failed
	};

	r = bt_passkey_set(
		attr_get_uint32(ATTR_ID_passkey, BT_PASSKEY_INVALID));
	if (r < 0) {
		LOG_ERR("Unable to set passkey: %d", r);
	} else {
		/* With multiple users callbacks must be cleared first. */
		r = bt_conn_auth_cb_register(NULL);
		if (r == 0) {
			r = bt_conn_auth_cb_register(&callbacks);
		}
		if (r < 0) {
			LOG_ERR("Unable to register security callbacks %d", r);
		}
	}

	return r;
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	if (conn == sp.conn_handle) {
		sp.paired = true;
		sp.bonded = bonded;

		LOG_DBG("Pairing complete: bonded: %s", bonded ? "yes" : "no");
	}
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	ARG_UNUSED(reason);

	if (conn == sp.conn_handle) {
		/* Don't call disconnect in callback from BT thread because it
		 * prevents stack from cleaning up nicely.
		 */
		k_work_reschedule(&sp.conn_work,
				  K_MSEC(PAIRING_FAILURE_DISCONNECT_DELAY_MS));
		sp.paired = false;
		sp.bonded = false;

		LOG_DBG("Pairing failed: reason: %u %s", reason,
			lbt_get_security_err_string(reason));
	}
}

static void disconnect_req(const char *reason)
{
	int r;

	if (sp.conn_handle != NULL) {
		r = bt_conn_disconnect(sp.conn_handle, BT_HCI_ERR_AUTH_FAIL);
		LOG_DBG("%s disconnect status: %d %s", reason, r,
			r == 0 ? "Success" : errno_str_get(r));
	}
}

static void security_failed_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	disconnect_req("Security failed");
}

static void pairing_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!sp.paired) {
		disconnect_req("Pairing timeout");
	}
}
#endif
