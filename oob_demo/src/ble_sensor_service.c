/* ble_sensor_service.c - BLE Sensor Service
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(ble_sensor_service);

#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>

#include "oob_common.h"
#include "laird_bluetooth.h"
#include "ble_sensor_service.h"

#define BSS_BASE_UUID_128(_x_)                                                 \
	BT_UUID_INIT_128(0x0c, 0xc7, 0x37, 0x39, 0xae, 0xa0, 0x74, 0x90, 0x1a, \
			 0x47, 0xab, 0x5b, LSB_16(_x_), MSB_16(_x_), 0x01,     \
			 0xab)

static struct bt_uuid_128 BSS_UUID = BSS_BASE_UUID_128(0x0000);
static struct bt_uuid_128 SENSOR_STATE_UUID = BSS_BASE_UUID_128(0x0001);
static struct bt_uuid_128 SENSOR_BT_ADDR_UUID = BSS_BASE_UUID_128(0x0002);

struct ble_sensor_service {
	u8_t sensor_state;
	char sensor_bt_addr[BT_ADDR_LE_STR_LEN + 1];

	u16_t sensor_state_index;
	u16_t sensor_bt_addr_index;
};

struct ccc_table {
	struct lbt_ccc_element sensor_state;
	struct lbt_ccc_element sensor_bt_addr;
};

static struct ble_sensor_service bss;
static struct ccc_table ccc;
static struct bt_conn *(*get_connection_handle_fptr)(void);

static ssize_t read_sensor_bt_addr(struct bt_conn *conn,
				   const struct bt_gatt_attr *attr, void *buf,
				   u16_t len, u16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       BT_ADDR_LE_STR_LEN);
}

static void sensor_state_ccc_handler(const struct bt_gatt_attr *attr,
				     u16_t value)
{
	ccc.sensor_state.notify = IS_NOTIFIABLE(value);
}

static void sensor_bt_addr_ccc_handler(const struct bt_gatt_attr *attr,
				       u16_t value)
{
	ccc.sensor_bt_addr.notify = IS_NOTIFIABLE(value);
}

/* Cellular Service Declaration */
static struct bt_gatt_attr sensor_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&BSS_UUID),
	BT_GATT_CHARACTERISTIC(&SENSOR_STATE_UUID.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
			       &bss.sensor_state),
	LBT_GATT_CCC(sensor_state),
	BT_GATT_CHARACTERISTIC(&SENSOR_BT_ADDR_UUID.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_sensor_bt_addr, NULL,
			       bss.sensor_bt_addr),
	LBT_GATT_CCC(sensor_bt_addr)
};

static struct bt_gatt_service sensor_service = BT_GATT_SERVICE(sensor_attrs);

static void bss_notify(bool notify, u16_t index, u16_t length)
{
	if (get_connection_handle_fptr == NULL) {
		return;
	}

	struct bt_conn *connection_handle = get_connection_handle_fptr();
	if (connection_handle != NULL) {
		if (notify) {
			bt_gatt_notify(connection_handle,
				       &sensor_service.attrs[index],
				       sensor_service.attrs[index].user_data,
				       length);
		}
	}
}

void bss_assign_connection_handler_getter(struct bt_conn *(*function)(void))
{
	get_connection_handle_fptr = function;
}

void bss_set_sensor_state(u8_t state)
{
	bss.sensor_state = state;
	bss_notify(ccc.sensor_state.notify, bss.sensor_state_index,
		   sizeof(bss.sensor_state));
}

void bss_set_sensor_bt_addr(char *addr)
{
	memset(bss.sensor_bt_addr, 0, sizeof(bss.sensor_bt_addr));
	if (addr != NULL) {
		strncpy(bss.sensor_bt_addr, addr, BT_ADDR_LE_STR_LEN);
	}
	bss_notify(ccc.sensor_bt_addr.notify, bss.sensor_bt_addr_index,
		   strlen(bss.sensor_bt_addr));
}

void bss_init()
{
	bt_gatt_service_register(&sensor_service);

	size_t gatt_size = (sizeof(sensor_attrs)/sizeof(sensor_attrs[0]));
	bss.sensor_state_index = lbt_find_gatt_index(&SENSOR_STATE_UUID.uuid,
						     sensor_attrs, gatt_size);
	bss.sensor_bt_addr_index = lbt_find_gatt_index(&SENSOR_BT_ADDR_UUID.uuid,
						       sensor_attrs, gatt_size);
}
