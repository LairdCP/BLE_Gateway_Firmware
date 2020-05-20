/**
 * @file ble_battery_service.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(mg100_battery_svc);

#define BATTERY_SVC_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define BATTERY_SVC_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define BATTERY_SVC_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define BATTERY_SVC_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>

#include "mg100_common.h"
#include "laird_bluetooth.h"
#include "ble_battery_service.h"
#include "power.h"
#include "battery.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define BATTERY_SVC_BASE_UUID_128(_x_)                                           \
	BT_UUID_INIT_128(0x66, 0x9a, 0x0c, 0x20, 0x00, 0x08, 0x12, 0xab, 0xea, \
			0x11, 0x41, 0x96, LSB_16(_x_), MSB_16(_x_), 0x4a, 0x6d)

static struct bt_uuid_128 BATTERY_SVC_UUID = BATTERY_SVC_BASE_UUID_128(0x06b0);
static struct bt_uuid_128 BATTERY_VOLTAGE_UUID = BATTERY_SVC_BASE_UUID_128(0x06b1);
static struct bt_uuid_128 BATTERY_CAP_UUID = BATTERY_SVC_BASE_UUID_128(0x06b2);
static struct bt_uuid_128 BATTERY_CHG_STATE_UUID = BATTERY_SVC_BASE_UUID_128(0x06b3);
static struct bt_uuid_128 BATTERY_THRESH_LOW_UUID = BATTERY_SVC_BASE_UUID_128(0x06b4);
static struct bt_uuid_128 BATTERY_THRESH_ALARM_UUID = BATTERY_SVC_BASE_UUID_128(0x06b5);
static struct bt_uuid_128 BATTERY_THRESH_4_UUID = BATTERY_SVC_BASE_UUID_128(0x06b6);
static struct bt_uuid_128 BATTERY_THRESH_3_UUID = BATTERY_SVC_BASE_UUID_128(0x06b7);
static struct bt_uuid_128 BATTERY_THRESH_2_UUID = BATTERY_SVC_BASE_UUID_128(0x06b8);
static struct bt_uuid_128 BATTERY_THRESH_1_UUID = BATTERY_SVC_BASE_UUID_128(0x06b9);
static struct bt_uuid_128 BATTERY_THRESH_0_UUID = BATTERY_SVC_BASE_UUID_128(0x06ba);
static struct bt_uuid_128 BATTERY_ALARM_UUID = BATTERY_SVC_BASE_UUID_128(0x06bb);

struct ble_battery_service {
	s16_t batt_voltage;
	u16_t batt_voltage_index;
	u16_t batt_cap_index;
	u16_t batt_chg_state_index;
	u16_t batt_alarm_index;
	enum battery_status batt_cap;
	u16_t batt_threshold_low;
	u16_t batt_threshold_alarm;
	u16_t batt_threshold_4;
	u16_t batt_threshold_3;
	u16_t batt_threshold_2;
	u16_t batt_threshold_1;
	u16_t batt_threshold_0;
	u8_t batt_chg_state;
	u8_t batt_alarm;
};

struct ccc_table {
	struct lbt_ccc_element battery_voltage;
	struct lbt_ccc_element battery_cap;
	struct lbt_ccc_element battery_chg_state;
	struct lbt_ccc_element battery_alarm;
};

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct ble_battery_service bps;
static struct ccc_table ccc;
static struct bt_conn *(*get_connection_handle_fptr)(void);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void battery_voltage_ccc_handler(const struct bt_gatt_attr *attr, u16_t value);
static void battery_cap_ccc_handler(const struct bt_gatt_attr *attr, u16_t value);
static void battery_chg_state_ccc_handler(const struct bt_gatt_attr *attr, u16_t value);
static void battery_alarm_ccc_handler(const struct bt_gatt_attr *attr, u16_t value);
static ssize_t write_battery_threshold_low(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags);
static ssize_t write_battery_threshold_alarm(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags);
static ssize_t write_battery_threshold_4(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags);
static ssize_t write_battery_threshold_3(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags);
static ssize_t write_battery_threshold_2(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags);
static ssize_t write_battery_threshold_1(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags);
static ssize_t write_battery_threshold_0(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags);

/******************************************************************************/
/* Battery Service Declaration                                                  */
/******************************************************************************/
static struct bt_gatt_attr battery_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&BATTERY_SVC_UUID),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_VOLTAGE_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, lbt_read_u16, NULL, &bps.batt_voltage),
	LBT_GATT_CCC(battery_voltage),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_CAP_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, lbt_read_u8, NULL, &bps.batt_cap),
	LBT_GATT_CCC(battery_cap),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_CHG_STATE_UUID.uuid, BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
		BT_GATT_PERM_READ, lbt_read_u8, NULL, &bps.batt_chg_state),
	LBT_GATT_CCC(battery_chg_state),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_THRESH_LOW_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, lbt_read_u16, write_battery_threshold_low, &bps.batt_threshold_low),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_THRESH_ALARM_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, lbt_read_u16, write_battery_threshold_alarm, &bps.batt_threshold_alarm),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_THRESH_4_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, lbt_read_u16, write_battery_threshold_4, &bps.batt_threshold_4),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_THRESH_3_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, lbt_read_u16, write_battery_threshold_3, &bps.batt_threshold_3),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_THRESH_2_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, lbt_read_u16, write_battery_threshold_2, &bps.batt_threshold_2),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_THRESH_1_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, lbt_read_u16, write_battery_threshold_1, &bps.batt_threshold_1),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_THRESH_0_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, lbt_read_u16, write_battery_threshold_0, &bps.batt_threshold_0),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_ALARM_UUID.uuid, BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_NONE, NULL, NULL, &bps.batt_alarm),
	LBT_GATT_CCC(battery_alarm)
};


static struct bt_gatt_service battery_svc = BT_GATT_SERVICE(battery_attrs);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void battery_svc_assign_connection_handler_getter(
	struct bt_conn *(*function)(void))
{
	get_connection_handle_fptr = function;
}

static void battery_svc_notify(bool notify, u16_t index, u16_t length)
{
	if (get_connection_handle_fptr == NULL) {
		return;
	}

	struct bt_conn *connection_handle = get_connection_handle_fptr();
	if (connection_handle != NULL) {
		if (notify) {
			bt_gatt_notify(connection_handle,
				       &battery_svc.attrs[index],
				       battery_svc.attrs[index].user_data,
				       length);
		}
	}
}

void battery_svc_set_battery(u16_t voltage, u8_t capacity)
{
	bps.batt_voltage = voltage;
	bps.batt_cap = capacity;

	battery_svc_notify(ccc.battery_voltage.notify, bps.batt_voltage_index,
			 sizeof(bps.batt_voltage));
	battery_svc_notify(ccc.battery_cap.notify, bps.batt_cap_index,
			 sizeof(bps.batt_cap));
}

void battery_svc_set_chg_state(u8_t chgState)
{
	bps.batt_chg_state = chgState;
	battery_svc_notify(ccc.battery_chg_state.notify, bps.batt_chg_state_index,
			 sizeof(bps.batt_chg_state));
}

void battery_svc_set_alarm_state(u8_t alarmState)
{
	bps.batt_alarm = alarmState;
	battery_svc_notify(ccc.battery_alarm.notify, bps.batt_alarm_index,
			 sizeof(bps.batt_alarm));
}

void battery_svc_init()
{
	bt_gatt_service_register(&battery_svc);

	size_t gatt_size = (sizeof(battery_attrs) / sizeof(battery_attrs[0]));
	bps.batt_voltage_index =
		lbt_find_gatt_index(&BATTERY_VOLTAGE_UUID.uuid, battery_attrs, gatt_size);
	bps.batt_cap_index = lbt_find_gatt_index(&BATTERY_CAP_UUID.uuid, battery_attrs,
						gatt_size);
	bps.batt_chg_state_index = lbt_find_gatt_index(&BATTERY_CHG_STATE_UUID.uuid, battery_attrs,
						gatt_size);
	bps.batt_alarm_index = lbt_find_gatt_index(&BATTERY_ALARM_UUID.uuid, battery_attrs,
						gatt_size);
}

void battery_svc_update_data()
{
	bps.batt_threshold_low = BatteryGetThresholds(BATTERY_IDX_LOW);
	bps.batt_threshold_alarm = BatteryGetThresholds(BATTERY_IDX_ALARM);
	bps.batt_threshold_4 = BatteryGetThresholds(BATTERY_IDX_4);
	bps.batt_threshold_3 = BatteryGetThresholds(BATTERY_IDX_3);
	bps.batt_threshold_2 = BatteryGetThresholds(BATTERY_IDX_2);
	bps.batt_threshold_1 = BatteryGetThresholds(BATTERY_IDX_1);
	bps.batt_threshold_0 = BatteryGetThresholds(BATTERY_IDX_0);
	bps.batt_chg_state = BatteryGetChgState();
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void battery_voltage_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.battery_voltage.notify = IS_NOTIFIABLE(value);
}

static void battery_cap_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.battery_cap.notify = IS_NOTIFIABLE(value);
}

static void battery_chg_state_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.battery_chg_state.notify = IS_NOTIFIABLE(value);
}

static void battery_alarm_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.battery_alarm.notify = IS_NOTIFIABLE(value);
}


static ssize_t write_battery_threshold_low(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	ssize_t length = lbt_write_u16(conn, attr, buf, len, offset, flags);
	BatterySetThresholds(BATTERY_IDX_LOW, bps.batt_threshold_low);
	return (length);
}

static ssize_t write_battery_threshold_alarm(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	ssize_t length = lbt_write_u16(conn, attr, buf, len, offset, flags);
	BatterySetThresholds(BATTERY_IDX_ALARM, bps.batt_threshold_alarm);
	return (length);
}

static ssize_t write_battery_threshold_4(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	ssize_t length = lbt_write_u16(conn, attr, buf, len, offset, flags);
	BatterySetThresholds(BATTERY_IDX_4, bps.batt_threshold_4);
	return (length);
}

static ssize_t write_battery_threshold_3(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	ssize_t length = lbt_write_u16(conn, attr, buf, len, offset, flags);
	BatterySetThresholds(BATTERY_IDX_3, bps.batt_threshold_3);
	return (length);
}

static ssize_t write_battery_threshold_2(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	ssize_t length = lbt_write_u16(conn, attr, buf, len, offset, flags);
	BatterySetThresholds(BATTERY_IDX_2, bps.batt_threshold_2);
	return (length);
}

static ssize_t write_battery_threshold_1(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	ssize_t length = lbt_write_u16(conn, attr, buf, len, offset, flags);
	BatterySetThresholds(BATTERY_IDX_1, bps.batt_threshold_1);
	return (length);
}

static ssize_t write_battery_threshold_0(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	ssize_t length = lbt_write_u16(conn, attr, buf, len, offset, flags);
	BatterySetThresholds(BATTERY_IDX_0, bps.batt_threshold_0);
	return (length);
}