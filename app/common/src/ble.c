/**
 * @file ble.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <logging/log.h>
LOG_MODULE_REGISTER(ble, CONFIG_BLE_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <init.h>
#include <bluetooth/bluetooth.h>
#include <stdio.h>

#include "attr.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define ID_DIGITS CONFIG_BLE_NUMBER_OF_DIGITS_TO_USE_IN_DEV_NAME

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int ble_initialize(const struct device *device);
static int ble_addr_init(void);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
SYS_INIT(ble_initialize, APPLICATION, CONFIG_BLE_INIT_PRIORITY);

void ble_update_name(const char *id)
{
	int err;
	static char
		bleDevName[sizeof(CONFIG_BT_DEVICE_NAME "-") + ID_DIGITS + 1];
	int idStart;
	int idEnd;

	/* Rebuild name */
	strncpy(bleDevName, CONFIG_BT_DEVICE_NAME "-", sizeof(bleDevName) - 1);
	idEnd = strlen(id);
	idStart = MAX(0, idEnd - ID_DIGITS);
	strncat(bleDevName, id + idStart, ID_DIGITS);
	err = bt_set_name((const char *)bleDevName);
	if (err) {
		LOG_ERR("Failed to set device name (%d)", err);
	} else {
		LOG_DBG("BLE device name set to [%s]", log_strdup(bleDevName));
	}

	attr_set_string(ATTR_ID_name, bleDevName, strlen(bleDevName));
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int ble_initialize(const struct device *device)
{
	ARG_UNUSED(device);
	int r = 0;

	r = bt_enable(NULL);
	LOG_WRN("Bluetooth init %s: %d", (r == 0) ? "success" : "failure", r);

	(void)ble_addr_init();

	return r;
}

static int ble_addr_init(void)
{
	int r = 0;
	size_t count = 1;
	bt_addr_le_t addr;
	char addr_str[BT_ADDR_LE_STR_LEN] = { 0 };
	char bd_addr[BT_ADDR_LE_STR_LEN];
	size_t size = attr_get_size(ATTR_ID_bluetoothAddress);

	bt_id_get(&addr, &count);
	if (count < 1) {
		LOG_DBG("Creating new address");
		bt_addr_le_copy(&addr, BT_ADDR_LE_ANY);
		r = bt_id_create(&addr, NULL);
	}
	bt_addr_le_to_str(&addr, addr_str, sizeof(addr_str));
	LOG_INF("Bluetooth Address: %s count: %d status: %d",
		log_strdup(addr_str), count, r);

	/* remove ':' from default format */
	size_t i;
	size_t j;
	for (i = 0, j = 0; j < size - 1; i++) {
		if (addr_str[i] != ':') {
			bd_addr[j] = addr_str[i];
			j += 1;
		}
	}
	bd_addr[j] = 0;
	attr_set_string(ATTR_ID_bluetoothAddress, bd_addr, size - 1);

	/* Use the Bluetooth address to make the name unique
	 * (when the modem init is delayed by application).
	 */
	ble_update_name(bd_addr);

#ifndef CONFIG_MODEM_HL7800
	/* Change the case to lower for the gateway ID */
	for (i = 0; i < size - 1; i++) {
		if (bd_addr[i] >= 'A' && bd_addr[i] <= 'Z') {
			bd_addr[i] += ('a' - 'A');
		}
	}

LOG_ERR("gateway id is now %s", log_strdup(bd_addr));

	attr_set_string(ATTR_ID_gatewayId, bd_addr, size - 1);
#endif

	return r;
}
