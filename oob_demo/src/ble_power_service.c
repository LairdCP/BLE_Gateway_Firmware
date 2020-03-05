/* ble_power_service.c - BLE Power Service
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(oob_power_svc);

#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>

#include "oob_common.h"
#include "laird_bluetooth.h"
#include "ble_power_service.h"
#include "power.h"

#define POWER_SVC_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define POWER_SVC_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define POWER_SVC_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define POWER_SVC_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

#define POWER_SVC_BASE_UUID_128(_x_)                                           \
	BT_UUID_INIT_128(0xeb, 0xb7, 0xb2, 0x67, 0xfb, 0x78, 0x4e, 0xf2, 0x9e, \
			 0x55, 0xd7, 0xf3, LSB_16(_x_), MSB_16(_x_), 0x1c, 0xdc)
static struct bt_uuid_128 POWER_SVC_UUID = POWER_SVC_BASE_UUID_128(0x0000);
static struct bt_uuid_128 VOLTAGE_UUID = POWER_SVC_BASE_UUID_128(0x0001);

struct ble_power_voltage {
	u8_t voltage_int;
	u8_t voltage_dec;
};

struct ble_power_service {
	struct ble_power_voltage voltage;

	u16_t voltage_index;
};

struct ccc_table {
	struct lbt_ccc_element voltage;
};

static struct ble_power_service bps;
static struct ccc_table ccc;
static struct bt_conn *(*get_connection_handle_fptr)(void);

static void voltage_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.voltage.notify = IS_NOTIFIABLE(value);
	power_mode_set(ccc.voltage.notify);
}

/* Power Service Declaration */
static struct bt_gatt_attr power_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&POWER_SVC_UUID),
	BT_GATT_CHARACTERISTIC(
		&VOLTAGE_UUID.uuid, BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_NONE, NULL, NULL, &bps.voltage),
	LBT_GATT_CCC(voltage)
};

static struct bt_gatt_service power_svc = BT_GATT_SERVICE(power_attrs);

void power_svc_assign_connection_handler_getter(struct bt_conn *(*function)(void))
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

void power_svc_set_voltage(u8_t integer, u8_t decimal)
{
	bps.voltage.voltage_int = integer;
	bps.voltage.voltage_dec = decimal;
	power_svc_notify(ccc.voltage.notify, bps.voltage_index,
			 sizeof(bps.voltage));
}

void power_svc_init()
{
	bt_gatt_service_register(&power_svc);

	size_t gatt_size = (sizeof(power_attrs)/sizeof(power_attrs[0]));
	bps.voltage_index = lbt_find_gatt_index(&VOLTAGE_UUID.uuid, power_attrs,
						gatt_size);
}