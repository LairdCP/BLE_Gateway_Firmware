/**
 * @file nv.c
 * @brief Non-volatile storage for the app
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(nv, LOG_LEVEL_DBG);

#define NV_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define NV_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define NV_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define NV_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <device.h>
#include <drivers/flash.h>
#include <storage/flash_map.h>
#include <fs/nvs.h>

#include "nv.h"
#ifdef CONFIG_BOARD_MG100
#include "lairdconnect_battery.h"
#include "sdcard_log.h"
#include "ble_motion_service.h"
#endif

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
enum SETTING_ID {
	SETTING_ID_COMMISSIONED,
	SETTING_ID_DEV_CERT,
	SETTING_ID_DEV_KEY,
	SETTING_ID_AWS_ENDPOINT,
	SETTING_ID_AWS_CLIENT_ID,
	SETTING_ID_AWS_ROOT_CA,
	SETTING_ID_LWM2M_CONFIG,
#ifdef CONFIG_BOARD_MG100
	SETTING_ID_BATTERY_LOW,
	SETTING_ID_BATTERY_ALARM,
	SETTING_ID_BATTERY_4,
	SETTING_ID_BATTERY_3,
	SETTING_ID_BATTERY_2,
	SETTING_ID_BATTERY_1,
	SETTING_ID_BATTERY_0,
	SETTING_ID_ACCEL_ODR,
	SETTING_ID_ACCEL_THRESH,
	SETTING_ID_ACCEL_SCALE,
	SETTING_ID_SDLOG_MAX_SIZE,
#endif
#ifdef CONFIG_APP_AWS_CUSTOMIZATION
	SETTING_ID_AWS_ENABLE_CUSTOM
#endif
};

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct nvs_fs fs;
static bool nvCommissioned;

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int nvReadCommissioned(bool *commissioned)
{
	int rc;
	*commissioned = nvCommissioned;
	rc = nvs_read(&fs, SETTING_ID_COMMISSIONED, &nvCommissioned,
		      sizeof(nvCommissioned));
	if (rc <= 0) {
		*commissioned = false;
	}

	return rc;
}

int nvStoreCommissioned(bool commissioned)
{
	int rc;
	nvCommissioned = commissioned;
	rc = nvs_write(&fs, SETTING_ID_COMMISSIONED, &nvCommissioned,
		       sizeof(nvCommissioned));
	if (rc < 0) {
		NV_LOG_ERR("Error writing commissioned (%d)", rc);
	}

	return rc;
}

int nvInit(void)
{
	int rc = 0;
	struct flash_pages_info info;
#ifdef CONFIG_BOARD_MG100
	uint16_t batteryData = 0;
	int Value = 0;
#endif
#ifdef CONFIG_APP_AWS_CUSTOMIZATION
	bool bValue;
#endif

	/* define the nvs file system by settings with:
	 *	sector_size equal to the pagesize,
	 *	starting at FLASH_AREA_OFFSET(storage)
	 */
	fs.offset = FLASH_AREA_OFFSET(storage);
	rc = flash_get_page_info_by_offs(device_get_binding(NV_FLASH_DEVICE),
					 fs.offset, &info);
	if (rc) {
		NV_LOG_ERR("Unable to get page info (%d)", rc);
		goto exit;
	}
	fs.sector_size = info.size;
	fs.sector_count = NUM_FLASH_SECTORS;

	rc = nvs_init(&fs, NV_FLASH_DEVICE);
	if (rc) {
		NV_LOG_ERR("Flash Init failed (%d)", rc);
		goto exit;
	}

	NV_LOG_INF("Free space in NV: %d", nvs_calc_free_space(&fs));

	rc = nvReadCommissioned(&nvCommissioned);
	if (rc <= 0) {
		/* setting not found, init it */
		rc = nvStoreCommissioned(false);
		if (rc <= 0) {
			NV_LOG_ERR("Could not write commissioned flag (%d)",
				   rc);
			goto exit;
		}
	}
#ifdef CONFIG_BOARD_MG100
	batteryData = BatteryGetThresholds(BATTERY_IDX_4);
	rc = nvReadBattery4(&batteryData);
	if ((rc <= 0) || (batteryData == 0)) {
		rc = nvStoreBattery4(&batteryData);
		if (rc <= 0) {
			NV_LOG_ERR(
				"Could not write battery threshold 4 data (%d)",
				rc);
			goto exit;
		}
	}

	batteryData = BatteryGetThresholds(BATTERY_IDX_3);
	rc = nvReadBattery3(&batteryData);
	if ((rc <= 0) || (batteryData == 0)) {
		rc = nvStoreBattery3(&batteryData);
		if (rc <= 0) {
			NV_LOG_ERR(
				"Could not write battery threshold 3 data (%d)",
				rc);
			goto exit;
		}
	}

	batteryData = BatteryGetThresholds(BATTERY_IDX_2);
	rc = nvReadBattery2(&batteryData);
	if ((rc <= 0) || (batteryData == 0)) {
		rc = nvStoreBattery2(&batteryData);
		if (rc <= 0) {
			NV_LOG_ERR(
				"Could not write battery threshold 2 data (%d)",
				rc);
			goto exit;
		}
	}

	batteryData = BatteryGetThresholds(BATTERY_IDX_1);
	rc = nvReadBattery1(&batteryData);
	if ((rc <= 0) || (batteryData == 0)) {
		rc = nvStoreBattery1(&batteryData);
		if (rc <= 0) {
			NV_LOG_ERR(
				"Could not write battery threshold 1 data (%d)",
				rc);
			goto exit;
		}
	}

	batteryData = BatteryGetThresholds(BATTERY_IDX_0);
	rc = nvReadBattery0(&batteryData);
	if ((rc <= 0) || (batteryData == 0)) {
		rc = nvStoreBattery0(&batteryData);
		if (rc <= 0) {
			NV_LOG_ERR(
				"Could not write battery threshold 0 data (%d)",
				rc);
			goto exit;
		}
	}

	batteryData = BatteryGetThresholds(BATTERY_IDX_LOW);
	rc = nvReadBatteryLow(&batteryData);
	if ((rc <= 0) || (batteryData == 0)) {
		rc = nvStoreBatteryLow(&batteryData);
		if (rc <= 0) {
			NV_LOG_ERR(
				"Could not write battery low threshold data (%d)",
				rc);
			goto exit;
		}
	}

	batteryData = BatteryGetThresholds(BATTERY_IDX_ALARM);
	rc = nvReadBatteryLow(&batteryData);
	if ((rc <= 0) || (batteryData == 0)) {
		rc = nvStoreBatteryAlarm(&batteryData);
		if (rc <= 0) {
			NV_LOG_ERR(
				"Could not write battery low alarm data (%d)",
				rc);
			goto exit;
		}
	}

	rc = nvReadAccelODR(&Value);
	if ((rc <= 0) || (Value == 0)) {
		rc = nvStoreAccelODR(MOTION_DEFAULT_ODR);
		if (rc <= 0) {
			NV_LOG_ERR("Could not write Accel ODR (%d)", rc);
			goto exit;
		}
	}

	rc = nvReadAccelThresh(&Value);
	if ((rc <= 0) || (Value == 0)) {
		rc = nvStoreAccelThresh(MOTION_DEFAULT_THS);
		if (rc <= 0) {
			NV_LOG_ERR("Could not write Accel Threshold (%d)", rc);
			goto exit;
		}
	}

	rc = nvReadAccelScale(&Value);
	if ((rc <= 0) || (Value == 0)) {
		rc = nvStoreAccelScale(MOTION_DEFAULT_SCALE);
		if (rc <= 0) {
			NV_LOG_ERR("Could not write Accel Scale (%d)", rc);
			goto exit;
		}
	}

	rc = nvReadSDLogMaxSize(&Value);
	if ((rc <= 0) || (Value == 0)) {
		rc = nvStoreSDLogMaxSize(SDCARD_LOG_DEFAULT_MAX_LENGTH);
		if (rc <= 0) {
			NV_LOG_ERR("Could not write SD log max size (%d)", rc);
			goto exit;
		}
	}
#endif /* CONFIG_BOARD_MG100 */
#ifdef CONFIG_APP_AWS_CUSTOMIZATION
	rc = nvReadAwsEnableCustom(&bValue);
	if (rc <= 0) {
		/* setting not found, init it */
		rc = nvStoreAwsEnableCustom(false);
		if (rc <= 0) {
			NV_LOG_ERR("Could not write AWS enable custom (%d)",
				   rc);
			goto exit;
		}
	}
#endif /* CONFIG_APP_AWS_CUSTOMIZATION */

exit:
	return rc;
}

int nvStoreDevCert(uint8_t *cert, uint16_t size)
{
	return nvs_write(&fs, SETTING_ID_DEV_CERT, cert, size);
}

int nvStoreDevKey(uint8_t *key, uint16_t size)
{
	return nvs_write(&fs, SETTING_ID_DEV_KEY, key, size);
}

int nvReadDevCert(uint8_t *cert, uint16_t size)
{
	return nvs_read(&fs, SETTING_ID_DEV_CERT, cert, size);
}

int nvReadDevKey(uint8_t *key, uint16_t size)
{
	return nvs_read(&fs, SETTING_ID_DEV_KEY, key, size);
}

int nvDeleteDevCert(void)
{
	return nvs_delete(&fs, SETTING_ID_DEV_CERT);
}

int nvDeleteDevKey(void)
{
	return nvs_delete(&fs, SETTING_ID_DEV_KEY);
}

int nvStoreAwsEndpoint(uint8_t *ep, uint16_t size)
{
	return nvs_write(&fs, SETTING_ID_AWS_ENDPOINT, ep, size);
}

int nvReadAwsEndpoint(uint8_t *ep, uint16_t size)
{
	return nvs_read(&fs, SETTING_ID_AWS_ENDPOINT, ep, size);
}

int nvStoreAwsClientId(uint8_t *id, uint16_t size)
{
	return nvs_write(&fs, SETTING_ID_AWS_CLIENT_ID, id, size);
}

int nvReadAwsClientId(uint8_t *id, uint16_t size)
{
	return nvs_read(&fs, SETTING_ID_AWS_CLIENT_ID, id, size);
}

int nvStoreAwsRootCa(uint8_t *cert, uint16_t size)
{
	return nvs_write(&fs, SETTING_ID_AWS_ROOT_CA, cert, size);
}

int nvReadAwsRootCa(uint8_t *cert, uint16_t size)
{
	return nvs_read(&fs, SETTING_ID_AWS_ROOT_CA, cert, size);
}

int nvDeleteAwsEndpoint(void)
{
	return nvs_delete(&fs, SETTING_ID_AWS_ENDPOINT);
}

int nvDeleteAwsClientId(void)
{
	return nvs_delete(&fs, SETTING_ID_AWS_CLIENT_ID);
}

int nvDeleteAwsRootCa(void)
{
	return nvs_delete(&fs, SETTING_ID_AWS_ROOT_CA);
}

int nvInitLwm2mConfig(void *data, void *init_value, uint16_t size)
{
	int rc = nvs_read(&fs, SETTING_ID_LWM2M_CONFIG, data, size);
	if (rc != size) {
		memcpy(data, init_value, size);
		rc = nvs_write(&fs, SETTING_ID_LWM2M_CONFIG, data, size);
		if (rc != size) {
			NV_LOG_ERR("Error writing LWM2M config (%d)", rc);
		}
	}
	return rc;
}

int nvWriteLwm2mConfig(void *data, uint16_t size)
{
	return nvs_write(&fs, SETTING_ID_LWM2M_CONFIG, data, size);
}

#ifdef CONFIG_BOARD_MG100
int nvReadBatteryData(enum SETTING_ID id, uint16_t *batteryData)
{
	int rc = -1;
	if ((id <= SETTING_ID_BATTERY_LOW) && (id >= SETTING_ID_BATTERY_0)) {
		rc = nvs_read(&fs, id, batteryData, sizeof(uint16_t));
	}

	return rc;
}

int nvReadBatteryLow(uint16_t *batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_LOW, batteryData,
			sizeof(uint16_t));
}

int nvReadBatteryAlarm(uint16_t *batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_ALARM, batteryData,
			sizeof(uint16_t));
}

int nvReadBattery4(uint16_t *batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_4, batteryData, sizeof(uint16_t));
}

int nvReadBattery3(uint16_t *batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_3, batteryData, sizeof(uint16_t));
}

int nvReadBattery2(uint16_t *batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_2, batteryData, sizeof(uint16_t));
}

int nvReadBattery1(uint16_t *batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_1, batteryData, sizeof(uint16_t));
}

int nvReadBattery0(uint16_t *batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_0, batteryData, sizeof(uint16_t));
}

int nvStoreBatteryLow(uint16_t *batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_LOW, batteryData,
			 sizeof(uint16_t));
}

int nvStoreBatteryAlarm(uint16_t *batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_ALARM, batteryData,
			 sizeof(uint16_t));
}

int nvStoreBattery4(uint16_t *batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_4, batteryData, sizeof(uint16_t));
}

int nvStoreBattery3(uint16_t *batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_3, batteryData, sizeof(uint16_t));
}

int nvStoreBattery2(uint16_t *batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_2, batteryData, sizeof(uint16_t));
}

int nvStoreBattery1(uint16_t *batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_1, batteryData, sizeof(uint16_t));
}

int nvStoreBattery0(uint16_t *batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_0, batteryData, sizeof(uint16_t));
}

int nvStoreAccelODR(int Value)
{
	return nvs_write(&fs, SETTING_ID_ACCEL_ODR, &Value, sizeof(int));
}

int nvStoreAccelThresh(int Value)
{
	return nvs_write(&fs, SETTING_ID_ACCEL_THRESH, &Value, sizeof(int));
}

int nvStoreAccelScale(int Value)
{
	return nvs_write(&fs, SETTING_ID_ACCEL_SCALE, &Value, sizeof(int));
}

int nvStoreSDLogMaxSize(int Value)
{
	return nvs_write(&fs, SETTING_ID_SDLOG_MAX_SIZE, &Value, sizeof(int));
}

int nvReadAccelODR(int *Value)
{
	return nvs_read(&fs, SETTING_ID_ACCEL_ODR, Value, sizeof(int));
}

int nvReadAccelThresh(int *Value)
{
	return nvs_read(&fs, SETTING_ID_ACCEL_THRESH, Value, sizeof(int));
}

int nvReadAccelScale(int *Value)
{
	return nvs_read(&fs, SETTING_ID_ACCEL_SCALE, Value, sizeof(int));
}

int nvReadSDLogMaxSize(int *Value)
{
	return nvs_read(&fs, SETTING_ID_SDLOG_MAX_SIZE, Value, sizeof(int));
}
#endif /* CONFIG_BOARD_MG100 */
#ifdef CONFIG_APP_AWS_CUSTOMIZATION
int nvStoreAwsEnableCustom(bool Value)
{
	return nvs_write(&fs, SETTING_ID_AWS_ENABLE_CUSTOM, &Value,
			 sizeof(bool));
}

int nvReadAwsEnableCustom(bool *Value)
{
	return nvs_read(&fs, SETTING_ID_AWS_ENABLE_CUSTOM, Value, sizeof(bool));
}
#endif /* CONFIG_APP_AWS_CUSTOMIZATION */
