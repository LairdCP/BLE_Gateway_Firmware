/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/*see
 * https://github.com/nrfconnect/sdk-nrf/blob/master/include/bluetooth/services/dfu_smp_c.rst#id15
 * for more details
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(dfu_smp_c);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/uuid.h>
#include <kernel.h>
#include <stddef.h>
#include <stdio.h>

#include "dfu_smp_c.h"
#include "ct_ble.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#if CONFIG_CT_DEBUG_SMP_TRANSFERS
static char hex_buf[(2 * CONFIG_CT_LOG_DOWNLOAD_BUFFER_SIZE) + 1];
#endif

#ifdef CONFIG_CT_VERBOSE
#define LOG_VRB(...) LOG_DBG(...)
#else
#define LOG_VRB(...)
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
/** @brief Notification callback function
 *
 *  Internal function used to process the response from the SMP characteristic.
 *
 *  @param conn   Connection handler.
 *  @param params Notification parameters structure - the pointer
 *                to the structure provided to subscribe function.
 *  @param data   Pointer to the data buffer.
 *  @param length The size of the received data.
 *
 *  @retval BT_GATT_ITER_STOP     Stop notification
 *  @retval BT_GATT_ITER_CONTINUE Continue notification
 */
uint8_t bt_gatt_dfu_smp_c_notify(struct bt_conn *conn,
				 struct bt_gatt_subscribe_params *params,
				 const void *data, uint16_t length)
{
	struct bt_gatt_dfu_smp_c *dfu_smp_c;

	dfu_smp_c = CONTAINER_OF(params, struct bt_gatt_dfu_smp_c,
				 notification_params);
	if (!data) {
		/* Notification disabled */
		dfu_smp_c->cbs.rsp_part = NULL;
		params->notify = NULL;
		LOG_VRB("\033[38;5;70mnotification disabled\n");
		return BT_GATT_ITER_STOP;
	}

#if CONFIG_CT_DEBUG_SMP_TRANSFERS
	{
		int i = 0;
		char *hb = hex_buf;
		char *dat = (uint8_t *)data;
		for (i = 0; i < length; i++) {
			hb += sprintf(hb, "%02X", dat[i]);
		}
		LOG_DBG("\033[38;5;198mnotif:\r\n%s\n", hex_buf);
	}
#endif

	if (dfu_smp_c->cbs.rsp_part) {
		dfu_smp_c->rsp_state.chunk_size = length;
		dfu_smp_c->rsp_state.data = data;
		LOG_VRB("len: %d data: %d\n", length, POINTER_TO_UINT(data));

		if (dfu_smp_c->rsp_state.offset == 0) {
			/* First block */
			uint32_t total_len;
			const struct dfu_smp_header *header;

			header = (const struct dfu_smp_header *)data;
			total_len = (((uint16_t)header->len_h8) << 8) |
				    header->len_l8;

			/* make sure to account for SMP header bytes in total size */
			total_len += sizeof(struct dfu_smp_header);

			/* total_len += sizeof(struct dfu_smp_header); */
			dfu_smp_c->rsp_state.total_size = total_len;
#if CONFIG_CT_DEBUG_SMP_TRANSFERS
			LOG_DBG(">>>  total_len: %d, smp_hdr: %02X%02X%02X%02X%02X%02X%02X%02X\n",
				total_len, ((uint8_t *)data)[0],
				((uint8_t *)data)[1], ((uint8_t *)data)[2],
				((uint8_t *)data)[3], ((uint8_t *)data)[4],
				((uint8_t *)data)[5], ((uint8_t *)data)[6],
				((uint8_t *)data)[7]);
#endif
		}
		/* call smp_file_download_rsp_proc() to process the packet */
		dfu_smp_c->cbs.rsp_part(dfu_smp_c);

		/* if rc != 0, abort the transfer */
		if (dfu_smp_c->rsp_state.rc) {
			bt_conn_disconnect(conn,
					   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			return BT_GATT_ITER_STOP;
		}

		dfu_smp_c->rsp_state.offset += length;
		if (dfu_smp_c->rsp_state.offset >=
		    dfu_smp_c->rsp_state.total_size) {
			/* stash the new offset to continue downloading from */
			uint32_t new_off = dfu_smp_c->downloaded_bytes;

			/* Whole response has been received */
			dfu_smp_c->cbs.rsp_part = NULL;

			LOG_VRB("smp rcv complete! (off: %d)\n", new_off);

			/* if there are more bytes in the download or if in the authentication
			 * states, update the offset and send the next request. */
			if ((ct_ble_get_state() !=
			     BT_DEMO_APP_STATE_LOG_DOWNLOAD) ||
			    (dfu_smp_c->file_size == 0 ||
			     (dfu_smp_c->file_size > 0 &&
			      new_off < dfu_smp_c->file_size))) {
				int32_t success =
					ct_ble_send_next_smp_request(new_off);
				if (!success) {
					/* Next command could not be sent due to error. Disconnect. */
					bt_conn_disconnect(
						conn,
						BT_HCI_ERR_REMOTE_USER_TERM_CONN);
					LOG_VRB("disconnecting...\n");
					return BT_GATT_ITER_STOP;
				}
			} else {
				bt_conn_disconnect(
					conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
				LOG_VRB("disconnecting...\n");
				return BT_GATT_ITER_STOP;
			}
		}

	} else {
		LOG_ERR("error in dfu_smp_c.c -EIO (data: %08X, length: %d, offset: %d, "
			"total_size: %d)",
			(uint32_t)data, length, dfu_smp_c->rsp_state.offset,
			dfu_smp_c->rsp_state.total_size);
		return BT_GATT_ITER_STOP;
	}
	return BT_GATT_ITER_CONTINUE;
}

int bt_gatt_dfu_smp_c_init(struct bt_gatt_dfu_smp_c *dfu_smp_c,
			   const struct bt_gatt_dfu_smp_c_init_params *params)
{
	dfu_smp_c->downloaded_bytes = 0;
	dfu_smp_c->file_size = 0;

	return 0;
}

int smp_assign_handles(struct bt_gatt_dfu_smp_c *dfu_smp_c,
		       uint16_t smp_char_handle, uint16_t smp_char_ccc_handle,
		       struct bt_conn *conn)
{
	LOG_DBG("Getting handles from DFU SMP service.");

	dfu_smp_c->handles.smp = smp_char_handle;
	dfu_smp_c->handles.smp_ccc = smp_char_ccc_handle;
	dfu_smp_c->conn = conn;

	return 0;
}

int bt_gatt_dfu_smp_c_command(struct bt_gatt_dfu_smp_c *dfu_smp_c,
			      bt_gatt_dfu_smp_rsp_part_cb rsp_cb,
			      size_t cmd_size, const void *cmd_data)
{
	int ret;

	if (!dfu_smp_c || !rsp_cb || !cmd_data || cmd_size == 0) {
		if (!dfu_smp_c)
			LOG_ERR("dfu_smp_c is null");
		if (!rsp_cb)
			LOG_ERR("rsp_cb is null");
		if (!cmd_data)
			LOG_ERR("cmd_data is null");
		if (!cmd_size)
			LOG_ERR("cmd_size == %d", cmd_size);
		return -EINVAL;
	}
	if (!dfu_smp_c->conn) {
		LOG_ERR("dfu_cmp_c->conn is NULL!");
		return -ENXIO;
	}
	if (cmd_size > bt_gatt_get_mtu(dfu_smp_c->conn)) {
		LOG_ERR("Command size (%u) cannot fit MTU (%u)", cmd_size,
			bt_gatt_get_mtu(dfu_smp_c->conn));
		return -EMSGSIZE;
	}
	if (dfu_smp_c->cbs.rsp_part) {
		LOG_ERR("dfu_smp_c->cbs.rsp_part != 0");
		return -EBUSY;
	}
	/* Sign into notification if not currently enabled */
	if (!dfu_smp_c->notification_params.notify) {
		dfu_smp_c->notification_params.value_handle =
			dfu_smp_c->handles.smp;
		dfu_smp_c->notification_params.ccc_handle =
			dfu_smp_c->handles.smp_ccc;
		dfu_smp_c->notification_params.notify =
			bt_gatt_dfu_smp_c_notify;
		dfu_smp_c->notification_params.value = BT_GATT_CCC_NOTIFY;
		atomic_set_bit(dfu_smp_c->notification_params.flags,
			       BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

		ret = bt_gatt_subscribe(dfu_smp_c->conn,
					&dfu_smp_c->notification_params);
		if (ret) {
			return ret;
		}
	}
	memset(&dfu_smp_c->rsp_state, 0, sizeof(dfu_smp_c->rsp_state));
	dfu_smp_c->cbs.rsp_part = rsp_cb;
	/* Send request */
	LOG_VRB("gatt_write to handle %d of %d bytes\n",
		dfu_smp_c->notification_params.value_handle, cmd_size);
	ret = bt_gatt_write_without_response(
		dfu_smp_c->conn, dfu_smp_c->notification_params.value_handle,
		cmd_data, cmd_size, false);
	if (ret) {
		LOG_ERR("error writing gatt characteristic");
		dfu_smp_c->cbs.rsp_part = NULL;
	}

	return ret;
}

struct bt_conn *
bt_gatt_dfu_smp_c_conn(const struct bt_gatt_dfu_smp_c *dfu_smp_c)
{
	return dfu_smp_c->conn;
}

const struct bt_gatt_dfu_smp_rsp_state *
bt_gatt_dfu_smp_c_rsp_state(const struct bt_gatt_dfu_smp_c *dfu_smp_c)
{
	return &(dfu_smp_c->rsp_state);
}

bool bt_gatt_dfu_smp_c_rsp_total_check(const struct bt_gatt_dfu_smp_c *dfu_smp_c)
{
	return (dfu_smp_c->rsp_state.chunk_size + dfu_smp_c->rsp_state.offset >=
		dfu_smp_c->rsp_state.total_size);
}
