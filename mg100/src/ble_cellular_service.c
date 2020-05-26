/**
 * @file ble_cellular_service.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(mg100_cell_svc);

#define CELL_SVC_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define CELL_SVC_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define CELL_SVC_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define CELL_SVC_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>

#include "mg100_common.h"
#include "laird_bluetooth.h"
#include "ble_cellular_service.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define CELL_SVC_BASE_UUID_128(_x_)                                            \
	BT_UUID_INIT_128(0x36, 0xa3, 0x4d, 0x40, 0xb6, 0x70, 0x69, 0xa6, 0xb1, \
			 0x4e, 0x84, 0x9e, LSB_16(_x_), MSB_16(_x_), 0x78,     \
			 0x43)

static struct bt_uuid_128 CELL_SVC_UUID = CELL_SVC_BASE_UUID_128(0x7c60);
static struct bt_uuid_128 IMEI_UUID = CELL_SVC_BASE_UUID_128(0x7c61);
static struct bt_uuid_128 APN_UUID = CELL_SVC_BASE_UUID_128(0x7c62);
static struct bt_uuid_128 APN_USERNAME_UUID = CELL_SVC_BASE_UUID_128(0x7c63);
static struct bt_uuid_128 APN_PASSWORD_UUID = CELL_SVC_BASE_UUID_128(0x7c64);
static struct bt_uuid_128 NETWORK_STATE_UUID = CELL_SVC_BASE_UUID_128(0x7c65);
static struct bt_uuid_128 FW_VERSION_UUID = CELL_SVC_BASE_UUID_128(0x7c66);
static struct bt_uuid_128 STARTUP_STATE_UUID = CELL_SVC_BASE_UUID_128(0x7c67);
static struct bt_uuid_128 RSSI_UUID = CELL_SVC_BASE_UUID_128(0x7c68);
static struct bt_uuid_128 SINR_UUID = CELL_SVC_BASE_UUID_128(0x7c69);
static struct bt_uuid_128 SLEEP_STATE_UUID = CELL_SVC_BASE_UUID_128(0x7c6a);
static struct bt_uuid_128 RAT_UUID = CELL_SVC_BASE_UUID_128(0x7c6b);
static struct bt_uuid_128 ICCID_UUID = CELL_SVC_BASE_UUID_128(0x7c6c);
static struct bt_uuid_128 SERIAL_NUMBER_UUID = CELL_SVC_BASE_UUID_128(0x7c6d);
static struct bt_uuid_128 BANDS_UUID = CELL_SVC_BASE_UUID_128(0x7c6e);
static struct bt_uuid_128 ACTIVE_BANDS_UUID = CELL_SVC_BASE_UUID_128(0x7c6f);

struct ble_cellular_service {
	char imei_value[MDM_HL7800_IMEI_SIZE];
	struct mdm_hl7800_apn apn;
	u8_t network_state;
	char fw_ver_value[MDM_HL7800_REVISION_MAX_SIZE];
	u8_t startup_state;
	s32_t rssi;
	s32_t sinr;
	u8_t sleep_state;
	u8_t rat; /* radio access technology (CAT-M1 or NB1) */
	u8_t iccid[MDM_HL7800_ICCID_SIZE];
	char serial_number[MDM_HL7800_SERIAL_NUMBER_SIZE];
	char bands[MDM_HL7800_LTE_BAND_STR_SIZE];
	char active_bands[MDM_HL7800_LTE_BAND_STR_SIZE];

	u16_t apn_index;
	u16_t apn_username_index;
	u16_t apn_password_index;
	u16_t network_state_index;
	u16_t startup_state_index;
	u16_t rssi_index;
	u16_t sinr_index;
	u16_t sleep_state_index;
	u16_t rat_index;
	u16_t active_bands_index;
};

struct ccc_table {
	struct lbt_ccc_element apn_value;
	struct lbt_ccc_element apn_username;
	struct lbt_ccc_element apn_password;
	struct lbt_ccc_element network_state;
	struct lbt_ccc_element startup_state;
	struct lbt_ccc_element rssi;
	struct lbt_ccc_element sinr;
	struct lbt_ccc_element sleep_state;
	struct lbt_ccc_element rat;
	struct lbt_ccc_element active_bands;
};

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct ble_cellular_service bcs;
static struct ccc_table ccc;
static struct bt_conn *(*get_connection_handle_fptr)(void);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static ssize_t read_imei(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, u16_t len, u16_t offset);
static ssize_t read_apn(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, u16_t len, u16_t offset);
static ssize_t update_apn_in_modem(ssize_t length);
static ssize_t write_apn(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags);
static ssize_t read_apn_username(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr, void *buf,
				 u16_t len, u16_t offset);
static ssize_t read_apn_password(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr, void *buf,
				 u16_t len, u16_t offset);
static ssize_t read_fw_ver(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, void *buf,
			   u16_t len, u16_t offset);
static ssize_t write_rat(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags);
static ssize_t read_iccid(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, u16_t len, u16_t offset);
static ssize_t read_serial_number(struct bt_conn *conn,
				  const struct bt_gatt_attr *attr, void *buf,
				  u16_t len, u16_t offset);
static void apn_value_ccc_handler(const struct bt_gatt_attr *attr, u16_t value);
static void apn_username_ccc_handler(const struct bt_gatt_attr *attr,
				     u16_t value);
static void apn_password_ccc_handler(const struct bt_gatt_attr *attr,
				     u16_t value);
static void network_state_ccc_handler(const struct bt_gatt_attr *attr,
				      u16_t value);
static void startup_state_ccc_handler(const struct bt_gatt_attr *attr,
				      u16_t value);
static void rssi_ccc_handler(const struct bt_gatt_attr *attr, u16_t value);
static void sinr_ccc_handler(const struct bt_gatt_attr *attr, u16_t value);
static void sleep_state_ccc_handler(const struct bt_gatt_attr *attr,
				    u16_t value);
static void rat_ccc_handler(const struct bt_gatt_attr *attr, u16_t value);
static void cell_svc_notify(bool notify, u16_t index, u16_t length);
static ssize_t read_bands(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, u16_t len, u16_t offset);
static void active_bands_ccc_handler(const struct bt_gatt_attr *attr,
				     u16_t value);

/******************************************************************************/
/* Cellular Service Declaration                                               */
/******************************************************************************/
static struct bt_gatt_attr cell_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&CELL_SVC_UUID),
	BT_GATT_CHARACTERISTIC(&IMEI_UUID.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_imei, NULL,
			       bcs.imei_value),
	BT_GATT_CHARACTERISTIC(&FW_VERSION_UUID.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_fw_ver, NULL,
			       bcs.fw_ver_value),
	BT_GATT_CHARACTERISTIC(&APN_UUID.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_apn,
			       write_apn, bcs.apn.value),
	LBT_GATT_CCC(apn_value),
	BT_GATT_CHARACTERISTIC(&APN_USERNAME_UUID.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_apn_username, NULL,
			       bcs.apn.username),
	LBT_GATT_CCC(apn_username),
	BT_GATT_CHARACTERISTIC(&APN_PASSWORD_UUID.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_apn_password, NULL,
			       bcs.apn.password),
	LBT_GATT_CCC(apn_password),
	BT_GATT_CHARACTERISTIC(&NETWORK_STATE_UUID.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
			       &bcs.network_state),
	LBT_GATT_CCC(network_state),
	BT_GATT_CHARACTERISTIC(&STARTUP_STATE_UUID.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
			       &bcs.startup_state),
	LBT_GATT_CCC(startup_state),
	BT_GATT_CHARACTERISTIC(
		&RSSI_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, lbt_read_integer, NULL, &bcs.rssi),
	LBT_GATT_CCC(rssi),
	BT_GATT_CHARACTERISTIC(
		&SINR_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, lbt_read_integer, NULL, &bcs.sinr),
	LBT_GATT_CCC(sinr),
	BT_GATT_CHARACTERISTIC(
		&SLEEP_STATE_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, lbt_read_u8, NULL, &bcs.sleep_state),
	LBT_GATT_CCC(sleep_state),
	BT_GATT_CHARACTERISTIC(&RAT_UUID.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       lbt_read_u8, write_rat, &bcs.rat),
	LBT_GATT_CCC(rat),
	BT_GATT_CHARACTERISTIC(&ICCID_UUID.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_iccid, NULL, bcs.iccid),
	BT_GATT_CHARACTERISTIC(&SERIAL_NUMBER_UUID.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_serial_number, NULL,
			       bcs.serial_number),
	BT_GATT_CHARACTERISTIC(&BANDS_UUID.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_bands, NULL, bcs.bands),
	BT_GATT_CHARACTERISTIC(&ACTIVE_BANDS_UUID.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_bands, NULL,
			       bcs.active_bands),
	LBT_GATT_CCC(active_bands),
};

static struct bt_gatt_service cell_svc = BT_GATT_SERVICE(cell_attrs);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void cell_svc_assign_connection_handler_getter(struct bt_conn *(*function)(void))
{
	get_connection_handle_fptr = function;
}

void cell_svc_set_imei(const char *imei)
{
	if (imei) {
		memcpy(bcs.imei_value, imei, MDM_HL7800_IMEI_STRLEN);
	}
}

void cell_svc_set_network_state(u8_t state)
{
	bcs.network_state = state;
	cell_svc_notify(ccc.network_state.notify, bcs.network_state_index,
			sizeof(bcs.network_state));
}

void cell_svc_set_startup_state(u8_t state)
{
	bcs.startup_state = state;
	cell_svc_notify(ccc.startup_state.notify, bcs.startup_state_index,
			sizeof(bcs.startup_state));
}

void cell_svc_set_sleep_state(u8_t state)
{
	bcs.sleep_state = state;
	cell_svc_notify(ccc.sleep_state.notify, bcs.sleep_state_index,
			sizeof(bcs.sleep_state));
}

void cell_svc_set_apn(struct mdm_hl7800_apn *access_point)
{
	__ASSERT_NO_MSG(access_point != NULL);
	memcpy(&bcs.apn, access_point, sizeof(struct mdm_hl7800_apn));
	cell_svc_notify(ccc.apn_value.notify, bcs.apn_index,
			strlen(bcs.apn.value));
	cell_svc_notify(ccc.apn_username.notify, bcs.apn_username_index,
			strlen(bcs.apn.username));
	cell_svc_notify(ccc.apn_password.notify, bcs.apn_password_index,
			strlen(bcs.apn.password));
}

void cell_svc_set_rssi(int value)
{
	bcs.rssi = (s32_t)value;
	cell_svc_notify(ccc.rssi.notify, bcs.rssi_index, sizeof(bcs.rssi));
}

void cell_svc_set_sinr(int value)
{
	bcs.sinr = (s32_t)value;
	cell_svc_notify(ccc.sinr.notify, bcs.sinr_index, sizeof(bcs.sinr));
}

void cell_svc_set_fw_ver(const char *ver)
{
	__ASSERT_NO_MSG(ver != NULL);
	memcpy(bcs.fw_ver_value, ver, MDM_HL7800_REVISION_MAX_STRLEN);
}

void cell_svc_set_rat(u8_t value)
{
	bcs.rat = value;
	cell_svc_notify(ccc.rat.notify, bcs.rat_index, sizeof(bcs.rat));
}

void cell_svc_set_iccid(const char *value)
{
	__ASSERT_NO_MSG(value != NULL);
	memcpy(bcs.iccid, value, MDM_HL7800_ICCID_STRLEN);
}

void cell_svc_set_serial_number(const char *value)
{
	__ASSERT_NO_MSG(value != NULL);
	memcpy(bcs.serial_number, value, MDM_HL7800_SERIAL_NUMBER_STRLEN);
}

void cell_svc_set_bands(char *value)
{
	__ASSERT_NO_MSG(value != NULL);
	memcpy(bcs.bands, value, MDM_HL7800_LTE_BAND_STRLEN);
}

void cell_svc_set_active_bands(char *value)
{
	__ASSERT_NO_MSG(value != NULL);
	memcpy(bcs.active_bands, value, MDM_HL7800_LTE_BAND_STRLEN);
}

void cell_svc_init()
{
	bt_gatt_service_register(&cell_svc);

	size_t gatt_size = (sizeof(cell_attrs) / sizeof(cell_attrs[0]));
	bcs.apn_index =
		lbt_find_gatt_index(&APN_UUID.uuid, cell_attrs, gatt_size);
	bcs.apn_username_index = lbt_find_gatt_index(&APN_USERNAME_UUID.uuid,
						     cell_attrs, gatt_size);
	bcs.apn_password_index = lbt_find_gatt_index(&APN_PASSWORD_UUID.uuid,
						     cell_attrs, gatt_size);
	bcs.network_state_index = lbt_find_gatt_index(&NETWORK_STATE_UUID.uuid,
						      cell_attrs, gatt_size);
	bcs.startup_state_index = lbt_find_gatt_index(&STARTUP_STATE_UUID.uuid,
						      cell_attrs, gatt_size);
	bcs.rssi_index =
		lbt_find_gatt_index(&RSSI_UUID.uuid, cell_attrs, gatt_size);
	bcs.sinr_index =
		lbt_find_gatt_index(&SINR_UUID.uuid, cell_attrs, gatt_size);
	bcs.sleep_state_index = lbt_find_gatt_index(&SLEEP_STATE_UUID.uuid,
						    cell_attrs, gatt_size);
	bcs.rat_index =
		lbt_find_gatt_index(&RAT_UUID.uuid, cell_attrs, gatt_size);

	bcs.active_bands_index = lbt_find_gatt_index(&ACTIVE_BANDS_UUID.uuid,
						     cell_attrs, gatt_size);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static ssize_t read_imei(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, u16_t len, u16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       MDM_HL7800_IMEI_STRLEN);
}

static ssize_t read_apn(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, u16_t len, u16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       MDM_HL7800_APN_MAX_STRLEN);
}

static ssize_t update_apn_in_modem(ssize_t length)
{
	s32_t status = mdm_hl7800_update_apn(bcs.apn.value);
	if (status < 0) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}
	return length;
}

static ssize_t write_apn(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	ssize_t length = lbt_write_string(conn, attr, buf, len, offset, flags,
					  MDM_HL7800_APN_MAX_STRLEN);

	return update_apn_in_modem(length);
}

static ssize_t read_apn_username(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr, void *buf,
				 u16_t len, u16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       MDM_HL7800_APN_USERNAME_MAX_STRLEN);
}

static ssize_t read_apn_password(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr, void *buf,
				 u16_t len, u16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       MDM_HL7800_APN_PASSWORD_MAX_STRLEN);
}

static ssize_t read_fw_ver(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, void *buf,
			   u16_t len, u16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       MDM_HL7800_REVISION_MAX_STRLEN);
}

static ssize_t write_rat(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	if (!mdm_hl7800_valid_rat(*((u8_t *)buf))) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	ssize_t length = lbt_write_u8(conn, attr, buf, len, offset, flags);
	if (length > 0) {
		s32_t status = mdm_hl7800_update_rat(bcs.rat);
		if (status < 0) {
			return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
		}
	}
	return length;
}

static ssize_t read_iccid(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, u16_t len, u16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       MDM_HL7800_ICCID_STRLEN);
}

static ssize_t read_serial_number(struct bt_conn *conn,
				  const struct bt_gatt_attr *attr, void *buf,
				  u16_t len, u16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       MDM_HL7800_SERIAL_NUMBER_STRLEN);
}

static ssize_t read_bands(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, u16_t len, u16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       MDM_HL7800_LTE_BAND_STRLEN);
}

static void apn_value_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.apn_value.notify = IS_NOTIFIABLE(value);
}

static void apn_username_ccc_handler(const struct bt_gatt_attr *attr,
				     u16_t value)
{
	ccc.apn_username.notify = IS_NOTIFIABLE(value);
}

static void apn_password_ccc_handler(const struct bt_gatt_attr *attr,
				     u16_t value)
{
	ccc.apn_password.notify = IS_NOTIFIABLE(value);
}

static void network_state_ccc_handler(const struct bt_gatt_attr *attr,
				      u16_t value)
{
	ccc.network_state.notify = IS_NOTIFIABLE(value);
}

static void startup_state_ccc_handler(const struct bt_gatt_attr *attr,
				      u16_t value)
{
	ccc.startup_state.notify = IS_NOTIFIABLE(value);
}

static void rssi_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.rssi.notify = IS_NOTIFIABLE(value);
}

static void sinr_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.sinr.notify = IS_NOTIFIABLE(value);
}

static void sleep_state_ccc_handler(const struct bt_gatt_attr *attr,
				    u16_t value)
{
	ccc.sleep_state.notify = IS_NOTIFIABLE(value);
}

static void rat_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.rat.notify = IS_NOTIFIABLE(value);
}

static void active_bands_ccc_handler(const struct bt_gatt_attr *attr,
				     u16_t value)
{
	ccc.active_bands.notify = IS_NOTIFIABLE(value);
}

static void cell_svc_notify(bool notify, u16_t index, u16_t length)
{
	if (get_connection_handle_fptr == NULL) {
		return;
	}

	struct bt_conn *connection_handle = get_connection_handle_fptr();
	if (connection_handle != NULL) {
		if (notify) {
			bt_gatt_notify(connection_handle,
				       &cell_svc.attrs[index],
				       cell_svc.attrs[index].user_data, length);
		}
	}
}
