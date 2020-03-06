/* laird_bluetooth.c - Common Bluetooth operations.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(laird_bluetooth);

#include "laird_bluetooth.h"

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
			const struct bt_gatt_attr *attr, void *buf, u16_t len,
			u16_t offset)
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

u16_t lbt_find_gatt_index(struct bt_uuid *uuid, struct bt_gatt_attr *gatt,
			  size_t size)
{
	size_t i = 0;
	while (i < size) {
		if (memcmp(gatt[i].uuid, uuid,
			   sizeof(struct bt_uuid_128)) == 0) {
			return (u16_t)i;
		}
		++i;
	}

	/* Not found */
	__ASSERT(0, "GATT handle for characteristic not found");
	return 0;
}
