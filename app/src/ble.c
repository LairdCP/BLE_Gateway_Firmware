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

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define IMEI_DIGITS CONFIG_BLE_NUMBER_OF_IMEI_DIGITS_TO_USE_IN_DEV_NAME

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int ble_initialize(const struct device *device);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
SYS_INIT(ble_initialize, APPLICATION, CONFIG_BLE_INIT_PRIORITY);

void ble_update_name(const char *imei)
{
	int err;
	static char bleDevName[sizeof(CONFIG_BT_DEVICE_NAME "-") + IMEI_DIGITS];
	int devNameEnd;
	int imeiEnd;

	/* Rebuild name */
	strncpy(bleDevName, CONFIG_BT_DEVICE_NAME "-", sizeof(bleDevName) - 1);
	devNameEnd = strlen(bleDevName);
	imeiEnd = strlen(imei);
	strncat(bleDevName + devNameEnd, imei + imeiEnd - IMEI_DIGITS,
		IMEI_DIGITS);
	err = bt_set_name((const char *)bleDevName);
	if (err) {
		LOG_ERR("Failed to set device name (%d)", err);
	} else {
		LOG_INF("BLE device name set to [%s]", log_strdup(bleDevName));
	}
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

	return r;
}
