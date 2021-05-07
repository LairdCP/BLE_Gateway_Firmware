/**
 * @file ct_ble.c
 * @brief BLE portion for Contact Tracing
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(ct_ble, CONFIG_CT_BLE_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <bluetooth/bluetooth.h>
#include <version.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <sys/crc.h>
#include <random/rand32.h>

#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <mgmt/mcumgr/smp_bt.h>
#include <mgmt/mcumgr/buf.h>
#include <os_mgmt/os_mgmt.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_vs.h>

#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>
#include "cborattr/cborattr.h"
#include "mgmt/mgmt.h"
#include "fs_mgmt/fs_mgmt_config.h"
#include "fs_mgmt/fs_mgmt.h"

#include <crypto/cipher.h>

#include "app_version.h"
#include "dfu_smp_c.h"
#include "ble_cellular_service.h"
#include "laird_utility_macros.h"
#include "lcz_bluetooth.h"
#include "lcz_sensor_adv_format.h"
#include "lcz_software_reset.h"
#include "led_configuration.h"
#include "ad_find.h"
#include "lcz_bt_scan.h"
#include "ct_datalog.h"
#include "lcz_qrtc.h"
#include "nv.h"
#include "ble_aws_service.h"
#include "aws.h"
#include "lte.h"
#include "bluegrass.h"
#include "ble_sensor_service.h"

#include "ct_ble.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifdef CONFIG_CT_DEBUG_SMP_TRANSFERS
#define LOG_SMP(...) LOG_DBG(...)
#else
#define LOG_SMP(...)
#endif

#ifdef CONFIG_CT_VERBOSE
#define LOG_VRB(...) LOG_DBG(...)
#else
#define LOG_VRB(...)
#endif

enum adv_type { ADV_TYPE_NONCONN = 0, ADV_TYPE_CONN };

enum aws_publish_state {
	/* CT log download has not yet started */
	AWS_PUBLISH_STATE_NONE = 0,
	/* publish has been issued and awaiting result */
	AWS_PUBLISH_STATE_PENDING,
	AWS_PUBLISH_STATE_SUCCESS,
	AWS_PUBLISH_STATE_FAIL
};

#define SEND_TO_AWS_TIMEOUT_TICKS K_SECONDS(5)
#define AWS_TOPIC_UP_SUFFIX "/up"
#define AWS_TOPIC_LOG_SUFFIX "/log"

#define DISCOVER_SERVICES_DELAY_TICKS K_MSEC(5)

#define SMP_TIMEOUT_TICKS K_SECONDS(10)

/* 2 minutes per entry */
#define STASH_ENTRY_FAILURE_CNT_MAX 24

#define SENSOR_CONNECTION_TIMEOUT_TICKS K_SECONDS(10)

#define BT_GAP_INIT_CONN_INT_MIN_CT 6
#define BT_GAP_INIT_CONN_INT_MAX_CT 20
#define BT_LE_CONN_PARAM_CT                                                    \
	BT_LE_CONN_PARAM(BT_GAP_INIT_CONN_INT_MIN_CT,                          \
			 BT_GAP_INIT_CONN_INT_MAX_CT, 0, 25)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, 0x36, 0xa3, 0x4d, 0x40, 0xb6, 0x70,
		      0x69, 0xa6, 0xb1, 0x4e, 0x84, 0x9e, 0x60, 0x7c, 0x78,
		      0x43),
};

static const struct lcz_led_blink_pattern CT_LED_SENSOR_SEARCH_PATTERN = {
	.on_time = 75,
	.off_time = 4925,
	.repeat_count = REPEAT_INDEFINITELY
};

static const struct lcz_led_blink_pattern
	CT_LED_SENSOR_SEARCH_CONNECTABLE_PATTERN = {
		.on_time = 75,
		.off_time = 925,
		.repeat_count = REPEAT_INDEFINITELY
	};

struct smp_buffer {
	struct dfu_smp_header header;
	uint8_t payload[CONFIG_MCUMGR_BUF_SIZE];
} __packed;

BUILD_ASSERT((AES_CBC_IV_SIZE % 4) == 0, "IV must be a multiple of 4");

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void disconnected(struct bt_conn *conn, uint8_t reason);
static void connected(struct bt_conn *conn, uint8_t err);
static void sensor_disconnected(struct bt_conn *conn, uint8_t reason);
static void sensor_connected(struct bt_conn *conn, uint8_t err);
static void sensor_disconnect_cleanup(struct bt_conn *conn);

static void discover_services_work_callback(struct k_work *work);
static void discover_failed_handler(struct bt_conn *conn, int err);

static int start_advertising(void);

static uint8_t discover_func_smp(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 struct bt_gatt_discover_params *params);
static void update_advert_timer_handler(struct k_timer *dummy);
static void update_advert(struct k_work *work);
static void smp_challenge_req_work_handler(struct k_work *work);
static void smp_fs_download_work_handler(struct k_work *work);
static void change_advert_type_work_handler(struct k_work *work);
static void send_stashed_entries_work_handler(struct k_work *work);
static void ResetEntryStashInformation(bool giveSemaphore);
static int send_smp_challenge_request(struct bt_gatt_dfu_smp_c *dfu_smp_c,
				      const char *filename, uint32_t offset);
static int send_smp_challenge_response(struct bt_gatt_dfu_smp_c *dfu_smp_c,
				       const char *filename, uint32_t offset,
				       uint8_t *pData, uint32_t dataLen);
static int send_smp_download_request(struct bt_gatt_dfu_smp_c *dfu_smp_c,
				     const char *filename, uint32_t offset);
static void smp_xfer_timeout_handler(struct k_timer *dummy);
static int calculate_record_bytes_in_entry(uint8_t *buf, uint16_t record_size,
					   uint16_t len);
static bool is_encryption_enabled(void);
static uint32_t validate_hw_compatibility(const struct device *dev);
static uint32_t encrypt_cbc(uint8_t *data, uint32_t dataLen, uint8_t *encrypted,
			    uint32_t encrypted_data_len_max, uint8_t *key,
			    uint8_t key_size);
static uint32_t decrypt_cbc(uint8_t *encrypted, uint32_t encrypted_data_len,
			    uint8_t *decrypted, uint32_t decrypted_data_len_max,
			    uint8_t *key, uint8_t key_size);

static void sensor_scan_conn_init();
static void sensor_att_timeout_callback(struct k_work *work);
static void sensor_disconnected(struct bt_conn *conn, uint8_t reason);
static void sensor_conn_timeout_handler(struct k_timer *dummy);
static void ct_sensor_adv_handler(const bt_addr_le_t *addr, int8_t rssi,
				  uint8_t type, struct net_buf_simple *ad);

static bool find_ct_ad(AdHandle_t *handle);
static bool valid_ct_record_type(uint8_t type);

static void sensor_att_timeout_callback(struct k_work *work);

static void set_ble_state(enum sensor_state state);

static void change_advert_type(enum adv_type adv_type);
static void disconnect_sensor(void);

static void ct_adv_watchdog_work_handler(struct k_work *work);
static bool ct_ble_remote_active_handler(void);
static void ct_conn_inactivity_work_handler(struct k_work *work);
static void disable_connectable_adv_work_handler(struct k_work *work);

static void adv_log_filter(const char *msg);

static void aws_work_handler(struct k_work *item);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static uint8_t log_buffer[CONFIG_CT_LOG_DOWNLOAD_BUFFER_SIZE];
static struct smp_buffer smp_rsp_buff;
static char smp_fs_download_filename[FS_MGMT_PATH_SIZE + 1];
static uint8_t file_data[FS_MGMT_DL_CHUNK_SIZE];

static struct k_work sensor_att_timeout_work;
static struct k_timer sensor_conn_timeout_timer;

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static struct bt_conn_cb sensor_callbacks = {
	.connected = sensor_connected,
	.disconnected = sensor_disconnected,
};

static struct bt_conn *central_conn;

static struct bt_gatt_discover_params dp;
static struct bt_gatt_subscribe_params sp;
static struct bt_gatt_exchange_params mp;
static struct bt_gatt_dfu_smp_c dfu_smp_c;

static struct {
	enum sensor_state app_state;
	uint16_t smp_service_handle;
	struct bt_gatt_subscribe_params smp_subscribe_params;
	uint32_t inactivity;
	struct k_delayed_work inactivity_work;
	uint16_t mtu;
	struct bt_conn *conn;
	bool encrypt_req;
	bool log_ble_xfer_active;
} remote;

static struct k_work discover_services_work;
static struct k_work update_advert_work;
static struct k_work smp_challenge_req_work;
static struct k_work smp_fs_download_work;
static struct k_work change_advert_type_work;
static struct k_work send_stashed_entries_work;
static struct k_delayed_work ct_adv_watchdog;
static struct k_delayed_work disable_connectable_adv_work;

static struct k_sem sending_to_aws_sem;

static struct {
	struct k_work work;
	uint8_t buf[CONFIG_CT_AWS_BUF_SIZE];
	size_t buf_len;
} aws_work;

static struct k_timer update_advert_timer;
static struct k_timer smp_xfer_timeout_timer;

static LczContactTracingAd_t ct_mfg_data = {
	.companyId = LAIRD_CONNECTIVITY_MANUFACTURER_SPECIFIC_COMPANY_ID1,
	.protocolId = CT_GATEWAY_AD_PROTOCOL_ID,
	.networkId = CT_DEFAULT_NETWORK_ID,
	.flags = 0,
	.addr = { { 0, 0, 0, 0, 0, 0 } },
	.recordType = CT_ADV_REC_TYPE_V00,
	.deviceType = 0,
	.epoch = 0,
	.txPower = 0,
	.motionMagnitude = 0,
	.modelId = LCZ_SENSOR_MODEL_ID_MG100,
	.reserved_1 = 0,
	.reserved_2 = 0,
	.reserved_3 = 0
};

static const struct bt_data contact_tracing_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, &ct_mfg_data, sizeof(ct_mfg_data))
};

static struct {
	bool available;
	uint8_t buffer[CONFIG_CT_LOG_DOWNLOAD_BUFFER_SIZE +
		       sizeof(struct ct_log_header_v2)];
	uint32_t len;
	uint32_t idx;
	uint8_t timeouts;
	uint8_t failure_cnt;
	uint16_t prev_ent_size;
} stashed_entries;

static struct {
	bool ble_initialized;
	enum adv_type adv_type;
	int scan_id;
	uint32_t all_ads;
	uint32_t ads;
	enum aws_publish_state aws_publish_state;
	bool log_publishing;
	uint32_t num_connections;
	uint32_t num_download_starts;
	uint32_t num_download_completions;
	uint8_t up_topic[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t log_topic[CONFIG_AWS_TOPIC_MAX_SIZE];
} ct;

/* Must be 16 bytes (IV size) greater than challenge (plaintext),
 * which is assumed to be 64 bytes
 */
static uint8_t challenge_rsp[80];
static uint8_t challenge_rsp_len;

static const struct device *crypto_dev;
static uint32_t crypto_cap_flags;

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/

void ct_ble_initialize(void)
{
	int err;

	if (ct.ble_initialized) {
		LOG_DBG("CT BLE already initialized");
		return;
	}

	sensor_scan_conn_init();

	bt_conn_cb_register(&conn_callbacks);

	k_timer_init(&update_advert_timer, update_advert_timer_handler, NULL);
	k_timer_init(&smp_xfer_timeout_timer, smp_xfer_timeout_handler, NULL);

	k_work_init(&discover_services_work, discover_services_work_callback);
	k_work_init(&update_advert_work, update_advert);
	k_work_init(&smp_challenge_req_work, smp_challenge_req_work_handler);
	k_work_init(&smp_fs_download_work, smp_fs_download_work_handler);
	k_work_init(&change_advert_type_work, change_advert_type_work_handler);
	k_work_init(&send_stashed_entries_work,
		    send_stashed_entries_work_handler);
	k_sem_init(&sending_to_aws_sem, 1, 1);
	k_work_init(&aws_work.work, aws_work_handler);
	k_delayed_work_init(&ct_adv_watchdog, ct_adv_watchdog_work_handler);
	k_delayed_work_init(&remote.inactivity_work,
			    ct_conn_inactivity_work_handler);

	if (CONFIG_CT_CONN_INACTIVITY_TICK_RATE_SECONDS != 0) {
		k_delayed_work_submit(
			&remote.inactivity_work,
			K_SECONDS(CONFIG_CT_CONN_INACTIVITY_TICK_RATE_SECONDS));
	}

	k_delayed_work_init(&disable_connectable_adv_work,
			    disable_connectable_adv_work_handler);

	err = nvReadBleNetworkId(&ct_mfg_data.networkId);
	if (err <= 0) {
		ct_mfg_data.networkId = CT_DEFAULT_NETWORK_ID;
	}

	crypto_dev = device_get_binding(CONFIG_CRYPTO_TINYCRYPT_SHIM_DRV_NAME);
	if (crypto_dev != NULL) {
		crypto_cap_flags = validate_hw_compatibility(crypto_dev);
	} else {
		LOG_ERR("Crypto should be enabled");
	}

	ct_ble_topic_builder();

	ct.adv_type = ADV_TYPE_NONCONN;
	start_advertising();

	/* Initialize the state to 'looking for device' */
	set_ble_state(BT_DEMO_APP_STATE_FINDING_DEVICE);

	ct.ble_initialized = true;
}

void ct_ble_set_network_id(uint16_t nwkId)
{
	/* set in nv by fs_intercept */
	ct_mfg_data.networkId = nwkId;
	LOG_DBG("Set networkId: %04X", ct_mfg_data.networkId);
}

void ct_ble_check_stashed_log_entries(void)
{
	if (ct.ble_initialized) {
		k_work_submit(&send_stashed_entries_work);
	} else {
		LOG_ERR("CT BLE not initialized");
	}
}

bool ct_ble_is_publishing_log(void)
{
	return ct.log_publishing;
}

bool ct_ble_get_log_transfer_active_flag(void)
{
	return remote.log_ble_xfer_active;
}

bool ct_ble_is_connected_to_sensor(void)
{
	return (remote.conn != NULL);
}

bool ct_ble_is_connected_to_central(void)
{
	return (central_conn != NULL);
}

uint32_t ct_ble_get_num_connections(void)
{
	return ct.num_connections;
}

uint32_t ct_ble_get_num_ct_dl_starts(void)
{
	return ct.num_download_starts;
}

uint32_t ct_ble_get_num_download_completes(void)
{
	return ct.num_download_completions;
}

uint32_t ct_ble_get_num_scan_results(void)
{
	return ct.all_ads;
}

uint32_t ct_ble_get_num_ct_scan_results(void)
{
	return ct.ads;
}

int ct_adv_on_button_isr(void)
{
	int r = -EPERM;

	LOG_DBG(".");

	if (!ct.ble_initialized) {
		LOG_ERR("Init CT BLE first");
		return r;
	}

	if (central_conn == NULL) {
		if (CONFIG_CT_CONNECTABLE_ADV_DURATION_SECONDS != 0) {
			r = k_delayed_work_submit(
				&disable_connectable_adv_work,
				K_SECONDS(
					CONFIG_CT_CONNECTABLE_ADV_DURATION_SECONDS));
		}
		change_advert_type(ADV_TYPE_CONN);
	} else {
		LOG_WRN("ignoring button, central already connected");
		r = 0;
	}

	return r;
}

void ct_ble_topic_builder(void)
{
	struct lte_status *lte_status = lteGetStatus();

	snprintf(ct.up_topic, sizeof(ct.up_topic), "%s%s%s",
		 aws_svc_get_topic_prefix(), lte_status->IMEI,
		 AWS_TOPIC_UP_SUFFIX);

	LOG_DBG("Pub Topic: %s", log_strdup(ct.up_topic));

	snprintf(ct.log_topic, sizeof(ct.log_topic), "%s%s%s",
		 aws_svc_get_topic_prefix(), lte_status->IMEI,
		 AWS_TOPIC_LOG_SUFFIX);
}

char *ct_ble_get_log_topic(void)
{
	return ct.log_topic;
}

int ct_ble_publish_dummy_data_to_aws(void)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	struct ct_publish_header_t pub_hdr;
	uint8_t buf[sizeof(struct ct_publish_header_t) +
		    CONFIG_CT_AWS_DUMMY_ENTRY_SIZE];
	size_t buf_len;
	size_t cnt;

	LOG_DBG(".");

	bt_id_get(addrs, &cnt);

	uint32_t currTime = lcz_qrtc_get_epoch();
	uint8_t dummyEntry[CONFIG_CT_AWS_DUMMY_ENTRY_SIZE] = {
		0x00,
		0x00,
		addrs[0].a.val[0],
		addrs[0].a.val[1],
		addrs[0].a.val[2],
		addrs[0].a.val[3],
		addrs[0].a.val[4],
		addrs[0].a.val[5],
		(currTime & 0xFF),
		((currTime >> 8) & 0xFF),
		((currTime >> 16) & 0xFF),
		((currTime >> 24) & 0xFF),
		0x18,
		0x00,
		0x11,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00
	};

	/* fill in the ct_publish_header_t */
	pub_hdr.entry_protocol_version = LOG_ENTRY_PROTOCOL_V2;
	memcpy(pub_hdr.device_id, addrs[0].a.val, BT_MAC_ADDR_LEN);
	pub_hdr.device_time = currTime;
	pub_hdr.last_upload_time = 0;
	pub_hdr.fw_version[0] = APP_VERSION_MAJOR;
	pub_hdr.fw_version[1] = APP_VERSION_MINOR;
	pub_hdr.fw_version[2] = APP_VERSION_PATCH;
	pub_hdr.fw_version[3] = APP_VERSION_PATCH >> 8;
	pub_hdr.battery_level = 0;
	pub_hdr.network_id = ct_mfg_data.networkId;

	memcpy(buf, &pub_hdr, sizeof(struct ct_publish_header_t));
	memcpy(&buf[sizeof(struct ct_publish_header_t)], dummyEntry,
	       sizeof(dummyEntry));
	buf_len = sizeof(struct ct_publish_header_t) + sizeof(dummyEntry);

	return awsSendBinData(buf, buf_len, ct.up_topic);
}

enum sensor_state ct_ble_get_state(void)
{
	return remote.app_state;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void mtu_callback(struct bt_conn *conn, uint8_t err,
			 struct bt_gatt_exchange_params *params)
{
	int r;

	if (conn == remote.conn && conn != NULL) {
		remote.mtu = bt_gatt_get_mtu(conn);
		LOG_VRB("MTU: %u", remote.mtu);
		if (remote.mtu) {
			/* Update discovery parameters before initiating discovery. */
			dp.uuid = NULL;
			dp.func = discover_func_smp;
			dp.start_handle = 0x0001;
			dp.end_handle = 0xffff;
			dp.type = BT_GATT_DISCOVER_PRIMARY;

			r = bt_gatt_discover(remote.conn, &dp);
			if (r) {
				discover_failed_handler(remote.conn, r);
			}
		}
	}
}

static int exchange_mtu(struct bt_conn *conn)
{
	mp.func = mtu_callback;
	return bt_gatt_exchange_mtu(conn, &mp);
}

static void discover_services_work_callback(struct k_work *work)
{
	ARG_UNUSED(work);

	exchange_mtu(remote.conn);
}

static void discover_failed_handler(struct bt_conn *conn, int err)
{
	LOG_ERR("Discover failed (err %d)", err);
	if (conn) {
		/* couldn't discover something, disconnect */
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static uint8_t discover_func_smp(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 struct bt_gatt_discover_params *params)
{
	int err;
	struct bt_gatt_service_val *gatt_service;

	LOG_VRB("in discover_func_smp, conn->index is %d (%x)", bci,
		POINTER_TO_UINT(conn));

	if (attr == NULL) {
		LOG_VRB("Discover complete");
		return BT_GATT_ITER_STOP;
	}

	gatt_service = (struct bt_gatt_service_val *)attr->user_data;

	if (bt_uuid_cmp(gatt_service->uuid, DFU_SMP_UUID_SERVICE) == 0) {
		LOG_VRB("Found SMP service (handle: %d)", attr->handle);
		dp.uuid = NULL;
		dp.start_handle = LBT_NEXT_HANDLE_AFTER_SERVICE(attr->handle);
		dp.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &dp);
		if (err) {
			LOG_ERR("Discover failed (err %d)", err);
		}
		return BT_GATT_ITER_STOP;
	} else if (bt_uuid_cmp(gatt_service->uuid, DFU_SMP_UUID_CHAR) == 0) {
		LOG_VRB("Found SMP characteristic (value handle: %d)",
			LBT_NEXT_HANDLE_AFTER_SERVICE(attr->handle));
		dfu_smp_c.handles.smp =
			LBT_NEXT_HANDLE_AFTER_SERVICE(attr->handle);
		dp.uuid = BT_UUID_GATT_CCC;
		dp.start_handle = LBT_NEXT_HANDLE_AFTER_CHAR(attr->handle);
		dp.type = BT_GATT_DISCOVER_DESCRIPTOR;
		sp.value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, &dp);
		if (err) {
			LOG_ERR("Discover failed (err %d)", err);
		}
		return BT_GATT_ITER_STOP;
	} else if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
		dfu_smp_c.conn = conn;
		dfu_smp_c.notification_params.notify = bt_gatt_dfu_smp_c_notify;
		dfu_smp_c.notification_params.value = BT_GATT_CCC_NOTIFY;
		dfu_smp_c.handles.smp_ccc = attr->handle;
		dfu_smp_c.notification_params.value_handle =
			dfu_smp_c.handles.smp;
		dfu_smp_c.notification_params.ccc_handle =
			dfu_smp_c.handles.smp_ccc;
		atomic_set_bit(dfu_smp_c.notification_params.flags,
			       BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

		err = bt_gatt_subscribe(conn, &dfu_smp_c.notification_params);
		if (err && err != -EALREADY) {
			LOG_ERR("Subscribe failed (err %d)", err);
		} else {
			/* now, send a download command to grab the log */
			set_ble_state(
				BT_DEMO_APP_STATE_CONNECTED_AND_CONFIGURED);

			if (is_encryption_enabled()) {
				k_work_submit(&smp_challenge_req_work);
			} else {
				remote.encrypt_req = false;
				k_work_submit(&smp_fs_download_work);
			}
		}

		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_CONTINUE;
}

static int start_advertising(void)
{
	int err = 0;
	bool commissioned;
	nvReadCommissioned(&commissioned);

	/* Advertise using the MG100 ad format */
	if (!commissioned) {
		/* stop the advert update timer in case it is running */
		k_timer_stop(&update_advert_timer);

		/* advertise the cellular service for compatibility with Pinnacle 100 mobile app */
		if (ct.adv_type == ADV_TYPE_CONN) {
			err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad,
					      ARRAY_SIZE(ad), NULL, 0);
			lcz_led_blink(
				BLUETOOTH_LED,
				&CT_LED_SENSOR_SEARCH_CONNECTABLE_PATTERN);
		} else {
			err = bt_le_adv_start(
				BT_LE_ADV_PARAM(
					BT_LE_ADV_OPT_USE_NAME |
						BT_LE_ADV_OPT_USE_IDENTITY,
					BT_GAP_ADV_FAST_INT_MIN_2,
					BT_GAP_ADV_FAST_INT_MAX_2, NULL),
				ad, ARRAY_SIZE(ad), NULL, 0);
			lcz_led_blink(BLUETOOTH_LED, &CT_LED_SENSOR_SEARCH_PATTERN);
		}
	} else {
		/* Advertise using the MG100 CT ad format */
		if (ct.adv_type == ADV_TYPE_CONN) {
			err = bt_le_adv_start(BT_LE_ADV_CONN_NAME,
					      contact_tracing_ad,
					      ARRAY_SIZE(contact_tracing_ad),
					      NULL, 0);
			lcz_led_blink(
				BLUETOOTH_LED,
				&CT_LED_SENSOR_SEARCH_CONNECTABLE_PATTERN);
		} else {
			err = bt_le_adv_start(
				BT_LE_ADV_PARAM(
					BT_LE_ADV_OPT_USE_NAME |
						BT_LE_ADV_OPT_USE_IDENTITY,
					BT_GAP_ADV_FAST_INT_MIN_2,
					BT_GAP_ADV_FAST_INT_MAX_2, NULL),
				contact_tracing_ad,
				ARRAY_SIZE(contact_tracing_ad), NULL, 0);
			lcz_led_blink(BLUETOOTH_LED, &CT_LED_SENSOR_SEARCH_PATTERN);
		}

		k_timer_start(&update_advert_timer,
			      K_MSEC(CONFIG_CT_AD_RATE_MS),
			      K_MSEC(CONFIG_CT_AD_RATE_MS));
	}

	if (ct.adv_type == ADV_TYPE_CONN) {
		LOG_DBG("adv-conn status: %s", lbt_get_hci_err_string(err));
	} else {
		LOG_DBG("adv-nonconn status: %s", lbt_get_hci_err_string(err));
	}

	return err;
}

/* This callback is triggered when a BLE connection occurs.
 * This handles when the gateway is the peripheral (a central has connected).
 */
static void connected(struct bt_conn *conn, uint8_t err)
{
	if (conn == remote.conn) {
		return;
	}

	if (err || conn == NULL) {
		goto fail;
	}

	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	/* In this case a central device connected to us */
	LOG_INF("Connected central: %s", log_strdup(addr));
	central_conn = bt_conn_ref(conn);
	change_advert_type(ADV_TYPE_NONCONN);
	/* Revert to slow blink pattern
	 * (should have been in LED_SENSOR_SEARCH_CONNECTABLE_PATTERN) */
	lcz_led_blink(BLUETOOTH_LED, &CT_LED_SENSOR_SEARCH_PATTERN);

	return;

fail:
	LOG_INF("Failed to connect to central: %s", log_strdup(addr));
	bt_conn_unref(conn);
	central_conn = NULL;
}

/* This callback is triggered when a BLE disconnection occurs,
 * but only handles cases in which the gateway is the peripheral.
 */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	if (conn == NULL) {
		LOG_ERR("disconnected callback got conn == NULL");
	}

	if (conn != central_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected sensor: %s (reason %u)", log_strdup(addr),
		reason);

	bt_conn_unref(conn);
	central_conn = NULL;
	start_advertising();
}

static void sensor_connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	if (conn != remote.conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err || conn == NULL) {
		goto fail;
	}

	LOG_INF("Connected sensor: %s", log_strdup(addr));
	bss_set_sensor_bt_addr(addr);

	remote.encrypt_req = false;
	remote.log_ble_xfer_active = true;
	ct.num_connections++;
	k_timer_stop(&sensor_conn_timeout_timer);
	k_work_submit(&discover_services_work);

	return;

fail:
	LOG_ERR("Failed to connect to sensor %s (%u)", log_strdup(addr), err);
	sensor_disconnect_cleanup(conn);
}

static void sensor_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	if (conn != remote.conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected sensor: %s reason: %s", log_strdup(addr),
		lbt_get_hci_err_string(reason));
	sensor_disconnect_cleanup(conn);
}

static void sensor_disconnect_cleanup(struct bt_conn *conn)
{
	k_timer_stop(&smp_xfer_timeout_timer);

	ct.log_publishing = false;
	bt_conn_unref(conn);
	remote.conn = NULL;
	remote.encrypt_req = false;

	set_ble_state(BT_DEMO_APP_STATE_FINDING_DEVICE);
}

static void disconnect_sensor(void)
{
	if (remote.conn) {
		bt_conn_disconnect(remote.conn,
				   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}

	if (stashed_entries.len > sizeof(struct ct_publish_header_t)) {
		/* There are stashed entries so set flag for them to be sent (after AWS reconnect) */
		stashed_entries.available = true;
	}
}

static void sensor_scan_conn_init()
{
	k_work_init(&sensor_att_timeout_work, sensor_att_timeout_callback);

	bt_conn_cb_register(&sensor_callbacks);

	lcz_bt_scan_register(&ct.scan_id, ct_sensor_adv_handler);

	k_timer_init(&sensor_conn_timeout_timer, sensor_conn_timeout_handler,
		     NULL);

	bss_init();
}

static void sensor_att_timeout_callback(struct k_work *work)
{
	bt_conn_disconnect(remote.conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static void sensor_conn_timeout_handler(struct k_timer *dummy)
{
	/* if this timer expires, it means a connection attempt failed mid-stride
	 * so disconnect */
	if (remote.conn != NULL) {
		LOG_ERR("Failed to connect - connection attempt timeout");
		k_work_submit(&sensor_att_timeout_work);
	}
}

static void adv_log_filter(const char *msg)
{
	if ((ct.all_ads % CONFIG_CT_ADV_LOG_FILTER_CNT) == 0) {
		LOG_INF("Ignoring ad: %s; all ads: %d ct ads: %d",
			log_strdup(msg), ct.all_ads, ct.ads);
	}
}

static void ct_sensor_adv_handler(const bt_addr_le_t *addr, int8_t rssi,
				  uint8_t type, struct net_buf_simple *ad)
{
	bool found = false;
	char bt_addr[BT_ADDR_LE_STR_LEN];
	AdHandle_t sensor_handle = { NULL, 0 };
	LczContactTracingAd_t *mfg;
	int err;

	ct.all_ads++;

	if (CONFIG_CT_ADV_WATCHDOG_SECONDS != 0) {
		err = k_delayed_work_submit(
			&ct_adv_watchdog,
			K_SECONDS(CONFIG_CT_ADV_WATCHDOG_SECONDS));
		if (err) {
			LOG_ERR("Unable to start adv watchdog");
		}
	}

	/* Leave this function if already connected */
	if (remote.conn) {
		adv_log_filter("already connected");
		return;
	}

	/* Leave this function if not connected to AWS */
	if (!Bluegrass_ReadyForPublish()) {
		adv_log_filter("not connected to AWS");
		return;
	}

	/* If there are queued entries to be sent over AWS, don't connect */
	if (stashed_entries.available) {
		adv_log_filter("send stash first");
		return;
	}

	/* We're only interested in connectable events */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	/* Check if this is the device we are looking for */
	sensor_handle = AdFind_Type(ad->data, ad->len,
				    BT_DATA_MANUFACTURER_DATA, BT_DATA_INVALID);
	if (sensor_handle.pPayload != NULL) {
		found = find_ct_ad(&sensor_handle);
	}

	if (!found) {
		adv_log_filter("non-contact tracing");
		return;
	}

	mfg = (LczContactTracingAd_t *)sensor_handle.pPayload;
	if (!valid_ct_record_type(mfg->recordType)) {
		adv_log_filter("Invalid record type");
		return;
	}

	ct.ads++;

	/* log_available flag is not set so do not connect */
	if ((mfg->flags & CT_ADV_FLAGS_HAS_LOG_DATA) == 0) {
		adv_log_filter("CT log data not present");
		return;
	}

	LOG_DBG("CT sensor with log data found (rssi: %d)", rssi);

	/* Can't connect while scanning */
	lcz_bt_scan_stop(ct.scan_id);

	/* Connect to device */
	bt_addr_le_to_str(addr, bt_addr, sizeof(bt_addr));
	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_CT, &remote.conn);
	if (err == 0) {
		LOG_DBG("Attempting to connect to remote BLE device %s",
			log_strdup(bt_addr));
		k_timer_start(&sensor_conn_timeout_timer,
			      SENSOR_CONNECTION_TIMEOUT_TICKS, K_NO_WAIT);
	} else {
		LOG_ERR("Failed to connect to remote BLE device %s err [%d]",
			log_strdup(bt_addr), err);
		set_ble_state(BT_DEMO_APP_STATE_FINDING_DEVICE);
	}
}

static bool find_ct_ad(AdHandle_t *handle)
{
	bool found = false;

	if (handle->pPayload != NULL) {
		if (memcmp(handle->pPayload, CT_TRACKER_AD_HEADER,
			   sizeof(CT_TRACKER_AD_HEADER)) == 0) {
			found = true;
		}
		if (memcmp(handle->pPayload, CT_DATA_DOWNLOAD_AD_HEADER,
			   sizeof(CT_DATA_DOWNLOAD_AD_HEADER)) == 0) {
			found = true;
		}
	}

	return found;
}

static bool valid_ct_record_type(uint8_t type)
{
	bool found = false;

	switch (type) {
	case CT_ADV_REC_TYPE_V00:
	case CT_ADV_REC_TYPE_V10:
	case CT_ADV_REC_TYPE_V11:
		found = true;
		break;
	default:
		found = false;
		break;
	}

	return found;
}

static void smp_echo_rsp_proc(struct bt_gatt_dfu_smp_c *dfu_smp_c)
{
	char tmp_buf[128] = { 0 };
	uint8_t *p_outdata = (uint8_t *)(&smp_rsp_buff);
	const struct bt_gatt_dfu_smp_rsp_state *rsp_state;

	rsp_state = bt_gatt_dfu_smp_c_rsp_state(dfu_smp_c);
	LOG_SMP("Echo response part received, size: %zu.",
		rsp_state->chunk_size);

	if (rsp_state->offset + rsp_state->chunk_size > sizeof(smp_rsp_buff)) {
		LOG_ERR("Response size buffer overflow (offset: %d, chunk_size: %d, sizeof(smp_rsp_buff): %d",
			rsp_state->offset, rsp_state->chunk_size,
			sizeof(smp_rsp_buff));
		dfu_smp_c->rsp_state.rc = MGMT_ERR_EMSGSIZE;
		return;
	} else {
		p_outdata += rsp_state->offset;
		memcpy(p_outdata, rsp_state->data, rsp_state->chunk_size);
	}

	if (bt_gatt_dfu_smp_c_rsp_total_check(dfu_smp_c)) {
		LOG_VRB("Total response received - decoding");
		if (smp_rsp_buff.header.op != MGMT_OP_WRITE_RSP) {
			LOG_ERR("Unexpected operation code (%u)!",
				smp_rsp_buff.header.op);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		uint16_t group = ((uint16_t)smp_rsp_buff.header.group_h8) << 8 |
				 smp_rsp_buff.header.group_l8;
		if (group != MGMT_GROUP_ID_OS) {
			LOG_ERR("Unexpected command group (%u)!", group);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		if (smp_rsp_buff.header.id != OS_MGMT_ID_ECHO) {
			LOG_ERR("Unexpected command (%u)",
				smp_rsp_buff.header.id);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		size_t payload_len = ((uint16_t)smp_rsp_buff.header.len_h8)
					     << 8 |
				     smp_rsp_buff.header.len_l8;

		CborError cbor_error;
		CborParser parser;
		CborValue value;
		struct cbor_buf_reader reader;

		cbor_buf_reader_init(&reader, smp_rsp_buff.payload,
				     payload_len);
		cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
		if (cbor_error != CborNoError) {
			LOG_ERR("CBOR parser initialization failed (err: %d)",
				cbor_error);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_EINVAL;
			return;
		}

		int i;
		char *tb = &tmp_buf[0];
		for (i = 0; i < payload_len; i++) {
			tb += sprintf(tb, "%02X ", smp_rsp_buff.payload[i]);
		}
		LOG_SMP("%s", tmp_buf);

		if (cbor_error != CborNoError) {
			LOG_ERR("Cannot print received CBOR stream (err: %d)",
				cbor_error);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_EINVAL;
			return;
		}
	}
}

static int send_smp_echo(struct bt_gatt_dfu_smp_c *dfu_smp_c,
			 const char *string)
{
	static struct smp_buffer smp_cmd;
	CborEncoder cbor, cbor_map;
	size_t payload_len;
	struct cbor_buf_writer writer;

	cbor_buf_writer_init(&writer, smp_cmd.payload, sizeof(smp_cmd.payload));
	cbor_encoder_init(&cbor, &writer.enc, 0);
	cbor_encoder_create_map(&cbor, &cbor_map, 1);
	cbor_encode_text_stringz(&cbor_map, "d");
	cbor_encode_text_stringz(&cbor_map, string);
	cbor_encoder_close_container(&cbor, &cbor_map);

	payload_len = (size_t)(writer.ptr - smp_cmd.payload);
	LOG_VRB("payload is %d bytes", payload_len);

	smp_cmd.header.op = MGMT_OP_WRITE;
	smp_cmd.header.flags = 0;
	smp_cmd.header.len_h8 = (uint8_t)((payload_len >> 8) & 0xFF);
	smp_cmd.header.len_l8 = (uint8_t)((payload_len >> 0) & 0xFF);
	smp_cmd.header.group_h8 = 0;
	smp_cmd.header.group_l8 = MGMT_GROUP_ID_OS;
	smp_cmd.header.seq = 0;
	smp_cmd.header.id = OS_MGMT_ID_ECHO;

	return bt_gatt_dfu_smp_c_command(dfu_smp_c, smp_echo_rsp_proc,
					 sizeof(smp_cmd.header) + payload_len,
					 &smp_cmd);
}

void ct_ble_smp_echo_test(void)
{
	static unsigned int echo_cnt = 0;
	char buffer[32];
	int ret;

	echo_cnt++;

	LOG_DBG("Echo test: %d", echo_cnt);
	snprintk(buffer, sizeof(buffer), "Echo message: %u", echo_cnt);
	ret = send_smp_echo(&dfu_smp_c, buffer);
	if (ret) {
		LOG_ERR("Echo command send error (err: %d)", ret);
	}
}

bool ct_ble_send_next_smp_request(uint32_t new_off)
{
	bool success = false;

	if (remote.app_state == BT_DEMO_APP_STATE_CHALLENGE_REQ) {
		LOG_DBG("/sys/challenge_rsp.bin");
		bt_gatt_dfu_smp_c_init(&dfu_smp_c, NULL);
		snprintk(smp_fs_download_filename,
			 sizeof(smp_fs_download_filename),
			 "/sys/challenge_rsp.bin");
		int32_t ret = send_smp_challenge_response(
			&dfu_smp_c, smp_fs_download_filename, 0, challenge_rsp,
			challenge_rsp_len);

		if (ret) {
			LOG_ERR("Authenticate device command send error (err: %d)",
				ret);
		} else {
			set_ble_state(BT_DEMO_APP_STATE_CHALLENGE_RSP);
			success = true;
			k_timer_start(&smp_xfer_timeout_timer,
				      SMP_TIMEOUT_TICKS, K_NO_WAIT);
		}
	} else if ((remote.app_state == BT_DEMO_APP_STATE_CHALLENGE_RSP) ||
		   (remote.app_state == BT_DEMO_APP_STATE_LOG_DOWNLOAD)) {
		if (remote.app_state == BT_DEMO_APP_STATE_CHALLENGE_RSP) {
			/* reset dfu_smp_c structure only on first download request */
			bt_gatt_dfu_smp_c_init(&dfu_smp_c, NULL);
		}
		snprintk(smp_fs_download_filename,
			 sizeof(smp_fs_download_filename), "/log/ct");
		int32_t ret = send_smp_download_request(
			&dfu_smp_c, smp_fs_download_filename, new_off);

		if (ret) {
			LOG_WRN("Download command send error (err: %d)", ret);
		} else {
			set_ble_state(BT_DEMO_APP_STATE_LOG_DOWNLOAD);
			success = true;
			k_timer_start(&smp_xfer_timeout_timer,
				      SMP_TIMEOUT_TICKS, K_NO_WAIT);
		}
	} else {
		LOG_ERR("Unknown app state - %d", remote.app_state);
		success = false;
	}

	return success;
}

static void smp_challenge_req_proc_handler(struct bt_gatt_dfu_smp_c *dfu_smp_c)
{
	CborError cbor_error;
	CborParser parser;
	CborValue value;
	struct cbor_buf_reader reader;

	uint8_t *p_outdata = (uint8_t *)(&smp_rsp_buff);
	const struct bt_gatt_dfu_smp_rsp_state *rsp_state =
		&dfu_smp_c->rsp_state;

	LOG_VRB("file part, size: %zu offset %d.", rsp_state->chunk_size,
		rsp_state->offset);

	if (rsp_state->offset + rsp_state->chunk_size > sizeof(smp_rsp_buff) ||
	    rsp_state->total_size > sizeof(smp_rsp_buff)) {
		LOG_ERR("Response size buffer overflow (offset: %d, chunk_size: %d, sizeof(smp_rsp_buff): %d, total_size: %d",
			rsp_state->offset, rsp_state->chunk_size,
			sizeof(smp_rsp_buff), rsp_state->total_size);
		dfu_smp_c->rsp_state.rc = MGMT_ERR_EMSGSIZE;
		return;
	} else {
		p_outdata += rsp_state->offset;
		memcpy(p_outdata, rsp_state->data, rsp_state->chunk_size);
		LOG_SMP("cpy %d into smp_rsp_buff @ %d", rsp_state->chunk_size,
			rsp_state->offset);
	}

	if (bt_gatt_dfu_smp_c_rsp_total_check(dfu_smp_c)) {
		/* SMP 8-byte HEADER PARSE START */
		if (smp_rsp_buff.header.op != MGMT_OP_READ_RSP) {
			LOG_ERR("Unexpected operation code (%u)!",
				smp_rsp_buff.header.op);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		uint16_t group = ((uint16_t)smp_rsp_buff.header.group_h8) << 8 |
				 smp_rsp_buff.header.group_l8;
		if (group != MGMT_GROUP_ID_FS) {
			LOG_ERR("Unexpected command group (%u)!", group);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		if (smp_rsp_buff.header.id != FS_MGMT_ID_FILE) {
			LOG_ERR("Unexpected command (%u)",
				smp_rsp_buff.header.id);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		/* payload_len represents the number of CBOR packet bytes available */
		size_t payload_len = ((uint16_t)smp_rsp_buff.header.len_h8)
					     << 8 |
				     smp_rsp_buff.header.len_l8;
		/* same as "total_len" of this smp packet */
		LOG_VRB("SMP payload_len: %d", payload_len);
		/**** SMP 8-byte HEADER PARSE END */

		/**** CBOR PARSE START */
		cbor_buf_reader_init(&reader, smp_rsp_buff.payload,
				     payload_len);
		cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
		if (cbor_error != CborNoError) {
			LOG_ERR("CBOR parser initialization failed (err: %d)",
				cbor_error);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_EINVAL;
			return;
		}

		long long int rc;
		unsigned long long off;
		unsigned long long len;
		size_t data_len;
		const struct cbor_attr_t uload_attr[5] = {
			[0] = { .attribute = "off",
				.type = CborAttrUnsignedIntegerType,
				.addr.uinteger = &off,
				.nodefault = true },
			[1] = { .attribute = "data",
				.type = CborAttrByteStringType,
				.addr.bytestring.data = file_data,
				.addr.bytestring.len = &data_len,
				.len = sizeof(file_data) },
			[2] = { .attribute = "rc",
				.type = CborAttrIntegerType,
				.addr.integer = &rc,
				.nodefault = true },
			[3] = { .attribute = "len",
				.type = CborAttrUnsignedIntegerType,
				.addr.uinteger = &len,
				.nodefault = true },
			[4] = { 0 },
		};

		cbor_error = cbor_read_object(&value, uload_attr);
		if (cbor_error != CborNoError) {
			LOG_ERR("Cannot parse received CBOR stream (err: %d)",
				cbor_error);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_EINVAL;
			return;
		}
		/**** CBOR PARSE END */

		/* if AWS connection is not up, abort the transfer */
		if (!Bluegrass_ReadyForPublish()) {
			LOG_ERR("AWS not connected during BLE transfer, aborting...");
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOENT;
			return;
		}

		/* check the return code */
		dfu_smp_c->rsp_state.rc = rc;
		if (rc) {
			LOG_ERR("Non-zero cbor rc:%d", (uint32_t)rc);
			if (rc ==
			    MGMT_ERR_EUNKNOWN) /* File path requested isn't implemented */
			{
				LOG_ERR("Authentication not supported by remote device. Continue download...");
				dfu_smp_c->rsp_state.rc = MGMT_ERR_EOK;
				/* setting to this state will send the log download request */
				set_ble_state(BT_DEMO_APP_STATE_CHALLENGE_RSP);
				remote.encrypt_req = false;
			}
			return;
		}

		LOG_SMP("off: %d, data_len: %d, rc: %d, len: %d", (uint32_t)off,
			(uint32_t)data_len, (uint32_t)rc, (uint32_t)len);

		/* reset the SMP transfer timeout timer */
		k_timer_start(&smp_xfer_timeout_timer, SMP_TIMEOUT_TICKS,
			      K_NO_WAIT);
		LOG_VRB("smp tmr restart");

		if (dfu_smp_c->downloaded_bytes == 0) {
			/* If the file path existed but no authentication data was sent, assume auth not needed */
			if (data_len == 0) {
				LOG_ERR("No auth data from remote device (auth not required). Continue download...");
				dfu_smp_c->rsp_state.rc = MGMT_ERR_EOK;
				/* setting to this state will send the log download request */
				set_ble_state(BT_DEMO_APP_STATE_CHALLENGE_RSP);
				remote.encrypt_req = false;
				return;
			}
			dfu_smp_c->file_size = len;
		}

		/* copy the file data into log_buffer */
		if (sizeof(log_buffer) >
		    dfu_smp_c->downloaded_bytes + data_len) {
			memcpy(&log_buffer[dfu_smp_c->downloaded_bytes],
			       file_data, data_len);
			LOG_SMP("copied %d ct_log_header bytes into log_buffer",
				data_len);
		} else {
			LOG_SMP("overflow, just keep downloading but don't store the data");
			dfu_smp_c->rsp_state.rc = MGMT_ERR_EMSGSIZE;
			return;
		}

		dfu_smp_c->downloaded_bytes += data_len;

		/* If this is last packet, encrypt the received authentication data and send the encrypted string to the remote device */
		if (dfu_smp_c->downloaded_bytes > 0 &&
		    dfu_smp_c->downloaded_bytes == dfu_smp_c->file_size) {
			uint8_t key[AES_KEY_SIZE];
			memset(key, AES_BLANK_KEY_BYTE_VALUE, AES_KEY_SIZE);
			nvReadAesKey(key);

			/* This function returns the output size of the cipher text + IV (0 if failed)
			 * The output buffer max len must be "input len + 16" (challenge length + IV size)
			 */
			challenge_rsp_len = encrypt_cbc(
				log_buffer, dfu_smp_c->downloaded_bytes,
				challenge_rsp, sizeof(challenge_rsp), key,
				sizeof(key));
		}
	}
}

static void smp_challenge_rsp_proc_handler(struct bt_gatt_dfu_smp_c *dfu_smp_c)
{
	CborError cbor_error;
	CborParser parser;
	CborValue value;
	struct cbor_buf_reader reader;

	uint8_t *p_outdata = (uint8_t *)(&smp_rsp_buff);
	const struct bt_gatt_dfu_smp_rsp_state *rsp_state =
		&dfu_smp_c->rsp_state;

	LOG_VRB("file part, size: %zu offset %d.", rsp_state->chunk_size,
		rsp_state->offset);

	if (rsp_state->offset + rsp_state->chunk_size > sizeof(smp_rsp_buff) ||
	    rsp_state->total_size > sizeof(smp_rsp_buff)) {
		LOG_ERR("Response size buffer overflow (offset: %d, chunk_size: %d, sizeof(smp_rsp_buff): %d, total_size: %d",
			rsp_state->offset, rsp_state->chunk_size,
			sizeof(smp_rsp_buff), rsp_state->total_size);
		dfu_smp_c->rsp_state.rc = MGMT_ERR_EMSGSIZE;
		return;
	} else {
		p_outdata += rsp_state->offset;
		memcpy(p_outdata, rsp_state->data, rsp_state->chunk_size);
		LOG_SMP("cpy %d into smp_rsp_buff @ %d", rsp_state->chunk_size,
			rsp_state->offset);
	}

	if (bt_gatt_dfu_smp_c_rsp_total_check(dfu_smp_c)) {
		/**** SMP 8-byte HEADER PARSE START */
		if (smp_rsp_buff.header.op != MGMT_OP_WRITE_RSP) {
			LOG_ERR("Unexpected operation code (%u)!",
				smp_rsp_buff.header.op);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		uint16_t group = ((uint16_t)smp_rsp_buff.header.group_h8) << 8 |
				 smp_rsp_buff.header.group_l8;
		if (group != MGMT_GROUP_ID_FS) {
			LOG_ERR("Unexpected command group (%u)!", group);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		if (smp_rsp_buff.header.id != 0 /* FS_MGMT_ID_FILE */) {
			LOG_ERR("Unexpected command (%u)",
				smp_rsp_buff.header.id);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		/* payload_len represents the number of CBOR packet bytes available */
		size_t payload_len = ((uint16_t)smp_rsp_buff.header.len_h8)
					     << 8 |
				     smp_rsp_buff.header.len_l8;
		/* same as "total_len" of this smp packet */
		LOG_VRB("SMP payload_len: %d", payload_len);
		/**** SMP 8-byte HEADER PARSE END */

		/**** CBOR PARSE START */
		cbor_buf_reader_init(&reader, smp_rsp_buff.payload,
				     payload_len);
		cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
		if (cbor_error != CborNoError) {
			LOG_ERR("CBOR parser initialization failed (err: %d)",
				cbor_error);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_EINVAL;
			return;
		}

		long long int rc;
		unsigned long long off;
		unsigned long long len;
		size_t data_len;
		const struct cbor_attr_t uload_attr[5] = {
			[0] = { .attribute = "off",
				.type = CborAttrUnsignedIntegerType,
				.addr.uinteger = &off,
				.nodefault = true },
			[1] = { .attribute = "data",
				.type = CborAttrByteStringType,
				.addr.bytestring.data = file_data,
				.addr.bytestring.len = &data_len,
				.len = sizeof(file_data) },
			[2] = { .attribute = "rc",
				.type = CborAttrIntegerType,
				.addr.integer = &rc,
				.nodefault = true },
			[3] = { .attribute = "len",
				.type = CborAttrUnsignedIntegerType,
				.addr.uinteger = &len,
				.nodefault = true },
			[4] = { 0 },
		};

		cbor_error = cbor_read_object(&value, uload_attr);
		if (cbor_error != CborNoError) {
			LOG_ERR("Cannot parse received CBOR stream (err: %d)",
				cbor_error);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_EINVAL;
			return;
		}
		/**** CBOR PARSE END */

		/* if AWS connection is not up, abort the transfer */
		if (!Bluegrass_ReadyForPublish()) {
			LOG_ERR("AWS not connected during BLE transfer, aborting...");
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOENT;
			return;
		}

		dfu_smp_c->file_size = len;

		/* check the return code */
		dfu_smp_c->rsp_state.rc = rc;
		if (rc) {
			LOG_ERR("Non-zero cbor rc:%d (authentication failed)",
				(uint32_t)rc);
			return;
		} else {
			LOG_DBG("Authentication successful. Continue download...");
			dfu_smp_c->rsp_state.rc = MGMT_ERR_EOK;
			/* setting to this state will send the log download request */
			set_ble_state(BT_DEMO_APP_STATE_CHALLENGE_RSP);
			remote.encrypt_req = true;
		}

		LOG_SMP("off: %d, data_len: %d, rc: %d, len: %d", (uint32_t)off,
			(uint32_t)data_len, (uint32_t)rc, (uint32_t)len);

		/* reset the SMP transfer timeout timer */
		k_timer_start(&smp_xfer_timeout_timer, SMP_TIMEOUT_TICKS,
			      K_NO_WAIT);
		LOG_VRB("smp tmr restart");
	}
}

static int calculate_record_bytes_in_entry(uint8_t *buf, uint16_t record_size,
					   uint16_t len)
{
	uint32_t i;
	uint16_t header_size = offsetof(log_entry_t, data);

	/* given an entry, find the index in the buffer (if any) at
	 * which the remaining records would be all 0xFF's
	 */
	if (header_size > len)
		return -1;
	if (record_size > len)
		return -1;
	if ((header_size + record_size) > len)
		return -1;
	if (buf == NULL)
		return -1;

	log_entry_data_rssi_tracking_t *records =
		(log_entry_data_rssi_tracking_t *)&buf[header_size];

	for (i = 0; i < (len - header_size); i += record_size) {
		if (records[i].recordType == 0xFF)
			return i;
	}
	return i;
}

/* clang-format off */
static void smp_file_download_rsp_proc(struct bt_gatt_dfu_smp_c *dfu_smp_c)
{
	CborError cbor_error;
	CborParser parser;
	CborValue value;
	struct cbor_buf_reader reader;
	bt_addr_le_t addr;
	char addr_str[BT_ADDR_LE_STR_LEN];
	uint16_t crctmp, crcval;

	uint8_t *p_outdata = (uint8_t *)(&smp_rsp_buff);
	const struct bt_gatt_dfu_smp_rsp_state *rsp_state = &dfu_smp_c->rsp_state;

	LOG_VRB("file part, size: %zu offset %d.", rsp_state->chunk_size, rsp_state->offset);

	if (rsp_state->offset + rsp_state->chunk_size > sizeof(smp_rsp_buff) ||
	    rsp_state->total_size > sizeof(smp_rsp_buff)) {
		LOG_ERR("Response size buffer overflow (offset: %d, chunk_size: %d, sizeof(smp_rsp_buff): %d, total_size: %d",
			rsp_state->offset, rsp_state->chunk_size, sizeof(smp_rsp_buff), rsp_state->total_size);
		dfu_smp_c->rsp_state.rc = MGMT_ERR_EMSGSIZE;
		return;
	} else {
		p_outdata += rsp_state->offset;
		memcpy(p_outdata, rsp_state->data, rsp_state->chunk_size);
		LOG_SMP("cpy %d into smp_rsp_buff @ %d", rsp_state->chunk_size, rsp_state->offset);
	}

	if (bt_gatt_dfu_smp_c_rsp_total_check(dfu_smp_c)) {
		/**** SMP 8-byte HEADER PARSE START */
		if (smp_rsp_buff.header.op != MGMT_OP_READ_RSP /* READ RSP*/) {
			LOG_ERR("Unexpected operation code (%u)!", smp_rsp_buff.header.op);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		uint16_t group = ((uint16_t)smp_rsp_buff.header.group_h8) << 8 | smp_rsp_buff.header.group_l8;
		if (group != MGMT_GROUP_ID_FS) {
			LOG_ERR("Unexpected command group (%u)!", group);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		if (smp_rsp_buff.header.id != 0 /* FS_MGMT_ID_FILE */) {
			LOG_ERR("Unexpected command (%u)", smp_rsp_buff.header.id);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOTSUP;
			return;
		}
		/* payload_len represents the number of CBOR packet bytes available */
		size_t payload_len = ((uint16_t)smp_rsp_buff.header.len_h8) << 8 | smp_rsp_buff.header.len_l8;
		/* same as "total_len" of this smp packet */
		LOG_VRB("SMP payload_len: %d", payload_len);
		/**** SMP 8-byte HEADER PARSE END */

		/**** CBOR PARSE START */
		cbor_buf_reader_init(&reader, smp_rsp_buff.payload, payload_len);
		cbor_error = cbor_parser_init(&reader.r, 0, &parser, &value);
		if (cbor_error != CborNoError) {
			LOG_ERR("CBOR parser initialization failed (err: %d)", cbor_error);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_EINVAL;
			return;
		}

		long long int rc;
		unsigned long long off;
		unsigned long long len;
		size_t data_len;
		const struct cbor_attr_t uload_attr[5] = {
			[0] = { .attribute = "off",
				.type = CborAttrUnsignedIntegerType,
				.addr.uinteger = &off,
				.nodefault = true },
			[1] = { .attribute = "data",
				.type = CborAttrByteStringType,
				.addr.bytestring.data = file_data,
				.addr.bytestring.len = &data_len,
				.len = sizeof(file_data) },
			[2] = { .attribute = "rc", .type = CborAttrIntegerType, .addr.integer = &rc, .nodefault = true },
			[3] = { .attribute = "len",
				.type = CborAttrUnsignedIntegerType,
				.addr.uinteger = &len,
				.nodefault = true },
			[4] = { 0 },
		};

		cbor_error = cbor_read_object(&value, uload_attr);
		if (cbor_error != CborNoError) {
			LOG_ERR("Cannot parse received CBOR stream (err: %d)", cbor_error);
			dfu_smp_c->rsp_state.rc = MGMT_ERR_EINVAL;
			return;
		}
		/**** CBOR PARSE END */

		/* check the return code */
		dfu_smp_c->rsp_state.rc = rc;
		if (rc) {
			LOG_ERR("Non-zero cbor rc:%d", (uint32_t)rc);
			return;
		}

		/* if AWS connection is not up, abort the transfer */
		if (!Bluegrass_ReadyForPublish()) {
			LOG_ERR("AWS not connected during BLE transfer, aborting...");
			dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOENT;

			if (stashed_entries.len > sizeof(struct ct_publish_header_t)) {
				/* There are stashed entries so set flag for them to be sent (after AWS reconnect) */
				stashed_entries.available = true;
			}

			return;
		}

		LOG_SMP("off: %d, data_len: %d, rc: %d, len: %d", (uint32_t)off, (uint32_t)data_len, (uint32_t)rc,
			(uint32_t)len);

		/* reset the SMP transfer timeout timer */
		k_timer_start(&smp_xfer_timeout_timer, SMP_TIMEOUT_TICKS, K_NO_WAIT);
		LOG_VRB("smp tmr restart");

		uint8_t dec_file_data[FS_MGMT_DL_CHUNK_SIZE];
		if (remote.encrypt_req == true) {
			uint8_t key[AES_KEY_SIZE];
			memset(key, AES_BLANK_KEY_BYTE_VALUE, AES_KEY_SIZE);
			nvReadAesKey(key);

			/* max output len must be "input len - 16" */
			uint32_t decrypted_length = decrypt_cbc(file_data, data_len, dec_file_data,
							       data_len - AES_CBC_IV_SIZE, key, sizeof(key));
			data_len = decrypted_length;

			if (decrypted_length == 0) {
				/* decryption failed */
				dfu_smp_c->downloaded_bytes += data_len;
				dfu_smp_c->rsp_state.rc = MGMT_ERR_EINVAL;
				return;
			}
		} else {
			memcpy(dec_file_data, file_data, data_len);
		}

		if (dfu_smp_c->downloaded_bytes == 0) {
			/* If there are no entries, just disconnect */
			if (len == data_len) {
				LOG_DBG("No entries, disconnecting...");
				dfu_smp_c->downloaded_bytes += data_len;
				dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOENT;
				return;
			}

			/* If we are downloading the first chunk, this is the ct_log_header */
			bt_addr_le_to_str(bt_conn_get_dst(remote.conn), addr_str, sizeof(addr_str));

			/* Record the file size */
			dfu_smp_c->file_size = len;
			dfu_smp_c->rec_cnt = 0;
			dfu_smp_c->ent_cnt = 0;

			/* copy the file data into log_buffer */
			if (sizeof(log_buffer) > dfu_smp_c->downloaded_bytes + data_len) {
				memcpy(&log_buffer[dfu_smp_c->downloaded_bytes], dec_file_data, data_len);
				LOG_SMP("copied %d ct_log_header bytes into log_buffer", data_len);
			} else {
				LOG_SMP("overflow, just keep downloading but don't store the data");
			}

			dfu_smp_c->entry_protocol_version =
				((struct ct_log_header_entry_protocol_version *)log_buffer)->entry_protocol_version;

			if (dfu_smp_c->entry_protocol_version == 1) {
				LOG_INF("\033[38;5;28m%s log dl (V%d, %d bytes)", log_strdup(addr_str),
					dfu_smp_c->entry_protocol_version, (uint32_t)len);
			} else if (dfu_smp_c->entry_protocol_version == 2) {
				LOG_INF("\033[38;5;231m%s log dl (V%d, %d bytes)", log_strdup(addr_str),
					dfu_smp_c->entry_protocol_version, (uint32_t)len);
			}

			/* keep track of the # of entry bytes downloaded into log_buffer */
			dfu_smp_c->entry_downloaded_bytes = 0;

			ct.num_download_starts++;

			switch (dfu_smp_c->entry_protocol_version) {
			case LOG_ENTRY_PROTOCOL_V1:
				/**
				 * Parser for Entry Protocol 0x0001
				 *   ct_log_header entry_size dictates the number of bytes for each entry (not variable per entry)
				 */
				dfu_smp_c->entry_size = ((struct ct_log_header *)log_buffer)->entry_size;

				/* make a copy of the ct_log_header for use later on in parsing */
				memcpy(&dfu_smp_c->ct_log_header, log_buffer, data_len);
				break;

			case LOG_ENTRY_PROTOCOL_V2:
				/**
				 * Parser for Entry Protocol 0x0002
				 *   ct_log_header_v2 is used, adds field for # of bytes in record data (supports variable size per entry and/or record)
				 */
				crctmp = *((uint16_t *)&log_buffer[sizeof(struct ct_log_header_v2)]);
				crcval = crc16_ccitt(0, log_buffer, sizeof(struct ct_log_header_v2));

				if (crctmp == crcval) {
					dfu_smp_c->entry_size = ((struct ct_log_header_v2 *)log_buffer)->max_entry_size;

					/* make a copy of the ct_log_header_v2 for use later on in parsing */
					memcpy(&dfu_smp_c->ct_log_header, log_buffer, sizeof(struct ct_log_header_v2));
				} else {
					LOG_ERR("CRC mismatch in log header");
					dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOENT;
					return;
				}

				break;

			default:
				/* abort the transfer, unsupported entry protocol format */
				LOG_ERR("Unsupported Entry Protocol Version %04X", dfu_smp_c->entry_protocol_version);
				dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOENT;
				return;
			}

			LOG_VRB("entry_size: %d", dfu_smp_c->entry_size);

		} else {
			/* copy a chunk of data into the log_buffer */
			if (sizeof(log_buffer) > dfu_smp_c->entry_downloaded_bytes + data_len) {
				memcpy(&log_buffer[dfu_smp_c->entry_downloaded_bytes], dec_file_data, data_len);
				LOG_SMP("copied %d entry bytes into log_buffer", data_len);
			} else {
				LOG_SMP("overflow, just keep downloading but don't store the data");
			}

			ct.log_publishing = true;
			/* set flag indicating active data transfer */
			remote.log_ble_xfer_active = true;
			/* if enough bytes have been downloaded to complete an entry + 2 bytes for crc16,
			 * calculate the CRC16 and if it passes, copy the entry into the log buffer
			 */
			switch (dfu_smp_c->entry_protocol_version) {
			case LOG_ENTRY_PROTOCOL_V1:
				/*
				 * Parser for Entry Protocol 0x0001
				 *   Each entry is of fixed size (256 + 2 CRC bytes for total of 258)
				 *   and all 256 bytes + CRC16 are sent in one SMP CBOR packet.
				 */
				if ((dfu_smp_c->entry_downloaded_bytes + data_len) >=
				    (dfu_smp_c->entry_size + sizeof(uint16_t))) {
					crctmp = *((uint16_t *)&log_buffer[dfu_smp_c->entry_size]);
					crcval = crc16_ccitt(0, log_buffer, dfu_smp_c->entry_size);

					if (crctmp == crcval) {
						dfu_smp_c->ent_cnt++; /* count # of entries received */

						/* CRC was good, send the entry to AWS */
						/* take sem if we can send to AWS */
						if (k_sem_take(&sending_to_aws_sem, SEND_TO_AWS_TIMEOUT_TICKS) != 0) {
							LOG_ERR("ble->aws pub timeout");

							/* This entry was not able to be published due to timeout so need to stash this entry and force a disconnect */
							/* to send after processing this packet. */
							int record_bytes_in_entry = calculate_record_bytes_in_entry(
								log_buffer, sizeof(log_entry_data_rssi_tracking_t),
								dfu_smp_c->entry_size);
							if (record_bytes_in_entry > 0 &&
							    (record_bytes_in_entry + offsetof(log_entry_t, data) +
							     sizeof(struct ct_publish_header_t)) <
								    sizeof(aws_work.buf)) {
								/* fill in the ct_publish_header_t */
								struct ct_publish_header_t pub_hdr;
								pub_hdr.entry_protocol_version =
									dfu_smp_c->ct_log_header.entry_protocol_version;
								memcpy(pub_hdr.device_id,
								       dfu_smp_c->ct_log_header.device_id,
								       BT_MAC_ADDR_LEN);
								pub_hdr.device_time = lcz_qrtc_get_epoch();
								pub_hdr.last_upload_time =
									dfu_smp_c->ct_log_header.last_upload_time;

								/* put in stash if fits. */
								if ((stashed_entries.len +
								     sizeof(struct ct_publish_header_t) +
								     offsetof(log_entry_t, data) +
								     record_bytes_in_entry) <
								    CONFIG_CT_LOG_DOWNLOAD_BUFFER_SIZE) {
									if (stashed_entries.len == 0) {
										memcpy(stashed_entries.buffer, &pub_hdr,
										       sizeof(struct ct_publish_header_t));
										stashed_entries.len += sizeof(
											struct ct_publish_header_t);
										stashed_entries.idx = sizeof(
											struct ct_publish_header_t);
									}

									/* current entry being stashed */
									log_entry_t *entry = (log_entry_t *)&log_buffer
										[0];
									LOG_DBG("Stash entry %02x%02x, %d",
										entry->header.serial[1],
										entry->header.serial[0],
										entry->header.timestamp);
									memcpy(&stashed_entries.buffer[stashed_entries.len],
									       log_buffer,
									       offsetof(log_entry_t, data) +
										       record_bytes_in_entry);
									stashed_entries.len +=
										sizeof(struct ct_publish_header_t) +
										offsetof(log_entry_t, data) +
										record_bytes_in_entry;
								} else {
									LOG_ERR("No space left in entry stash, have to discard entry");
								}

								LOG_VRB(">> %d %d %d", record_bytes_in_entry,
									aws_work.buf_len,
									sizeof(aws_work.buf));
							} else {
								if (record_bytes_in_entry == 0) {
									LOG_DBG("0 records found in entry");
								} else {
									LOG_DBG("skipping AWS publish, entry too large for buffer");
								}
							}

							disconnect_sensor();
						} else {
							int record_bytes_in_entry = calculate_record_bytes_in_entry(
								log_buffer, sizeof(log_entry_data_rssi_tracking_t),
								dfu_smp_c->entry_size);
							if (record_bytes_in_entry > 0 &&
							    (record_bytes_in_entry + offsetof(log_entry_t, data) +
							     sizeof(struct ct_publish_header_t)) <
								    sizeof(aws_work.buf)) {
								/* fill in the ct_publish_header_t */
								struct ct_publish_header_t pub_hdr;
								pub_hdr.entry_protocol_version =
									dfu_smp_c->ct_log_header.entry_protocol_version;
								memcpy(pub_hdr.device_id,
								       dfu_smp_c->ct_log_header.device_id,
								       BT_MAC_ADDR_LEN);
								pub_hdr.device_time = lcz_qrtc_get_epoch();
								pub_hdr.last_upload_time =
									dfu_smp_c->ct_log_header.last_upload_time;

								memcpy(aws_work.buf, &pub_hdr,
								       sizeof(struct ct_publish_header_t));
								memcpy(&aws_work.buf[sizeof(
									       struct ct_publish_header_t)],
								       log_buffer,
								       offsetof(log_entry_t, data) +
									       record_bytes_in_entry);

								aws_work.buf_len =
									sizeof(struct ct_publish_header_t) +
									offsetof(log_entry_t, data) +
									record_bytes_in_entry;
								ct.aws_publish_state =
									AWS_PUBLISH_STATE_PENDING; /* set state to indicate waiting on publish result - check for publish success on next run of this function */

								/* Preemptively put in stash if fits. If publish is successful, remove it from stash */
								if ((stashed_entries.len +
								     sizeof(struct ct_publish_header_t) +
								     offsetof(log_entry_t, data) +
								     record_bytes_in_entry) <
								    CONFIG_CT_LOG_DOWNLOAD_BUFFER_SIZE) {
									if (stashed_entries.len == 0) {
										memcpy(stashed_entries.buffer, &pub_hdr,
										       sizeof(struct ct_publish_header_t));
										stashed_entries.len += sizeof(
											struct ct_publish_header_t);
										stashed_entries.idx = sizeof(
											struct ct_publish_header_t);
									}

									memcpy(&stashed_entries.buffer[stashed_entries.len],
									       log_buffer,
									       offsetof(log_entry_t, data) +
										       record_bytes_in_entry);
									stashed_entries.len +=
										sizeof(struct ct_publish_header_t) +
										offsetof(log_entry_t, data) +
										record_bytes_in_entry;
									stashed_entries.prev_ent_size =
										sizeof(struct ct_publish_header_t) +
										offsetof(log_entry_t,
											 data) +
										record_bytes_in_entry; /* need a static variable to hold this value since ent_size will be updated before ct.aws_publish_state can be checked. */
								} else {
									LOG_ERR("No space left in entry stash, have to discard entry");
								}

								LOG_VRB(">> %d %d %d", record_bytes_in_entry,
									aws_work.buf_len,
									sizeof(aws_work.buf));
							} else {
								if (record_bytes_in_entry == 0) {
									LOG_DBG("0 records found in entry");
								} else {
									LOG_DBG("skipping AWS publish, entry too large for buffer");
								}
							}
							/* Send the data to AWS via work queue item */
							k_work_submit(&aws_work.work);

							/* Wait for publish completion immediately before moving on so that can stash entry if needed. */
							/* (If not stashed here, publish status may not be checked later on and entry stash may not be correctly set) */
							if (ct.aws_publish_state == AWS_PUBLISH_STATE_PENDING) {
								if ((k_sem_take(&sending_to_aws_sem,
										SEND_TO_AWS_TIMEOUT_TICKS) != 0)) {
									LOG_ERR("aws pub timed out");
									/* If publish times out, it must have failed. */
									ct.aws_publish_state =
										AWS_PUBLISH_STATE_FAIL;
								} else {
									/* Semaphore was taken. Only used to synchronize publish completion and no longer needed. */
									k_sem_give(&sending_to_aws_sem);
								}

								if (ct.aws_publish_state ==
								    AWS_PUBLISH_STATE_SUCCESS) {
									/* Decrement stash length to discard the successfully published entry */
									stashed_entries.len -= stashed_entries.prev_ent_size;
								} else /* AWS_PUBLISH_STATE_FAIL || AWS_PUBLISH_STATE_PENDING */
								{
									log_entry_t *entry = (log_entry_t
												      *)&stashed_entries.buffer
										[stashed_entries.len -
										 stashed_entries.prev_ent_size]; /* need to offset backwards to point to recently added entry */
									LOG_DBG("Stash entry %02x%02x, %d",
										entry->header.serial[1],
										entry->header.serial[0],
										entry->header.timestamp);
									stashed_entries.available = true;
								}
								ct.aws_publish_state =
									AWS_PUBLISH_STATE_NONE; /* reset state for next CT log download */
							} else {
								ct.aws_publish_state =
									AWS_PUBLISH_STATE_NONE; /* reset state for next CT log download */
							}
						}
					} else {
						/* red */
						LOG_DBG("\033[1;31m[ENT%3d] %s [%02X %02X %02X %02X...%02X %02X] rcv_crc: %04X, calc_crc: %04X ",
							dfu_smp_c->entry_count, log_strdup(addr_str), log_buffer[0], log_buffer[1],
							log_buffer[2], log_buffer[3],
							log_buffer[dfu_smp_c->entry_size - 2],
							log_buffer[dfu_smp_c->entry_size - 1], crctmp, crcval);
					}

					/* copy the remaining bytes back into log_buffer if there are any */
					if (dfu_smp_c->entry_downloaded_bytes + data_len >
					    (dfu_smp_c->entry_size + sizeof(uint16_t))) {
						uint16_t remaining = (dfu_smp_c->entry_downloaded_bytes + data_len -
								      (dfu_smp_c->entry_size + sizeof(uint16_t)));
						if (remaining > 0) {
							memcpy(log_buffer, &dec_file_data[remaining], remaining);
							dfu_smp_c->entry_downloaded_bytes = remaining;
						} else {
							dfu_smp_c->entry_downloaded_bytes = 0;
						}
					} else {
						dfu_smp_c->entry_downloaded_bytes = 0;
					}
					dfu_smp_c->entry_count++;
				}
				break;

			case LOG_ENTRY_PROTOCOL_V2: {
				/* entry index iterator */
				uint32_t ent_idx = 0;
				/* byte offset into log_buffer for start of next entry */
				uint32_t ent_offset = 0;
				/* size of current entry */
				uint16_t ent_size = 0;
				/* record index iterator */
				uint16_t rec_idx = 0;
				/* count records per entry for debug output */
				uint16_t rec_cnt = 0;
				log_entry_t *entry;

				LOG_VRB("Received V2 Entry: %d total, %d entry bytes", len,
					dfu_smp_c->entry_downloaded_bytes + data_len);
				/**
				 * Parser for Entry Protocol 0x0002
				 *   Each SMP packet length is a multiple of (max_entry_size + 2 CRC bytes)
				 *   and entries may be variable length. One or more entries may be present
				 *   in a single SMP CBOR packet.
				 */
				do {
					/* reset the SMP transfer timeout timer */
					k_timer_start(&smp_xfer_timeout_timer, SMP_TIMEOUT_TICKS, K_NO_WAIT);
					LOG_VRB("smp tmr restart in msg");

					/* iterate over the downloaded bytes parsing each entry */
					entry = (log_entry_t *)&log_buffer[ent_offset];
					if (entry->header.entryStart != LOG_ENTRY_START_BYTE) {
						/* this is not an entry, break (could be padding bytes if encryption is enabled) */
						LOG_ERR("start (0x%02X) != %02X", entry->header.entryStart,
							LOG_ENTRY_START_BYTE);
						break;
					}
					ent_size = *((uint16_t *)(&entry->header.reserved[0]));

					if ((ent_offset + ent_size + sizeof(uint16_t)) > sizeof(log_buffer)) {
						LOG_ERR("err ent_offset: %d, ent_size: %d", ent_offset, ent_size);
						break;
					}

					uint16_t crctmp = *((uint16_t *)&log_buffer[ent_offset + ent_size]);
					uint16_t crcval = crc16_ccitt(0, &log_buffer[ent_offset], ent_size);

					if (crctmp == crcval) {

						addr.type = BT_ADDR_LE_RANDOM;
						memcpy(addr.a.val,
						       ((log_entry_t *)&log_buffer[ent_offset])->header.serial,
						       sizeof(bt_addr_t));
						bt_addr_le_to_str(&addr, addr_str, sizeof(addr_str));

						LOG_VRB("\033[0;32m[ENT%3d] \033[38;5;68m%s ", ent_idx, log_strdup(addr_str));
						rec_cnt = 0;
						for (rec_idx = sizeof(entry->header); rec_idx < ent_size;) {
							uint8_t rec_type = log_buffer[ent_offset + rec_idx];
							switch (rec_type) {
							case CT_ADV_REC_TYPE_V10:
								rec_idx += 4;
								dfu_smp_c->rec_cnt++;
								rec_cnt++;
								break;
							case CT_ADV_REC_TYPE_V11:
								rec_idx += 8;
								dfu_smp_c->rec_cnt++;
								rec_cnt++;
								break;
							default:
								LOG_DBG("\033[38;5;196m[Unk%02X] ", rec_type);
								rec_idx += 4;
								break;
							}
						}

						LOG_VRB("\033[38;5;163m[%2d rec] %3d \033[38;5;68m%s, %d", rec_cnt,
							ent_size, log_strdup(addr_str),
							((log_entry_t *)&log_buffer[ent_offset])->header.timestamp);
						LOG_VRB("%8lld\033[38;5;163m %3d", k_uptime_get(), ent_size);
						LOG_VRB("\033[0;32m[ENT%3d] %d", dfu_smp_c->downloaded_bytes);

						/* store the entry size in the last 2 bytes of the entry header */
						*((uint16_t *)&((log_entry_t *)&log_buffer[ent_offset])
							  ->header.reserved[0]) = ent_size;

#if defined(CONFIG_CT_AWS_PUBLISH_ENTRIES)
						/* send the entry to AWS */
						/* take sem if we can send to AWS */
						if (k_sem_take(&sending_to_aws_sem, SEND_TO_AWS_TIMEOUT_TICKS) != 0) {
							LOG_ERR("ble->aws pub timeout");

							/* This entry was not able to be published due to timeout so need to stash this entry and force a disconnect
							 * to send after processing this packet.
							 * check that entry will fit in the aws send buffer
							 */
							if (ent_size > 0 &&
							    (ent_size + sizeof(struct ct_publish_header_t)) <
								    sizeof(aws_work.buf)) {
								/* fill in the ct_publish_header_t */
								struct ct_publish_header_t pub_hdr;
								pub_hdr.entry_protocol_version =
									dfu_smp_c->ct_log_header.entry_protocol_version;
								memcpy(pub_hdr.device_id,
								       dfu_smp_c->ct_log_header.device_id,
								       BT_MAC_ADDR_LEN);
								pub_hdr.device_time = lcz_qrtc_get_epoch();
								pub_hdr.last_upload_time =
									dfu_smp_c->ct_log_header.last_upload_time;
								memcpy(pub_hdr.fw_version,
								       dfu_smp_c->ct_log_header.local_info.fw_version,
								       sizeof(pub_hdr.fw_version));
								pub_hdr.battery_level =
									dfu_smp_c->ct_log_header.local_info
										.battery_level;
								pub_hdr.network_id =
									dfu_smp_c->ct_log_header.local_info.network_id;

								/* Put in stash if fits. */
								if ((stashed_entries.len +
								     sizeof(struct ct_publish_header_t) + ent_size) <
								    CONFIG_CT_LOG_DOWNLOAD_BUFFER_SIZE) {
									if (stashed_entries.len == 0) {
										memcpy(stashed_entries.buffer, &pub_hdr,
										       sizeof(struct ct_publish_header_t));
										stashed_entries.len += sizeof(
											struct ct_publish_header_t);
										stashed_entries.idx = sizeof(
											struct ct_publish_header_t);
									}

									LOG_DBG("Stash entry %02x%02x, %d",
										entry->header.serial[1],
										entry->header.serial[0],
										entry->header
											.timestamp); /* current entry is being stashed */
									memcpy(&stashed_entries.buffer[stashed_entries.len],
									       &log_buffer[ent_offset], ent_size);
									stashed_entries.len += ent_size;
								} else {
									LOG_ERR("No space left in entry stash, have to discard entry");
								}
							} else {
								if (ent_size == 0) {
									LOG_DBG("0 records found in entry");
								} else {
									LOG_DBG("skipping AWS publish, entry too large for buffer");
								}
							}

							disconnect_sensor();
						} else {
							/* Check publish result if an entry was previously published (STATE_NONE indicates no publish has occurred yet) */
							if (ct.aws_publish_state != AWS_PUBLISH_STATE_NONE) {
								if (ct.aws_publish_state ==
								    AWS_PUBLISH_STATE_SUCCESS) {
									/* Decrement stash length to discard the successfully published entry */
									stashed_entries.len -= stashed_entries.prev_ent_size;
								} else /* AWS_PUBLISH_STATE_FAIL || AWS_PUBLISH_STATE_PENDING */
								{
									log_entry_t *entry = (log_entry_t
												      *)&stashed_entries.buffer
										[stashed_entries.len -
										 stashed_entries.prev_ent_size]; /* need to offset backwards to point to recently added entry */
									LOG_DBG("Stash entry %02x%02x, %d",
										entry->header.serial[1],
										entry->header.serial[0],
										entry->header.timestamp);
									stashed_entries.available = true;
								}
							}

							/* check that entry will fit in the aws send buffer */
							if (ent_size > 0 &&
							    (ent_size + sizeof(struct ct_publish_header_t)) <
								    sizeof(aws_work.buf)) {
								/* fill in the ct_publish_header_t */
								struct ct_publish_header_t pub_hdr;
								pub_hdr.entry_protocol_version =
									dfu_smp_c->ct_log_header.entry_protocol_version;
								memcpy(pub_hdr.device_id,
								       dfu_smp_c->ct_log_header.device_id,
								       BT_MAC_ADDR_LEN);
								pub_hdr.device_time = lcz_qrtc_get_epoch();
								pub_hdr.last_upload_time =
									dfu_smp_c->ct_log_header.last_upload_time;
								memcpy(pub_hdr.fw_version,
								       dfu_smp_c->ct_log_header.local_info.fw_version,
								       sizeof(pub_hdr.fw_version));
								pub_hdr.battery_level =
									dfu_smp_c->ct_log_header.local_info
										.battery_level;
								pub_hdr.network_id =
									dfu_smp_c->ct_log_header.local_info.network_id;
								memcpy(aws_work.buf, &pub_hdr,
								       sizeof(struct ct_publish_header_t));
								memcpy(&aws_work.buf[sizeof(
									       struct ct_publish_header_t)],
								       &log_buffer[ent_offset], ent_size);
								aws_work.buf_len =
									sizeof(struct ct_publish_header_t) + ent_size;

								LOG_VRB(">> %d %d %d", record_bytes_in_entry,
									aws_work.buf_len,
									sizeof(aws_work.buf));

								ct.aws_publish_state =
									AWS_PUBLISH_STATE_PENDING; /* set state to indicate waiting on publish result - check for publish success on next run of this function */

								/* Preemptively put in stash if fits. If publish is successful, remove it from stash */
								if ((stashed_entries.len +
								     sizeof(struct ct_publish_header_t) + ent_size) <
								    CONFIG_CT_LOG_DOWNLOAD_BUFFER_SIZE) {
									if (stashed_entries.len == 0) {
										memcpy(stashed_entries.buffer, &pub_hdr,
										       sizeof(struct ct_publish_header_t));
										stashed_entries.len += sizeof(
											struct ct_publish_header_t);
										stashed_entries.idx = sizeof(
											struct ct_publish_header_t);
									}

									memcpy(&stashed_entries.buffer[stashed_entries.len],
									       &log_buffer[ent_offset], ent_size);
									stashed_entries.len += ent_size;
									stashed_entries.prev_ent_size =
										ent_size; /* need a static variable to hold this value since ent_size will be updated before ct.aws_publish_state can be checked. */

								} else {
									LOG_ERR("No space left in entry stash, have to discard entry");
								}

							} else {
								if (ent_size == 0) {
									LOG_DBG("0 records found in entry");
								} else {
									LOG_DBG("skipping AWS publish, entry too large for buffer");
								}
							}

							/* Send the data to AWS via work queue item */
							k_work_submit(&aws_work.work);
						}
#endif

						ent_idx++;

						ent_offset +=
							ent_size +
							sizeof(uint16_t); /* + sizeof(uint16_t) accounts for CRC16 from transfer, not counted in ent_size */
						dfu_smp_c->ent_cnt++;
					} else {
						/* abort the transfer */
						LOG_DBG("\033[1;31m[ENT%3d] rcv_crc: %04x, calc_crc: %04x CRC MISMATCH",
							ent_idx, crctmp, crcval);
						LOG_ERR("CRC mismatch in entry");
						dfu_smp_c->rsp_state.rc = MGMT_ERR_ENOENT;
						return;
					}
				} while (ent_offset < data_len);

#if defined(CONFIG_CT_AWS_PUBLISH_ENTRIES)
				/* If this was the last entry, need to wait for publish completion
				 * Have to check here because if there was a failure, the rest of the log won't be downloaded immediately and so
				 * status of the last entry needs to be checked to ensure stash is in correct state
				 */
				if (ct.aws_publish_state == AWS_PUBLISH_STATE_PENDING) {
					if ((k_sem_take(&sending_to_aws_sem, SEND_TO_AWS_TIMEOUT_TICKS) != 0)) {
						LOG_ERR("ble->aws pub timeout");
						/* If publish times out, it must have failed. */
						ct.aws_publish_state = AWS_PUBLISH_STATE_FAIL;
					} else {
						/* Semaphore was taken. Only used to synchronize publish completion and no longer needed. */
						k_sem_give(&sending_to_aws_sem);
					}

					/* Check publish result if an entry was previously published (STATE_NONE indicates no publish has occurred yet) */
					if (ct.aws_publish_state == AWS_PUBLISH_STATE_SUCCESS) {
						/* Decrement stash length to discard the successfully published entry */
						stashed_entries.len -= stashed_entries.prev_ent_size;
					} else /* AWS_PUBLISH_STATE_FAIL || AWS_PUBLISH_STATE_PENDING */
					{
						log_entry_t *entry = (log_entry_t *)&stashed_entries.buffer
							[stashed_entries.len -
							 stashed_entries.prev_ent_size]; /* need to offset backwards to point to recently added entry */
						LOG_DBG("Stash entry %02x%02x, %d", entry->header.serial[1],
							entry->header.serial[0], entry->header.timestamp);
						stashed_entries.available = true;
					}
					ct.aws_publish_state =
						AWS_PUBLISH_STATE_NONE; /* reset state for next CT log download */
				} else {
					ct.aws_publish_state =
						AWS_PUBLISH_STATE_NONE; /* reset state for next CT log download */
				}
#endif
			} break;

			default:
				break;
			}
		}

		LOG_VRB("%d", dfu_smp_c->downloaded_bytes);
		LOG_VRB("\033[38;5;68m\033[8D->%02d%%    ",
			(uint16_t)(((float)dfu_smp_c->downloaded_bytes / (float)dfu_smp_c->file_size) * 100));

		dfu_smp_c->downloaded_bytes += data_len;

		if (dfu_smp_c->downloaded_bytes > 0 && dfu_smp_c->downloaded_bytes == dfu_smp_c->file_size) {
			LOG_VRB("\033[38;5;68m\033[8D---]\033[1;36m Done (%d bytes)\033[0m",
				dfu_smp_c->downloaded_bytes);
			/*  green */
			LOG_VRB("\033[0;32m[%02X %02X %02X %02X...%02X %02X]", log_buffer[0], log_buffer[1],
				log_buffer[2], log_buffer[3], log_buffer[dfu_smp_c->file_size - 2],
				log_buffer[dfu_smp_c->file_size - 1]);

			if (dfu_smp_c->entry_protocol_version == 1) {
				LOG_DBG("\033[38;5;46m%d %s", dfu_smp_c->ent_cnt,
					dfu_smp_c->ent_cnt > 1 ? "entries" : "entry");
			} else if (dfu_smp_c->entry_protocol_version == 2) {
				LOG_DBG("\033[38;5;51m%d %s, %d rec", dfu_smp_c->ent_cnt,
					dfu_smp_c->ent_cnt > 1 ? "entries" : "entry", dfu_smp_c->rec_cnt);
			}

			ct.num_download_completions++;

			/* If there was no d/c or timeout after downloading entire log, then can clear entry stash */
			if (!stashed_entries.available) {
				ResetEntryStashInformation(false);
			}
			ct.log_publishing = false;
		}
	}
}
/* clang-format on */

static int send_smp_challenge_request(struct bt_gatt_dfu_smp_c *dfu_smp_c,
				      const char *filename, uint32_t offset)
{
	static struct smp_buffer smp_cmd;
	CborEncoder cbor, cbor_map;
	size_t payload_len;
	struct cbor_buf_writer writer;

	cbor_buf_writer_init(&writer, smp_cmd.payload, sizeof(smp_cmd.payload));
	cbor_encoder_init(&cbor, &writer.enc, 0);
	cbor_encoder_create_map(&cbor, &cbor_map, 2);
	cbor_encode_text_stringz(&cbor_map, "name");
	cbor_encode_text_stringz(&cbor_map, filename);
	cbor_encode_text_stringz(&cbor_map, "off");
	cbor_encode_int(&cbor_map, offset);
	cbor_encoder_close_container(&cbor, &cbor_map);

	payload_len = (size_t)(writer.ptr - smp_cmd.payload);
	LOG_VRB("payload is %d bytes", payload_len);

	smp_cmd.header.op = MGMT_OP_READ;
	smp_cmd.header.flags = 0;
	smp_cmd.header.len_h8 = (uint8_t)((payload_len >> 8) & 0xFF);
	smp_cmd.header.len_l8 = (uint8_t)((payload_len >> 0) & 0xFF);
	smp_cmd.header.group_h8 = 0;
	smp_cmd.header.group_l8 = MGMT_GROUP_ID_FS;
	smp_cmd.header.seq = 0;
	smp_cmd.header.id = FS_MGMT_ID_FILE;

	/* clear the smp_rsp_buff */
	memset((uint8_t *)&smp_rsp_buff, 0, sizeof(smp_rsp_buff));

	return bt_gatt_dfu_smp_c_command(dfu_smp_c,
					 smp_challenge_req_proc_handler,
					 sizeof(smp_cmd.header) + payload_len,
					 &smp_cmd);
}

static int send_smp_challenge_response(struct bt_gatt_dfu_smp_c *dfu_smp_c,
				       const char *filename, uint32_t offset,
				       uint8_t *pData, uint32_t dataLen)
{
	static struct smp_buffer smp_cmd;
	CborEncoder cbor, cbor_map;
	size_t payload_len;
	struct cbor_buf_writer writer;

	cbor_buf_writer_init(&writer, smp_cmd.payload, sizeof(smp_cmd.payload));
	cbor_encoder_init(&cbor, &writer.enc, 0);
	cbor_encoder_create_map(&cbor, &cbor_map, 4);
	cbor_encode_text_stringz(&cbor_map, "name");
	cbor_encode_text_stringz(&cbor_map, filename);
	cbor_encode_text_stringz(&cbor_map, "off");
	cbor_encode_int(&cbor_map, offset);
	cbor_encode_text_stringz(&cbor_map, "data");
	cbor_encode_byte_string(&cbor_map, pData, dataLen);
	cbor_encode_text_stringz(&cbor_map, "len");
	cbor_encode_uint(&cbor_map, dataLen);
	cbor_encoder_close_container(&cbor, &cbor_map);

	payload_len = (size_t)(writer.ptr - smp_cmd.payload);

	smp_cmd.header.op = MGMT_OP_WRITE;
	smp_cmd.header.flags = 0;
	smp_cmd.header.len_h8 = (uint8_t)((payload_len >> 8) & 0xFF);
	smp_cmd.header.len_l8 = (uint8_t)((payload_len >> 0) & 0xFF);
	smp_cmd.header.group_h8 = 0;
	smp_cmd.header.group_l8 = MGMT_GROUP_ID_FS;
	smp_cmd.header.seq = 0;
	smp_cmd.header.id = FS_MGMT_ID_FILE;

	/* clear the smp_rsp_buff */
	memset((uint8_t *)&smp_rsp_buff, 0, sizeof(smp_rsp_buff));

	return bt_gatt_dfu_smp_c_command(dfu_smp_c,
					 smp_challenge_rsp_proc_handler,
					 sizeof(smp_cmd.header) + payload_len,
					 &smp_cmd);
}

static int send_smp_download_request(struct bt_gatt_dfu_smp_c *dfu_smp_c,
				     const char *filename, uint32_t offset)
{
	static struct smp_buffer smp_cmd;
	CborEncoder cbor, cbor_map;
	size_t payload_len;
	struct cbor_buf_writer writer;

	/* if continuing a download, caller may send NULL as filename */
	if (filename == NULL) {
		filename = smp_fs_download_filename;
	}

	cbor_buf_writer_init(&writer, smp_cmd.payload, sizeof(smp_cmd.payload));
	cbor_encoder_init(&cbor, &writer.enc, 0);
	cbor_encoder_create_map(&cbor, &cbor_map, 2);
	cbor_encode_text_stringz(&cbor_map, "name");
	cbor_encode_text_stringz(&cbor_map, filename);
	cbor_encode_text_stringz(&cbor_map, "off");
	cbor_encode_int(&cbor_map, offset);
	cbor_encoder_close_container(&cbor, &cbor_map);

	payload_len = (size_t)(writer.ptr - smp_cmd.payload);
	LOG_VRB("payload is %d bytes", payload_len);

	smp_cmd.header.op = MGMT_OP_READ;
	smp_cmd.header.flags = 0;
	smp_cmd.header.len_h8 = (uint8_t)((payload_len >> 8) & 0xFF);
	smp_cmd.header.len_l8 = (uint8_t)((payload_len >> 0) & 0xFF);
	smp_cmd.header.group_h8 = 0;
	smp_cmd.header.group_l8 = MGMT_GROUP_ID_FS;
	smp_cmd.header.seq = 0;
	smp_cmd.header.id = FS_MGMT_ID_FILE;

	/* clear the smp_rsp_buff */
	memset((uint8_t *)&smp_rsp_buff, 0, sizeof(smp_rsp_buff));

	return bt_gatt_dfu_smp_c_command(dfu_smp_c, smp_file_download_rsp_proc,
					 sizeof(smp_cmd.header) + payload_len,
					 &smp_cmd);
}

static void smp_challenge_req_work_handler(struct k_work *work)
{
	int ret;

	snprintk(smp_fs_download_filename, sizeof(smp_fs_download_filename),
		 "/sys/challenge.bin");

	bt_gatt_dfu_smp_c_init(&dfu_smp_c, NULL);
	ret = send_smp_challenge_request(&dfu_smp_c, smp_fs_download_filename,
					 0);
	if (ret) {
		LOG_WRN("Authenticate device command send error (err: %d)",
			ret);
		bt_conn_disconnect(remote.conn,
				   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	} else {
		set_ble_state(BT_DEMO_APP_STATE_CHALLENGE_REQ);
		k_timer_start(&smp_xfer_timeout_timer, SMP_TIMEOUT_TICKS,
			      K_NO_WAIT);
	}
}

static void smp_fs_download_work_handler(struct k_work *work)
{
	int ret;

	snprintk(smp_fs_download_filename, sizeof(smp_fs_download_filename),
		 "/log/ct");

	bt_gatt_dfu_smp_c_init(&dfu_smp_c, NULL);
	ret = send_smp_download_request(&dfu_smp_c, smp_fs_download_filename,
					0);
	if (ret) {
		LOG_WRN("Download command send error (err: %d)", ret);
		bt_conn_disconnect(remote.conn,
				   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	} else {
		set_ble_state(BT_DEMO_APP_STATE_LOG_DOWNLOAD);
		k_timer_start(&smp_xfer_timeout_timer, SMP_TIMEOUT_TICKS,
			      K_NO_WAIT);
	}
}

static void change_advert_type_work_handler(struct k_work *work)
{
	bt_le_adv_stop();
	k_timer_stop(&update_advert_timer);
	start_advertising();
}

/* clang-format off */
static void send_stashed_entries_work_handler(struct k_work *work)
{
	/* Make sure AWS is connected, and no other log download is occurring (normally over BLE) */
	if (Bluegrass_ReadyForPublish() && (remote.app_state == BT_DEMO_APP_STATE_FINDING_DEVICE)) {
		if (stashed_entries.available) {
			if (stashed_entries.len >
			    sizeof(struct ct_publish_header_t)) /* make sure ct header has been copied */
			{
				if (k_sem_take(&sending_to_aws_sem, SEND_TO_AWS_TIMEOUT_TICKS) != 0) {
					stashed_entries.timeouts++;

					if (stashed_entries.timeouts > STASH_ENTRY_FAILURE_CNT_MAX) {
						LOG_ERR("Entry stash publish semaphore timed out (%d). Reset stash and continue...",
							stashed_entries.timeouts);
						stashed_entries.timeouts = 0;
						ResetEntryStashInformation(false); /* semaphore take timed out - another task must have it so this function shouldn't give */
						return;
					}
					k_work_submit(&send_stashed_entries_work);
				} else {
					ct.log_publishing = true;
					stashed_entries.timeouts = 0;

					/* Check status of previous publish */
					if (ct.aws_publish_state != AWS_PUBLISH_STATE_NONE) {
						if (ct.aws_publish_state == AWS_PUBLISH_STATE_SUCCESS) {
							stashed_entries.idx += stashed_entries.prev_ent_size;
						} else /* AWS_PUBLISH_STATE_FAIL || AWS_PUBLISH_STATE_PENDING */
						{
							stashed_entries.failure_cnt++;
							if (stashed_entries.failure_cnt > STASH_ENTRY_FAILURE_CNT_MAX) {
								/* Too many failures so just have to move onto next entry */
								LOG_ERR("Entry stash publish failed to max (%d). Move to next entry and continue...",
									stashed_entries.failure_cnt);
								stashed_entries.idx += stashed_entries.prev_ent_size;
								stashed_entries.failure_cnt = 0;

								/* after 2 minutes of failures, AWS unlikely to be working so just reboot */
								lcz_software_reset(1000);
							}
						}
					}

					if (stashed_entries.idx >= stashed_entries.len) {
						/* All stashed entries have been sent */
						LOG_DBG("Entry Stash all sent. Reset stash and continue normal operation...");
						ResetEntryStashInformation(true);
						return;
					}

					/* iterate over the downloaded bytes parsing each entry */
					log_entry_t *entry = (log_entry_t *)&stashed_entries.buffer[stashed_entries.idx];
					if (entry->header.entryStart != LOG_ENTRY_START_BYTE) {
						/* this is not an entry, break (could be padding bytes if encryption is enabled) */
						LOG_ERR("Stash entry not found - 0x%02X not equal to start byte %02X",
							entry->header.entryStart, LOG_ENTRY_START_BYTE);
						ResetEntryStashInformation(true);
						return;
					}

					/* Verify index seems valid after finding an entry */
					if (stashed_entries.idx < stashed_entries.len) {
						uint16_t ent_size = *((uint16_t *)(&entry->header.reserved[0]));

						/* Validate entry size and then send next stashed */
						if ((ent_size <= LOG_ENTRY_MAX_SIZE) &&
						    stashed_entries.idx + ent_size <= stashed_entries.len) {
							uint32_t epoch = lcz_qrtc_get_epoch();
							*((uint32_t *)&stashed_entries.buffer[offsetof(
								struct ct_publish_header_t, device_time)]) = epoch;

							memcpy(aws_work.buf, stashed_entries.buffer,
							       sizeof(struct ct_publish_header_t));
							memcpy(&aws_work.buf[sizeof(struct ct_publish_header_t)],
							       &stashed_entries.buffer[stashed_entries.idx], ent_size);
							aws_work.buf_len =
								sizeof(struct ct_publish_header_t) + ent_size;
							ct.aws_publish_state =
								AWS_PUBLISH_STATE_PENDING; /* set state to indicate waiting on publish result - check for publish success on next run of this function */
							stashed_entries.prev_ent_size =
								ent_size; /* store entry size to increment index accordingly on next run of this function */

							/* Send the data to AWS via work queue item */
							k_work_submit(&aws_work.work); /* will eventually give sending_to_aws_sem */

							k_work_submit(&send_stashed_entries_work); /* queue next run to send next stashed entry or finish sending stash */
						} else {
							LOG_WRN("Stash entry size exceeds buffer length %d + %d > %d.  Reset stash and continue...",
								stashed_entries.idx, ent_size, stashed_entries.len);
							ResetEntryStashInformation(true);
						}
					} else {
						LOG_WRN("Stash Index greater than buffer length %d > %d. Reset stash and continue...",
							stashed_entries.idx, stashed_entries.len);
						ResetEntryStashInformation(true);
					}
				}
			} else {
				/* No entries seem to be stashed. Reset stash information. */
				ResetEntryStashInformation(true);
			}
		} else {
			/* Clear stash info */
			ResetEntryStashInformation(false);
		}
	}
}
/* clang-format on */

static void ResetEntryStashInformation(bool giveSemaphore)
{
	memset(stashed_entries.buffer, 0, sizeof(stashed_entries.buffer));
	stashed_entries.len = 0;
	stashed_entries.idx = 0;
	stashed_entries.available = false;
	stashed_entries.timeouts = 0;
	stashed_entries.failure_cnt = 0;
	stashed_entries.prev_ent_size = 0;
	ct.aws_publish_state = AWS_PUBLISH_STATE_NONE;
	ct.log_publishing = false;

	if (giveSemaphore) {
		k_sem_give(&sending_to_aws_sem);
	}
}

static void smp_xfer_timeout_handler(struct k_timer *dummy)
{
	/* a timeout has occurred waiting for an SMP response from the sensor, disconnect */
	if (remote.conn) {
		LOG_ERR("SMP timeout");
		bt_conn_disconnect(remote.conn,
				   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static void set_ble_state(enum sensor_state state)
{
	if (remote.app_state != state) {
		LOG_DBG("%s->%s", get_sensor_state_string(remote.app_state),
			get_sensor_state_string(state));
		remote.app_state = state;
		bss_set_sensor_state(state);
	}

	switch (state) {
	case BT_DEMO_APP_STATE_CONNECTED_AND_CONFIGURED:
		lcz_led_turn_on(BLUETOOTH_LED);
		break;

	case BT_DEMO_APP_STATE_FINDING_DEVICE:
		lcz_led_blink(BLUETOOTH_LED, &CT_LED_SENSOR_SEARCH_PATTERN);
		bss_set_sensor_bt_addr(NULL);
		lcz_bt_scan_restart(ct.scan_id);
		break;

	default:
		/* Nothing needs to be done for these states. */
		break;
	}
}

static void ct_adv_watchdog_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_WRN("Advertisement not received in the last hour");
	lcz_software_reset(0);
}

static bool ct_ble_remote_active_handler(void)
{
	/* Init to active in case there is no CT connection */
	bool active = true;

	if (remote.conn != NULL) {
		if (!remote.log_ble_xfer_active) {
			active = false;
		}
	}

	remote.log_ble_xfer_active = false;

	return active;
}

static void ct_conn_inactivity_work_handler(struct k_work *work)
{
	/* If peripheral bt is connected but no data is being sent, then disconnect. */
	if (!ct_ble_remote_active_handler()) {
		remote.inactivity +=
			CONFIG_CT_CONN_INACTIVITY_TICK_RATE_SECONDS;
		if (remote.inactivity >=
		    CONFIG_CT_CONN_INACTIVITY_WATCHDOG_TIMEOUT) {
			/* Depending on when the connection occurs, this timeout
			 * could be 2 or 3 minutes
			 */
			LOG_WRN("Detected inactive BT link %d, disconnect",
				remote.inactivity);
			remote.inactivity = 0;
			disconnect_sensor();
		}
	} else {
		remote.inactivity = 0;
	}
}

static void disable_connectable_adv_work_handler(struct k_work *work)
{
	change_advert_type(ADV_TYPE_NONCONN);
}

/*
 * Add work to system work queue to stop scanning
 */
static void update_advert_timer_handler(struct k_timer *dummy)
{
	k_work_submit(&update_advert_work);
}

/*
 * Update the advertisement data based on device state
 */
static void update_advert(struct k_work *work)
{
	bool commissioned;
	nvReadCommissioned(&commissioned);

	if (commissioned) {
		if (lcz_qrtc_epoch_was_set()) {
			ct_mfg_data.flags |= CT_ADV_FLAGS_HAS_EPOCH_TIME;
			ct_mfg_data.epoch = lcz_qrtc_get_epoch();
		}

		int rc = bt_le_adv_update_data(contact_tracing_ad,
					       ARRAY_SIZE(contact_tracing_ad),
					       NULL, 0);
		if (rc) {
			LOG_ERR("Adv data update failure (rc %d)", rc);
		}
	}
}

static void change_advert_type(enum adv_type adv_type)
{
	ct.adv_type = adv_type;
	if (ct.ble_initialized) {
		k_work_submit(&change_advert_type_work);
	} else {
		LOG_ERR("CT BLE not initialized");
	}
}

static bool is_encryption_enabled(void)
{
	bool blankKey = true;
	uint8_t key[AES_KEY_SIZE];

	memset(key, AES_BLANK_KEY_BYTE_VALUE, AES_KEY_SIZE);
	nvReadAesKey(key);

	for (int i = 0; i < AES_KEY_SIZE; i++) {
		if (key[i] != 0xFF) {
			blankKey = false;
		}
	}

	return !blankKey; /* blank key means encryption is disabled */
}

static uint32_t validate_hw_compatibility(const struct device *dev)
{
	uint32_t flags = 0U;

	flags = cipher_query_hwcaps(dev);
	if ((flags & CAP_RAW_KEY) == 0U) {
		LOG_INF("Please provision the key separately "
			"as the module does not support a raw key");
		return -1;
	}

	if ((flags & CAP_SYNC_OPS) == 0U) {
		LOG_ERR("The app assumes sync semantics. "
			"Please rewrite the app accordingly before proceeding");
		return -1;
	}

	if ((flags & CAP_SEPARATE_IO_BUFS) == 0U) {
		LOG_ERR("The app assumes distinct IO buffers. "
			"Please rewrite the app accordingly before proceeding");
		return -1;
	}

	return CAP_RAW_KEY | CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS;
}

static uint32_t encrypt_cbc(uint8_t *data, uint32_t dataLen, uint8_t *encrypted,
			    uint32_t encrypted_data_len_max, uint8_t *key,
			    uint8_t key_size)
{
	int rc = 0;

	if (crypto_dev == NULL) {
		return rc;
	}

	struct cipher_ctx ini = {
		.keylen = key_size,
		.key.bit_stream = key,
		.flags = crypto_cap_flags,
	};

	struct cipher_pkt encrypt = {
		.in_buf = data,
		.in_len = dataLen,
		.out_buf_max = encrypted_data_len_max,
		.out_buf = encrypted,
	};

	uint8_t iv[AES_CBC_IV_SIZE];
	for (int i = 0; i < AES_CBC_IV_SIZE; i += 4) {
		uint32_t val = sys_rand32_get();
		memcpy(&iv[i], &val, sizeof(uint32_t));
	}

	rc = cipher_begin_session(crypto_dev, &ini, CRYPTO_CIPHER_ALGO_AES,
				  CRYPTO_CIPHER_MODE_CBC,
				  CRYPTO_CIPHER_OP_ENCRYPT);
	if (rc) {
		LOG_ERR("ENCRYPT begin session - Failed");
		return 0;
	}

	bool success = true;
	if (cipher_cbc_op(&ini, &encrypt, iv)) {
		LOG_ERR("ENCRYPT - Failed");
		success = false;
		encrypt.out_len = 0;
	}

	if (success) {
		LOG_DBG("Encryption success. Output length: %d",
			encrypt.out_len);
	}

	cipher_free_session(crypto_dev, &ini);

	/* TinyCrypt does not include IV size in out_len
	 * (though it includes IV bytes in the output buffer)
	 */
	if (success && (encrypt.out_len > 0)) {
		return encrypt.out_len + sizeof(iv);
	} else {
		return 0;
	}
}

static uint32_t decrypt_cbc(uint8_t *encrypted, uint32_t encrypted_data_len,
			    uint8_t *decrypted, uint32_t decrypted_data_len_max,
			    uint8_t *key, uint8_t key_size)
{
	int rc = 0;

	if (crypto_dev == NULL) {
		return rc;
	}

	struct cipher_ctx ini = {
		.keylen = key_size,
		.key.bit_stream = key,
		.flags = crypto_cap_flags,
	};
	struct cipher_pkt decrypt = {
		.in_buf = encrypted,
		.in_len = encrypted_data_len,
		.out_buf = decrypted,
		.out_buf_max = decrypted_data_len_max,
	};

	rc = cipher_begin_session(crypto_dev, &ini, CRYPTO_CIPHER_ALGO_AES,
				  CRYPTO_CIPHER_MODE_CBC,
				  CRYPTO_CIPHER_OP_DECRYPT);
	if (rc) {
		LOG_ERR("DECRYPT begin session - Failed");
		return 0;
	}

	/* input data buffer must include IV at start */
	bool success = true;
	if (cipher_cbc_op(&ini, &decrypt, encrypted)) {
		LOG_ERR("DECRYPT - Failed");
		success = false;
		decrypt.out_len = 0;
	}

	if (success) {
		LOG_DBG("Decryption success. Output length: %d",
			decrypt.out_len);
	}

	cipher_free_session(crypto_dev, &ini);

	/* TinyCrypt does include IV size in out_len
	 * (though it does not include IV bytes in the output buffer)
	 */
	if (success && (decrypt.out_len > AES_CBC_IV_SIZE)) {
		return decrypt.out_len - AES_CBC_IV_SIZE;
	} else {
		return 0;
	}
}

static void aws_work_handler(struct k_work *item)
{
	ARG_UNUSED(item);

	/* If not connected to AWS, set state to fail so entry is stashed. */
	if (!awsConnected()) {
		disconnect_sensor();
		ct.aws_publish_state = AWS_PUBLISH_STATE_FAIL;
	} else {
#if defined(CONFIG_CT_AWS_PUBLISH_ENTRIES)
		/* perform the AWS send in system context */
		int rc = awsSendBinData(aws_work.buf, aws_work.buf_len,
					ct.up_topic);
		if (rc != 0) {
			disconnect_sensor();
			ct.aws_publish_state = AWS_PUBLISH_STATE_FAIL;
		} else {
			ct.aws_publish_state = AWS_PUBLISH_STATE_SUCCESS;
		}
#endif
	}

	k_sem_give(&sending_to_aws_sem);
}