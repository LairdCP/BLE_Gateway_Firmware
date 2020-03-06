/* bootloader.c - Bootloader control
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_INF
LOG_MODULE_REGISTER(mg100_bootloader);

#define BOOTLOADER_LOG_ERR(...) LOG_ERR(__VA_ARGS__)

//=============================================================================
// Includes
//=============================================================================

#include <stdio.h>
#include <zephyr/types.h>
#include <kernel.h>
#include "bootloader.h"
#include "BlrPublic.h"
#include "ble_bootloader_service.h"
#include <time.h>

//=============================================================================
// Local Data Definitions
//=============================================================================

#define MAX_DATA_SIZE 64
#define TIMESTAMP_FIELD_SIZE 4
#define TIMESTAMP_INVALID_MIN 0x0
#define TIMESTAMP_INVALID_MAX 0xffffffff
static bool bootloader_present = false;

//=============================================================================
// Global Function Definitions
//=============================================================================

bool bootloader_init(void)
{
	/* Setup the BLE service and init the Blr module */
	bbs_init();
	bootloader_present = BlrPubSetup();
	bootloader_fetch();
	return bootloader_present;
}

void bootloader_fetch(void)
{
	/* Queries information from bootloader and updated bluetooth service */
	bbs_set_bootloader_present(bootloader_present);

	if (bootloader_present == true) {
		//Query values from bootloader
		u32_t status = 0;
		u8_t value8 = 0;
		u16_t value16 = 0;
		u32_t value32 = 0;
		u8_t date[sizeof(__DATE__)];
		u8_t buffer[MAX_DATA_SIZE];
		time_t timestamp;

#ifdef BLR_SKIP_CHECKSUM_VERIFY
		value8 = 0;
#else
		value8 = 1;
#endif
		bbs_set_bootloader_header_checked(value8);

		value8 = BlrPubGetInfo(&value16, NULL, NULL);

		bbs_set_error_code(value8);

		if (value8 == BOOTLOADER_INIT_STATE_INITIALISED) {
			bbs_set_ext_header_version(value16);
		}

		value8 = BlrPubGetInfo(NULL, &value16, NULL);

		if (value8 == BOOTLOADER_INIT_STATE_INITIALISED) {
			bbs_set_ext_function_version(value16);
		}

		value8 = BlrPubGetInfo(NULL, NULL, date);

		if (value8 == BOOTLOADER_INIT_STATE_INITIALISED) {
			bbs_set_firmware_build_date(date);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_CUSTOMER_PK_SET,
				     0, 0, &value8, sizeof(value8), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_customer_key_set(value8);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_READBACK_PROTECTION,
				     0, 0, &value8, sizeof(value8), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_readback_protection(value8);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_CPU_DEBUG_PROTECTION,
				     0, 0, &value8, sizeof(value8), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_cpu_debug_protection(value8);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_QSPI_CHECKED, 0,
				     0, &value8, sizeof(value8), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_QSPI_checked(value8);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_BOOTLOADER_RELEASE_BUILD,
				     0, 0, &value8, sizeof(value8), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_type(value8);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_BOOTLOADER_UPDATE_FAILURES,
				     0, 0, &value8, sizeof(value8), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_update_failures(value8);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_BOOTLOADER_UPDATE_LAST_FAIL_CODE,
				     0, 0, &value8, sizeof(value8), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_update_last_fail_code(value8);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_MODEM_UPDATE_LAST_FAIL_CODE,
				     0, 0, &value8, sizeof(value8), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_modem_update_last_fail_code(value8);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_COMPRESSION_UPDATE_ERRORS,
				     0, 0, &value8, sizeof(value8), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_compression_errors(value8);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_BOOT_VERIFICATION,
				     0, 0, &value8, sizeof(value8), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_boot_verification(value8);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_SECTION_VERSION,
				     2, 0, (u8_t*)&value16, sizeof(value16), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_version(value16);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_BOOTLOADER_UPDATE_LAST_FAIL_VERSION,
				     0, 0, (u8_t*)&value16, sizeof(value16), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_update_last_fail_version(value16);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_BOOTLOADER_UPDATES_APPLIED,
				     0, 0, (u8_t*)&value16, sizeof(value16), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {

			bbs_set_bootloader_updates_applied(value16);
		}
		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_SECTION_UPDATES_APPLIED,
				     0, 0, (u8_t*)&value16, sizeof(value16), NULL, NULL,
				    NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_section_updates_applied(value16);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_MODEM_UPDATES_APPLIED,
				     0, 0, (u8_t*)&value16, sizeof(value16), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_modem_updates_applied(value16);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_MODEM_UPDATE_LAST_FAIL_VERSION,
				     0, 0, (u8_t*)&value16, sizeof(value16), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_modem_update_last_fail_version(value16);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_COMPRESSION_UPDATE_LAST_FAIL_CODE,
				     0, 0, (u8_t*)&value16, sizeof(value16), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_bootloader_compression_last_fail_code(value16);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_QSPI_HEADER_CRC, 0,
				     0, (u8_t*)&value32, sizeof(value32), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_QSPI_crc(value32);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_CUSTOMER_PK, 0, 0,
				     buffer, sizeof(buffer), NULL, NULL, NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_customer_key(buffer);
		}

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_QSPI_HEADER_SHA256,
				     0, 0, buffer, sizeof(buffer), NULL, NULL,
				     NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS) {
			bbs_set_QSPI_sha256(buffer);
		}

		memset(&timestamp, 0, sizeof(timestamp));

		status = BlrPubQuery(BOOTLOADER_STORAGE_INDEX_BUILD_DATE, 0,
				     0, (u8_t*)&timestamp, TIMESTAMP_FIELD_SIZE, NULL,
				     NULL, NULL);
		if (status == BOOTLOADER_STORAGE_CODE_SUCCESS &&
		    timestamp != TIMESTAMP_INVALID_MIN && timestamp !=
		    TIMESTAMP_INVALID_MAX) {
			struct tm *timesplit = gmtime(&timestamp);
			strftime(buffer, sizeof(__DATE__), "%b %d %Y", timesplit);
			buffer[sizeof(__DATE__)-1] = 0;
			bbs_set_module_build_date(buffer);
		}
	}
	else {
		/* Bootloader not present or check failure */
		u8_t value8 = BlrPubGetInfo(NULL, NULL, NULL);

		bbs_set_error_code(value8);
	}
}
