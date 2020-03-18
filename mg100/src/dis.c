/* dis.c - Device Information Service
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(dis);

//=============================================================================
// Includes
//=============================================================================
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>
#include <kernel_version.h>
#include <stdio.h>

#include "mg100_common.h"
#include "laird_bluetooth.h"
#include "dis.h"

//=============================================================================
// Local Constant Definitions
//=============================================================================

// '%c' didn't work properly in string conversion so '%u' was used.
// Therefore, version string size is larger than Zephyr max of "255.255.255".
#define ZEPHYR_VERSION_MAX_SIZE ((10 * 3) + 2 + 1)
#define ZEPHYR_VERSION_MAX_STR_LEN (ZEPHYR_VERSION_MAX_SIZE - 1)

static struct bt_uuid_16 DIS_UUID = BT_UUID_INIT_16(0x180a);
static struct bt_uuid_16 MODEL_NUMBER_UUID = BT_UUID_INIT_16(0x2a24);
static struct bt_uuid_16 FIRMWARE_REVISION_UUID = BT_UUID_INIT_16(0x2a26);
static struct bt_uuid_16 SOFTWARE_REVISION_UUID = BT_UUID_INIT_16(0x2a28);
static struct bt_uuid_16 MANUFACTURER_NAME_UUID = BT_UUID_INIT_16(0x2a29);

static const char MODEL_NUMBER[] = "MG100";
static const char SOFTWARE_REVISION[] = APP_VERSION_STRING;
static const char MANUFACTURER_NAME[] = "Laird Connectivity";

//=============================================================================
// Type Definitions
//=============================================================================
// NA

//=============================================================================
// Local Function Prototypes
//=============================================================================
static ssize_t read_const_string(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr, void *buf,
				 u16_t len, u16_t offset);

static ssize_t read_zephyr_version(struct bt_conn *conn,
				   const struct bt_gatt_attr *attr, void *buf,
				   u16_t len, u16_t offset);

//=============================================================================
// Local Data Definitions
//=============================================================================
static char firmware_version[ZEPHYR_VERSION_MAX_SIZE];

static struct bt_gatt_attr dis_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&DIS_UUID),
	BT_GATT_CHARACTERISTIC(&MODEL_NUMBER_UUID.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_const_string, NULL,
			       (char *)MODEL_NUMBER),
	BT_GATT_CHARACTERISTIC(&FIRMWARE_REVISION_UUID.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_zephyr_version, NULL,
			       firmware_version),
	BT_GATT_CHARACTERISTIC(&SOFTWARE_REVISION_UUID.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_const_string, NULL,
			       (char *)SOFTWARE_REVISION),
	BT_GATT_CHARACTERISTIC(&MANUFACTURER_NAME_UUID.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_const_string, NULL,
			       (char *)MANUFACTURER_NAME),

};

static struct bt_gatt_service dis_gatt = BT_GATT_SERVICE(dis_attrs);

//=============================================================================
// Global Function Definitions
//=============================================================================
void dis_initialize(void)
{
	u32_t version = sys_kernel_version_get();
	snprintf(firmware_version, ZEPHYR_VERSION_MAX_STR_LEN, "%u.%u.%u",
		 SYS_KERNEL_VER_MAJOR(version), SYS_KERNEL_VER_MINOR(version),
		 SYS_KERNEL_VER_PATCHLEVEL(version));
	__ASSERT(strlen(firmware_version) > strlen(".."),
		 "Zephyr stringbuilder failure");

	bt_gatt_service_register(&dis_gatt);
}

//=============================================================================
// Local Function Definitions
//=============================================================================

// Constant strings are assumed to be properly terminated.
static ssize_t read_const_string(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr, void *buf,
				 u16_t len, u16_t offset)
{
	const char *value = attr->user_data;
	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 strlen(value));
}

// The Zephyr version is placed in the firmware revision characteristic.
static ssize_t read_zephyr_version(struct bt_conn *conn,
				   const struct bt_gatt_attr *attr, void *buf,
				   u16_t len, u16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       ZEPHYR_VERSION_MAX_STR_LEN);
}

// end
