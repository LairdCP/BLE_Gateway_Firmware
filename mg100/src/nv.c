/**
 * @file nv.c
 * @brief Non-volatile storage for the app
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(mg100_nv);

#define NV_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define NV_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define NV_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define NV_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <device.h>
#include <flash.h>
#include <nvs/nvs.h>

#include "battery.h"
#include "nv.h"

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
	SETTING_ID_BATTERY_LOW,
	SETTING_ID_BATTERY_ALARM,
	SETTING_ID_BATTERY_4,
	SETTING_ID_BATTERY_3,
	SETTING_ID_BATTERY_2,
	SETTING_ID_BATTERY_1,
	SETTING_ID_BATTERY_0,
	SETTING_ID_LWM2M_CONFIG
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

	/* define the nvs file system by settings with:
	 *	sector_size equal to the pagesize,
	 *	starting at NV_FLASH_OFFSET
	 */
	fs.offset = NV_FLASH_OFFSET;
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

exit:
	return rc;
}

int nvStoreDevCert(u8_t *cert, u16_t size)
{
	return nvs_write(&fs, SETTING_ID_DEV_CERT, cert, size);
}

int nvStoreDevKey(u8_t *key, u16_t size)
{
	return nvs_write(&fs, SETTING_ID_DEV_KEY, key, size);
}

int nvReadDevCert(u8_t *cert, u16_t size)
{
	return nvs_read(&fs, SETTING_ID_DEV_CERT, cert, size);
}

int nvReadDevKey(u8_t *key, u16_t size)
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

int nvStoreAwsEndpoint(u8_t *ep, u16_t size)
{
	return nvs_write(&fs, SETTING_ID_AWS_ENDPOINT, ep, size);
}

int nvReadAwsEndpoint(u8_t *ep, u16_t size)
{
	return nvs_read(&fs, SETTING_ID_AWS_ENDPOINT, ep, size);
}

int nvStoreAwsClientId(u8_t *id, u16_t size)
{
	return nvs_write(&fs, SETTING_ID_AWS_CLIENT_ID, id, size);
}

int nvReadAwsClientId(u8_t *id, u16_t size)
{
	return nvs_read(&fs, SETTING_ID_AWS_CLIENT_ID, id, size);
}

int nvStoreAwsRootCa(u8_t *cert, u16_t size)
{
	return nvs_write(&fs, SETTING_ID_AWS_ROOT_CA, cert, size);
}

int nvReadAwsRootCa(u8_t *cert, u16_t size)
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

int nvReadBatteryData(enum SETTING_ID id, u16_t * batteryData)
{
	int rc = -1;
	if ((id <= SETTING_ID_BATTERY_LOW) && (id >= SETTING_ID_BATTERY_0))
	{
		rc = nvs_read(&fs, id, batteryData,
		      sizeof(u16_t));
	}

	return rc;
}

int nvReadBatteryLow(u16_t * batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_LOW, batteryData, sizeof(u16_t));
}

int nvReadBatteryAlarm(u16_t * batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_ALARM, batteryData, sizeof(u16_t));
}

int nvReadBattery4(u16_t * batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_4, batteryData, sizeof(u16_t));
}

int nvReadBattery3(u16_t * batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_3, batteryData, sizeof(u16_t));
}

int nvReadBattery2(u16_t * batteryData)
{
	return nvs_read(&fs,SETTING_ID_BATTERY_2, batteryData, sizeof(u16_t));
}

int nvReadBattery1(u16_t * batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_1, batteryData, sizeof(u16_t));
}

int nvReadBattery0(u16_t * batteryData)
{
	return nvs_read(&fs, SETTING_ID_BATTERY_0, batteryData, sizeof(u16_t));
}

int nvStoreBatteryLow(u16_t * batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_LOW, batteryData, sizeof(u16_t));
}

int nvStoreBatteryAlarm(u16_t * batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_ALARM, batteryData, sizeof(u16_t));
}

int nvStoreBattery4(u16_t * batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_4, batteryData, sizeof(u16_t));
}

int nvStoreBattery3(u16_t * batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_3, batteryData, sizeof(u16_t));
}

int nvStoreBattery2(u16_t * batteryData)
{
	return nvs_write(&fs,SETTING_ID_BATTERY_2, batteryData, sizeof(u16_t));
}

int nvStoreBattery1(u16_t * batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_1, batteryData, sizeof(u16_t));
}

int nvStoreBattery0(u16_t * batteryData)
{
	return nvs_write(&fs, SETTING_ID_BATTERY_0, batteryData, sizeof(u16_t));
}

int nvInitLwm2mConfig(void *data, void *init_value, u16_t size)
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

int nvWriteLwm2mConfig(void *data, u16_t size)
{
	return nvs_write(&fs, SETTING_ID_LWM2M_CONFIG, data, size);
}
