/**
 * @file ble_power_service.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(mg100_power_svc);

#define POWER_SVC_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define POWER_SVC_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define POWER_SVC_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define POWER_SVC_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>

#include "mg100_common.h"
#include "laird_bluetooth.h"
#include "ble_power_service.h"
#include "power.h"
#include "battery.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define POWER_SVC_BASE_UUID_128(_x_)                                           \
	BT_UUID_INIT_128(0xeb, 0xb7, 0xb2, 0x67, 0xfb, 0x78, 0x4e, 0xf2, 0x9e, \
			 0x55, 0xd7, 0xf3, LSB_16(_x_), MSB_16(_x_), 0x1c, 0xdc)
static struct bt_uuid_128 POWER_SVC_UUID = POWER_SVC_BASE_UUID_128(0x0000);
static struct bt_uuid_128 REBOOT_UUID = POWER_SVC_BASE_UUID_128(0x0002);
static struct bt_uuid_128 BATTERY_VOLTAGE_UUID = POWER_SVC_BASE_UUID_128(0x0003);
static struct bt_uuid_128 BATTERY_CAP_UUID = POWER_SVC_BASE_UUID_128(0x0004);
static struct bt_uuid_128 BATTERY_CHG_STATE_UUID = POWER_SVC_BASE_UUID_128(0x0005);
static struct bt_uuid_128 BATTERY_THRESH_LOW_UUID = POWER_SVC_BASE_UUID_128(0x0006);
static struct bt_uuid_128 BATTERY_THRESH_ALARM_UUID = POWER_SVC_BASE_UUID_128(0x0007);
static struct bt_uuid_128 BATTERY_THRESH_4_UUID = POWER_SVC_BASE_UUID_128(0x0008);
static struct bt_uuid_128 BATTERY_THRESH_3_UUID = POWER_SVC_BASE_UUID_128(0x0009);
static struct bt_uuid_128 BATTERY_THRESH_2_UUID = POWER_SVC_BASE_UUID_128(0x000a);
static struct bt_uuid_128 BATTERY_THRESH_1_UUID = POWER_SVC_BASE_UUID_128(0x000b);
static struct bt_uuid_128 BATTERY_THRESH_0_UUID = POWER_SVC_BASE_UUID_128(0x000c);
static struct bt_uuid_128 BATTERY_ALARM_UUID = POWER_SVC_BASE_UUID_128(0x000d);

struct ble_power_service {
	s16_t voltage;
	u16_t voltage_index;
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
#ifdef CONFIG_REBOOT
	u8_t reboot;
#endif
};

struct ccc_table {
	struct lbt_ccc_element voltage;
	struct lbt_ccc_element battery_cap;
	struct lbt_ccc_element battery_chg_state;
	struct lbt_ccc_element battery_alarm;
};

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct ble_power_service bps;
static struct ccc_table ccc;
static struct bt_conn *(*get_connection_handle_fptr)(void);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void voltage_ccc_handler(const struct bt_gatt_attr *attr, u16_t value);
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
static void init_battery_data();

#ifdef CONFIG_REBOOT
static ssize_t write_power_reboot(struct bt_conn *conn,
				  const struct bt_gatt_attr *attr,
				  const void *buf, u16_t len, u16_t offset,
				  u8_t flags);
#endif

/******************************************************************************/
/* Power Service Declaration                                                  */
/******************************************************************************/
static struct bt_gatt_attr power_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&POWER_SVC_UUID),
	BT_GATT_CHARACTERISTIC(
		&BATTERY_VOLTAGE_UUID.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, lbt_read_u16, NULL, &bps.voltage),
	LBT_GATT_CCC(voltage),
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
#ifdef CONFIG_REBOOT
	,
	BT_GATT_CHARACTERISTIC(
		&REBOOT_UUID.uuid, BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_WRITE, NULL, write_power_reboot, &bps.reboot),
#endif
};


static struct bt_gatt_service power_svc = BT_GATT_SERVICE(power_attrs);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void power_svc_assign_connection_handler_getter(
	struct bt_conn *(*function)(void))
{
	get_connection_handle_fptr = function;
}

static void power_svc_notify(bool notify, u16_t index, u16_t length)
{
	if (get_connection_handle_fptr == NULL) {
		return;
	}

	struct bt_conn *connection_handle = get_connection_handle_fptr();
	if (connection_handle != NULL) {
		if (notify) {
			bt_gatt_notify(connection_handle,
				       &power_svc.attrs[index],
				       power_svc.attrs[index].user_data,
				       length);
		}
	}
}

void power_svc_set_battery(u16_t voltage, u8_t capacity)
{
	bps.voltage = voltage;
	bps.batt_cap = capacity;

	power_svc_notify(ccc.voltage.notify, bps.voltage_index,
			 sizeof(bps.voltage));
	power_svc_notify(ccc.battery_cap.notify, bps.batt_cap_index,
			 sizeof(bps.batt_cap));
}

void power_svc_set_chg_state(u8_t chgState)
{
	bps.batt_chg_state = chgState;
	power_svc_notify(ccc.battery_chg_state.notify, bps.batt_chg_state_index,
			 sizeof(bps.batt_chg_state));
}

void power_svc_set_alarm_state(u8_t alarmState)
{
	bps.batt_alarm = alarmState;
	power_svc_notify(ccc.battery_alarm.notify, bps.batt_alarm_index,
			 sizeof(bps.batt_alarm));
}

void power_svc_init()
{
	init_battery_data();
	bt_gatt_service_register(&power_svc);

	size_t gatt_size = (sizeof(power_attrs) / sizeof(power_attrs[0]));
	bps.voltage_index =
		lbt_find_gatt_index(&BATTERY_VOLTAGE_UUID.uuid, power_attrs, gatt_size);
	bps.batt_cap_index = lbt_find_gatt_index(&BATTERY_CAP_UUID.uuid, power_attrs,
						gatt_size);
	bps.batt_chg_state_index = lbt_find_gatt_index(&BATTERY_CHG_STATE_UUID.uuid, power_attrs,
						gatt_size);
	bps.batt_alarm_index = lbt_find_gatt_index(&BATTERY_ALARM_UUID.uuid, power_attrs,
						gatt_size);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void voltage_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.voltage.notify = IS_NOTIFIABLE(value);
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

static void init_battery_data()
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

#ifdef CONFIG_REBOOT
static ssize_t write_power_reboot(struct bt_conn *conn,
				  const struct bt_gatt_attr *attr,
				  const void *buf, u16_t len, u16_t offset,
				  u8_t flags)
{
	ssize_t length = lbt_write_u8(conn, attr, buf, len, offset, flags);
	if (length > 0) {
		power_reboot_module(bps.reboot);
	}
	return length;
}
#endif
