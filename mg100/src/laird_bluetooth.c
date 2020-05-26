/**
 * @file laird_bluetooth.c
 * @brief Common Bluetooth operations.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(laird_bluetooth);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/gatt.h>

#include "laird_utility_macros.h"
#include "laird_bluetooth.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define BT_ATT_ERR_SUCCESS 0x00

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
ssize_t lbt_read_u8(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		    void *buf, u16_t len, u16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(u8_t));
}

ssize_t lbt_read_u16(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		     void *buf, u16_t len, u16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(u16_t));
}

ssize_t lbt_read_u32(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		     void *buf, u16_t len, u16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(u32_t));
}

ssize_t lbt_read_integer(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, u16_t len, u16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(int));
}

ssize_t lbt_read_string(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, u16_t len, u16_t offset,
			u16_t max_str_length)
{
	const char *value = attr->user_data;
	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 MIN(strlen(value), max_str_length));
}

ssize_t lbt_read_string_no_max_size(struct bt_conn *conn,
				    const struct bt_gatt_attr *attr, void *buf,
				    u16_t len, u16_t offset)
{
	const char *value = attr->user_data;
	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 strlen(value));
}

ssize_t lbt_write_string(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, u16_t len, u16_t offset, u8_t flags,
			 u16_t max_str_length)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(flags);

	if ((offset + len) > max_str_length) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	char *value = attr->user_data;
	memcpy(value + offset, buf, len);
	/* null terminate the value that was written */
	*(value + offset + len) = 0;

	return len;
}

ssize_t lbt_write_u8(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		     const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(flags);

	if (offset != 0 || len != sizeof(u8_t)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(attr->user_data, buf, len);
	return len;
}

ssize_t lbt_write_u16(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		     const void *buf, u16_t len, u16_t offset, u8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(flags);

	if (offset != 0 || len != sizeof(u16_t)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(attr->user_data, buf, len);
	return len;
}

u16_t lbt_find_gatt_index(struct bt_uuid *uuid, struct bt_gatt_attr *gatt,
			  size_t size)
{
	size_t i = 0;
	while (i < size) {
		if (memcmp(gatt[i].uuid, uuid, sizeof(struct bt_uuid_128)) ==
		    0) {
			return (u16_t)i;
		}
		++i;
	}

	/* Not found */
	__ASSERT(0, "GATT handle for characteristic not found");
	return 0;
}

const char *lbt_get_att_err_string(u8_t code)
{
	/* clang-format off */
	switch (code) {
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, SUCCESS);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, INVALID_HANDLE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, READ_NOT_PERMITTED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, WRITE_NOT_PERMITTED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, INVALID_PDU);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, AUTHENTICATION);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, NOT_SUPPORTED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, INVALID_OFFSET);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, AUTHORIZATION);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, PREPARE_QUEUE_FULL);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, ATTRIBUTE_NOT_FOUND);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, ATTRIBUTE_NOT_LONG);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, ENCRYPTION_KEY_SIZE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, INVALID_ATTRIBUTE_LEN);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, UNLIKELY);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, INSUFFICIENT_ENCRYPTION);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, UNSUPPORTED_GROUP_TYPE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, INSUFFICIENT_RESOURCES);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, DB_OUT_OF_SYNC);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, VALUE_NOT_ALLOWED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, WRITE_REQ_REJECTED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, CCC_IMPROPER_CONF);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, PROCEDURE_IN_PROGRESS);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_ATT_ERR, OUT_OF_RANGE);
	default:
		return "UNKNOWN";
	}
	/* clang-format on */
}

const char *lbt_get_hci_err_string(u8_t code)
{
	/* clang-format off */
	switch (code) {
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, SUCCESS);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, UNKNOWN_CMD);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, UNKNOWN_CONN_ID);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, HW_FAILURE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, PAGE_TIMEOUT);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, AUTHENTICATION_FAIL);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, PIN_OR_KEY_MISSING);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, MEM_CAPACITY_EXCEEDED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, CONN_TIMEOUT);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, CONN_LIMIT_EXCEEDED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, SYNC_CONN_LIMIT_EXCEEDED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, CONN_ALREADY_EXISTS);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, CMD_DISALLOWED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, INSUFFICIENT_RESOURCES);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, INSUFFICIENT_SECURITY);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, BD_ADDR_UNACCEPTABLE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, CONN_ACCEPT_TIMEOUT);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, UNSUPP_FEATURE_PARAM_VAL);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, INVALID_PARAM);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, REMOTE_USER_TERM_CONN);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, REMOTE_LOW_RESOURCES);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, REMOTE_POWER_OFF);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, LOCALHOST_TERM_CONN);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, PAIRING_NOT_ALLOWED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, UNSUPP_REMOTE_FEATURE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, INVALID_LL_PARAM);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, UNSPECIFIED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, UNSUPP_LL_PARAM_VAL);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, LL_RESP_TIMEOUT);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, LL_PROC_COLLISION);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, INSTANT_PASSED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, PAIRING_NOT_SUPPORTED);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, DIFF_TRANS_COLLISION);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, UNACCEPT_CONN_PARAM);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, ADV_TIMEOUT);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, TERM_DUE_TO_MIC_FAIL);
		PREFIXED_SWITCH_CASE_RETURN_STRING(BT_HCI_ERR, CONN_FAIL_TO_ESTAB);
	default:
		return "UNKNOWN";
	}
	/* clang-format on */
}

const char *lbt_get_nrf52_reset_reason_string(u8_t code)
{
	/* clang-format off */
	switch (code) {
		PREFIXED_SWITCH_CASE_RETURN_STRING(RESET_REASON, POWER_UP);
		PREFIXED_SWITCH_CASE_RETURN_STRING(RESET_REASON, RESETPIN);
		PREFIXED_SWITCH_CASE_RETURN_STRING(RESET_REASON, DOG);
		PREFIXED_SWITCH_CASE_RETURN_STRING(RESET_REASON, SREQ);
		PREFIXED_SWITCH_CASE_RETURN_STRING(RESET_REASON, LOCKUP);
		PREFIXED_SWITCH_CASE_RETURN_STRING(RESET_REASON, OFF);
		PREFIXED_SWITCH_CASE_RETURN_STRING(RESET_REASON, LPCOMP);
		PREFIXED_SWITCH_CASE_RETURN_STRING(RESET_REASON, DIF);
		PREFIXED_SWITCH_CASE_RETURN_STRING(RESET_REASON, NFC);
		PREFIXED_SWITCH_CASE_RETURN_STRING(RESET_REASON, VBUS);
	default:
		return "UNKNOWN";
	}
	/* clang-format on */
}