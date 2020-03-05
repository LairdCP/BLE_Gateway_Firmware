/* ble_bootloader_service.c - BLE Bootloader Service
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(ble_bootloader_service);

#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>

#include "oob_common.h"
#include "laird_bluetooth.h"
#include "ble_bootloader_service.h"
#include "Bootloader_External_Settings.h"
#include "hexcode.h"

#define BBS_BASE_UUID_128(_x_)                                                 \
	BT_UUID_INIT_128(0xa0, 0xe3, 0x4f, 0x84, 0xb8, 0x2c, 0x04, 0xd3, 0xe0, \
			 0xf5, 0x7a, 0x7a, LSB_16(_x_), MSB_16(_x_), 0x2b,     \
			 0xe5)

static struct bt_uuid_128 BBS_UUID = BBS_BASE_UUID_128(0x0000);
static struct bt_uuid_128 BOOTLOADER_PRESENT_UUID = BBS_BASE_UUID_128(0x0001);
static struct bt_uuid_128 BOOTLOADER_HEADER_CHECKED_UUID = BBS_BASE_UUID_128(0x0002);
static struct bt_uuid_128 ERROR_CODE_UUID = BBS_BASE_UUID_128(0x0003);
static struct bt_uuid_128 BOOTLOADER_VERSION_UUID = BBS_BASE_UUID_128(0x0004);
static struct bt_uuid_128 EXT_HEADER_VERSION_UUID = BBS_BASE_UUID_128(0x0005);
static struct bt_uuid_128 EXT_FUNCTION_VERSION_UUID = BBS_BASE_UUID_128(0x0006);
static struct bt_uuid_128 CUSTOMER_KEY_SET_UUID = BBS_BASE_UUID_128(0x0007);
static struct bt_uuid_128 CUSTOMER_KEY_UUID = BBS_BASE_UUID_128(0x0008);
static struct bt_uuid_128 READBACK_PROTECTION_UUID = BBS_BASE_UUID_128(0x0009);
static struct bt_uuid_128 CPU_DEBUG_PROTECTION_UUID = BBS_BASE_UUID_128(0x000a);
static struct bt_uuid_128 QSPI_CHECKED_UUID = BBS_BASE_UUID_128(0x000b);
static struct bt_uuid_128 QSPI_CRC_UUID = BBS_BASE_UUID_128(0x000c);
static struct bt_uuid_128 QSPI_SHA256_UUID = BBS_BASE_UUID_128(0x000d);
static struct bt_uuid_128 BOOTLOADER_TYPE_UUID = BBS_BASE_UUID_128(0x000e);
static struct bt_uuid_128 BOOTLOADER_UPDATE_FAILURES_UUID = BBS_BASE_UUID_128(0x000f);
static struct bt_uuid_128 BOOTLOADER_UPDATE_LAST_FAIL_VERSION_UUID = BBS_BASE_UUID_128(0x0010);
static struct bt_uuid_128 BOOTLOADER_UPDATE_LAST_FAIL_CODE_UUID = BBS_BASE_UUID_128(0x0011);
static struct bt_uuid_128 BOOTLOADER_UPDATES_APPLIED_UUID = BBS_BASE_UUID_128(0x0012);
static struct bt_uuid_128 BOOTLOADER_SECTION_UPDATES_APPLIED_UUID = BBS_BASE_UUID_128(0x0013);
static struct bt_uuid_128 BOOTLOADER_MODEM_UPDATES_APPLIED_UUID = BBS_BASE_UUID_128(0x0014);
static struct bt_uuid_128 BOOTLOADER_MODEM_UPDATE_LAST_FAIL_VERSION_UUID = BBS_BASE_UUID_128(0x0015);
static struct bt_uuid_128 BOOTLOADER_MODEM_UPDATE_LAST_FAIL_CODE_UUID = BBS_BASE_UUID_128(0x0016);
static struct bt_uuid_128 BOOTLOADER_COMPRESSION_ERRORS_UUID = BBS_BASE_UUID_128(0x0017);
static struct bt_uuid_128 BOOTLOADER_COMPRESSION_LAST_FAIL_CODE_UUID = BBS_BASE_UUID_128(0x0018);
static struct bt_uuid_128 MODULE_BUILD_DATE_UUID = BBS_BASE_UUID_128(0x0019);
static struct bt_uuid_128 FIRMWARE_BUILD_DATE_UUID = BBS_BASE_UUID_128(0x001a);
static struct bt_uuid_128 BOOT_VERIFICATION_UUID = BBS_BASE_UUID_128(0x001b);

struct ble_bootloader_service {
	bool bootloader_present;
	bool bootloader_header_checked;
	u8_t error_code;
	u16_t bootloader_version;
	u16_t ext_header_version;
	u16_t ext_function_version;
	bool customer_key_set;
	char customer_key[SIGNATURE_SIZE*2+1];
	bool readback_protection;
	bool cpu_debug_protection;
	u8_t QSPI_checked;
	u32_t QSPI_crc;
	char QSPI_sha256[SHA256_SIZE*2+1];
	bool bootloader_type;
	u8_t bootloader_update_failures;
	u16_t bootloader_update_last_fail_version;
	u8_t bootloader_update_last_fail_code;
	u16_t bootloader_updates_applied;
	u16_t bootloader_section_updates_applied;
	u16_t bootloader_modem_updates_applied;
	u16_t bootloader_modem_update_last_fail_version;
	u8_t bootloader_modem_update_last_fail_code;
	u8_t bootloader_compression_errors;
	u16_t bootloader_compression_last_fail_code;
	char module_build_date[sizeof(__DATE__)];
	char firmware_build_date[sizeof(__DATE__)];
	u8_t boot_verification;
};

static struct ble_bootloader_service bbs;

/* Bootloader Service Declaration */
static struct bt_gatt_attr bootloader_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&BBS_UUID),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_PRESENT_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.bootloader_present),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_HEADER_CHECKED_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.bootloader_header_checked),

	BT_GATT_CHARACTERISTIC(&ERROR_CODE_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.error_code),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_VERSION_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u16, NULL,
	&bbs.bootloader_version),


	BT_GATT_CHARACTERISTIC(&EXT_HEADER_VERSION_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u16, NULL,
	&bbs.ext_header_version),

	BT_GATT_CHARACTERISTIC(&EXT_FUNCTION_VERSION_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u16, NULL,
	&bbs.ext_function_version),

	BT_GATT_CHARACTERISTIC(&CUSTOMER_KEY_SET_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.customer_key_set),

	BT_GATT_CHARACTERISTIC(&CUSTOMER_KEY_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_string_no_max_size,
	NULL, &bbs.customer_key),

	BT_GATT_CHARACTERISTIC(&READBACK_PROTECTION_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.readback_protection),

	BT_GATT_CHARACTERISTIC(&CPU_DEBUG_PROTECTION_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.cpu_debug_protection),

	BT_GATT_CHARACTERISTIC(&QSPI_CHECKED_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.QSPI_checked),

	BT_GATT_CHARACTERISTIC(&QSPI_CRC_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u32, NULL,
	&bbs.QSPI_crc),

	BT_GATT_CHARACTERISTIC(&QSPI_SHA256_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_string_no_max_size,
	NULL, &bbs.QSPI_sha256),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_TYPE_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.bootloader_type),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_UPDATE_FAILURES_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.bootloader_update_failures),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_UPDATE_LAST_FAIL_VERSION_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u16, NULL,
	&bbs.bootloader_update_last_fail_version),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_UPDATE_LAST_FAIL_CODE_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.bootloader_update_last_fail_code),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_UPDATES_APPLIED_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u16, NULL,
	&bbs.bootloader_updates_applied),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_SECTION_UPDATES_APPLIED_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u16, NULL,
	&bbs.bootloader_section_updates_applied),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_MODEM_UPDATES_APPLIED_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u16, NULL,
	&bbs.bootloader_modem_updates_applied),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_MODEM_UPDATE_LAST_FAIL_VERSION_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u16, NULL,
	&bbs.bootloader_modem_update_last_fail_version),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_MODEM_UPDATE_LAST_FAIL_CODE_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.bootloader_modem_update_last_fail_code),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_COMPRESSION_ERRORS_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.bootloader_compression_errors),

	BT_GATT_CHARACTERISTIC(&BOOTLOADER_COMPRESSION_LAST_FAIL_CODE_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u16, NULL,
	&bbs.bootloader_compression_last_fail_code),

	BT_GATT_CHARACTERISTIC(&MODULE_BUILD_DATE_UUID .uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_string_no_max_size,
	NULL, &bbs.module_build_date),

	BT_GATT_CHARACTERISTIC(&FIRMWARE_BUILD_DATE_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_string_no_max_size,
	NULL, &bbs.firmware_build_date),

	BT_GATT_CHARACTERISTIC(&BOOT_VERIFICATION_UUID.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, lbt_read_u8, NULL,
	&bbs.boot_verification)
};

static struct bt_gatt_service bootloader_service = BT_GATT_SERVICE(bootloader_attrs);

void bbs_set_bootloader_present(bool present)
{
	bbs.bootloader_present = present;
}

void bbs_set_bootloader_header_checked(bool checked)
{
	bbs.bootloader_header_checked = checked;
}

void bbs_set_error_code(u8_t error)
{
	bbs.error_code = error;
}

void bbs_set_bootloader_version(u16_t version)
{
	bbs.bootloader_version = version;
}

void bbs_set_ext_header_version(u16_t version)
{
	bbs.ext_header_version = version;
}

void bbs_set_ext_function_version(u16_t version)
{
	bbs.ext_function_version = version;
}

void bbs_set_customer_key_set(bool set)
{
	bbs.customer_key_set = set;
}

void bbs_set_customer_key(u8_t *key)
{
	if (key == NULL) {
		memset(bbs.customer_key, 0, sizeof(bbs.customer_key));
	}
	else {
		HexEncode(key, SIGNATURE_SIZE*2, bbs.customer_key, false, true);
	}
}

void bbs_set_readback_protection(bool readback)
{
	bbs.readback_protection = readback;
}

void bbs_set_cpu_debug_protection(bool debug)
{
	bbs.cpu_debug_protection = debug;
}

void bbs_set_QSPI_checked(u8_t checked)
{
	bbs.QSPI_checked = checked;
}

void bbs_set_QSPI_crc(u32_t checked)
{
	bbs.QSPI_crc = checked;
}

void bbs_set_QSPI_sha256(u8_t *sha256)
{
	if (sha256 == NULL) {
		memset(bbs.QSPI_sha256, 0, sizeof(bbs.QSPI_sha256));
	}
	else {
		HexEncode(sha256, SHA256_SIZE*2, bbs.QSPI_sha256, false, true);
	}
}

void bbs_set_bootloader_type(bool type)
{
	bbs.bootloader_type = type;
}

void bbs_set_bootloader_update_failures(u8_t failures)
{
	bbs.bootloader_update_failures = failures;
}

void bbs_set_bootloader_update_last_fail_version(u16_t version)
{
	bbs.bootloader_update_last_fail_version = version;
}

void bbs_set_bootloader_update_last_fail_code(u8_t code)
{
	bbs.bootloader_update_last_fail_code = code;
}

void bbs_set_bootloader_updates_applied(u16_t updates)
{
	bbs.bootloader_updates_applied = updates;
}

void bbs_set_bootloader_section_updates_applied(u16_t updates)
{
	bbs.bootloader_section_updates_applied = updates;
}

void bbs_set_bootloader_modem_updates_applied(u16_t updates)
{
	bbs.bootloader_modem_updates_applied = updates;
}

void bbs_set_bootloader_modem_update_last_fail_version(u16_t version)
{
	bbs.bootloader_modem_update_last_fail_version = version;
}

void bbs_set_bootloader_modem_update_last_fail_code(u8_t code)
{
	bbs.bootloader_modem_update_last_fail_code = code;
}

void bbs_set_bootloader_compression_errors(u8_t errors)
{
	bbs.bootloader_compression_errors = errors;
}

void bbs_set_bootloader_compression_last_fail_code(u16_t code)
{
	bbs.bootloader_compression_last_fail_code = code;
}

void bbs_set_module_build_date(uint8_t *date)
{
	if (date == NULL) {
		memset(bbs.module_build_date, 0, sizeof(bbs.module_build_date));
	}
	else {
		memcpy(bbs.module_build_date, date,
		       sizeof(bbs.module_build_date));
	}
}

void bbs_set_firmware_build_date(uint8_t *date)
{
	if (date == NULL) {
		memset(bbs.firmware_build_date, 0,
		       sizeof(bbs.firmware_build_date));
	}
	else {
		memcpy(bbs.firmware_build_date, date,
		       sizeof(bbs.firmware_build_date));
	}
}

void bbs_set_boot_verification(u8_t verification)
{
	bbs.boot_verification = verification;
}

void bbs_init()
{
	bt_gatt_service_register(&bootloader_service);

	bbs_set_customer_key(NULL);
	bbs_set_QSPI_sha256(NULL);
	bbs_set_module_build_date(NULL);
	bbs_set_firmware_build_date(NULL);
}
