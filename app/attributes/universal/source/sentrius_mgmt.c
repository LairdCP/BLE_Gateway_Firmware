/**
 * @file sentrius_mgmt.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <init.h>
#include <limits.h>
#include <string.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_writer.h>
#include "cborattr/cborattr.h"
#include "mgmt/mgmt.h"
#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <mgmt/mcumgr/smp_bt.h>
#include <bluetooth/services/dfu_smp.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>

#include "attr.h"
#include "lcz_qrtc.h"
#include "lcz_bluetooth.h"
#include "file_system_utilities.h"
#include "lcz_memfault.h"

#include "sentrius_mgmt.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define MGMT_OP_NOTIFY 4

/**
 * Command IDs for file system management group.
 *
 * @note Location zero isn't used because API generator doesn't
 * support multiple commands with the same id (even though their
 * group number is different).
 */
/* clang-format off */
/* pystart - mgmt function indices */
#define SENTRIUS_MGMT_ID_GET_PARAMETER                         1
#define SENTRIUS_MGMT_ID_SET_PARAMETER                         2
#define SENTRIUS_MGMT_ID_REV_ECHO                              3
#define SENTRIUS_MGMT_ID_CALIBRATE_THERMISTOR                  4
#define SENTRIUS_MGMT_ID_TEST_LED                              5
#define SENTRIUS_MGMT_ID_CALIBRATE_THERMISTOR_VERSION2         6
#define SENTRIUS_MGMT_ID_SET_RTC                               7
#define SENTRIUS_MGMT_ID_GET_RTC                               8
#define SENTRIUS_MGMT_ID_LOAD_PARAMETER_FILE                   9
#define SENTRIUS_MGMT_ID_DUMP_PARAMETER_FILE                   10
#define SENTRIUS_MGMT_ID_PREPARE_LOG                           11
#define SENTRIUS_MGMT_ID_ACK_LOG                               12
#define SENTRIUS_MGMT_ID_FACTORY_RESET                         13
#define SENTRIUS_MGMT_ID_SHA256                                14
#define SENTRIUS_MGMT_ID_SET_NOTIFY                            15
#define SENTRIUS_MGMT_ID_GET_NOTIFY                            16
#define SENTRIUS_MGMT_ID_DISABLE_NOTIFY                        17
#define SENTRIUS_MGMT_ID_GENERATE_TEST_LOG                     18
#define SENTRIUS_MGMT_ID_UART_LOG_HALT                         19
#define SENTRIUS_MGMT_ID_GENERATE_MEMFAULT_FILE                20
/* pyend */
/* clang-format on */

#define SENTRIUS_MGMT_HANDLER_CNT                                              \
	(sizeof SENTRIUS_MGMT_HANDLERS / sizeof SENTRIUS_MGMT_HANDLERS[0])

#define FLOAT_MAX 3.4028235E38
#define INVALID_PARAM_ID (ATTR_TABLE_SIZE + 1)
#define NOT_A_BOOL -1

#define MAX_PBUF_SIZE MAX(ATTR_MAX_STR_SIZE, ATTR_MAX_BIN_SIZE)

/* Overhead estimate (21 bytes) for notification buffer size.
 * A3                 # map(3)
 *    62              # text(2)
 *       6964         # "id"
 *    19 03E7         # unsigned(999)  - id less than 1000
 *    62              # text(2)
 *       7231         # "r1"
 *    60              # text(0)        - empty string
 *                    # ""
 *    66              # text(6)
 *       726573756C74 # "result"
 *    19 0400         # unsigned(1024) - size
 */
#define CBOR_NOTIFICATION_OVERHEAD 32

#define END_OF_CBOR_ATTR_ARRAY                                                 \
	{                                                                      \
		.attribute = NULL                                              \
	}

#define MGMT_STATUS_CHECK(x) ((x != 0) ? MGMT_ERR_ENOMEM : 0)

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
/* pystart - mgmt handler function defines */
static mgmt_handler_fn get_parameter;
static mgmt_handler_fn set_parameter;
static mgmt_handler_fn rev_echo;
static mgmt_handler_fn calibrate_thermistor;
static mgmt_handler_fn led_lest;
static mgmt_handler_fn calibrate_thermistor_version2;
static mgmt_handler_fn set_rtc;
static mgmt_handler_fn get_rtc;
static mgmt_handler_fn load_parameter_file;
static mgmt_handler_fn dump_parameter_file;
static mgmt_handler_fn prepare_log;
static mgmt_handler_fn ack_log;
static mgmt_handler_fn factory_reset;
static mgmt_handler_fn sha_256;
static mgmt_handler_fn set_notify;
static mgmt_handler_fn get_notify;
static mgmt_handler_fn disable_notify;
static mgmt_handler_fn generate_test_log;
static mgmt_handler_fn uart_log_halt;
static mgmt_handler_fn generate_memfault_file;
/* pyend */

static int sentrius_mgmt_init(const struct device *device);

static int cbor_encode_attribute(CborEncoder *encoder,
				 long long unsigned int param_id);

static void map_attr_to_cbor_attr(attr_id_t param_id,
				  struct cbor_attr_t *cbor_attr);

static int set_attribute(attr_id_t id, struct cbor_attr_t *cbor_attr);

static int factory_reset(struct mgmt_ctxt *ctxt);
static int sha_256(struct mgmt_ctxt *ctxt);

static void smp_ble_disconnected(struct bt_conn *conn, uint8_t reason);
static void smp_ble_connected(struct bt_conn *conn, uint8_t err);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
/* clang-format off */
static const struct mgmt_handler SENTRIUS_MGMT_HANDLERS[] = {
	[SENTRIUS_MGMT_ID_GET_PARAMETER] = {
		.mh_read = get_parameter,
		.mh_write = NULL
	},
	[SENTRIUS_MGMT_ID_SET_PARAMETER] = {
		.mh_write = set_parameter,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_CALIBRATE_THERMISTOR] = {
		.mh_write = calibrate_thermistor,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_TEST_LED] = {
		.mh_write = NULL,
		.mh_read = led_lest
	},
	[SENTRIUS_MGMT_ID_REV_ECHO] = {
		.mh_write = rev_echo,
		.mh_read = rev_echo
	},
	[SENTRIUS_MGMT_ID_CALIBRATE_THERMISTOR_VERSION2] = {
		.mh_write = calibrate_thermistor_version2,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_SET_RTC] = {
		.mh_write = set_rtc,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_GET_RTC] = {
		.mh_write = NULL,
		.mh_read = get_rtc
	},
	[SENTRIUS_MGMT_ID_LOAD_PARAMETER_FILE] = {
		.mh_write = load_parameter_file,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_DUMP_PARAMETER_FILE] = {
		.mh_write = dump_parameter_file,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_PREPARE_LOG] = {
		.mh_write = prepare_log,
		.mh_read = NULL,
    },
    [SENTRIUS_MGMT_ID_ACK_LOG] = {
		.mh_write = ack_log,
		.mh_read = NULL,
    },
	[SENTRIUS_MGMT_ID_FACTORY_RESET] = {
		.mh_write = factory_reset,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_SHA256] = {
		.mh_write = sha_256,
		.mh_read = sha_256
	},
	[SENTRIUS_MGMT_ID_SET_NOTIFY] = {
		.mh_write = set_notify,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_GET_NOTIFY] = {
		.mh_write = NULL,
		.mh_read = get_notify
	},
	[SENTRIUS_MGMT_ID_DISABLE_NOTIFY] = {
		.mh_write = disable_notify,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_GENERATE_TEST_LOG] = {
		.mh_write = generate_test_log,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_UART_LOG_HALT] = {
		.mh_write = uart_log_halt,
		.mh_read = NULL
	},
	[SENTRIUS_MGMT_ID_GENERATE_MEMFAULT_FILE] = {
		.mh_write = generate_memfault_file,
		.mh_read = NULL
	}

};
/* clang-format on */

static struct mgmt_group sentrius_mgmt_group = {
	.mg_handlers = SENTRIUS_MGMT_HANDLERS,
	.mg_handlers_count = SENTRIUS_MGMT_HANDLER_CNT,
	.mg_group_id = CONFIG_MGMT_GROUP_ID_SENTRIUS,
};

/* Only one parameter is written or read at a time. */
static union {
	long long unsigned int uinteger;
	long long int integer;
	bool boolean;
	char buf[MAX_PBUF_SIZE];
	float fval;
} param;

static size_t buf_size;

struct smp_notification {
	struct dfu_smp_header header;
	uint8_t buffer[MAX_PBUF_SIZE + CBOR_NOTIFICATION_OVERHEAD];
} __packed;

/* The connection callback is used to reference the connection handle.
 * This allows SMP notifications to be sent.
 * Typical SMP is command-response.
 */
static struct {
	struct bt_conn *conn_handle;
	struct bt_conn_cb conn_callbacks;
	struct smp_notification cmd;
} smp_ble;

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
SYS_INIT(sentrius_mgmt_init, APPLICATION, 99);

/* callback from attribute module */
int attr_notify(attr_id_t Index)
{
	int err = 0;
	CborEncoder cbor;
	CborEncoder cbor_map;
	struct cbor_buf_writer writer;
	size_t payload_len;
	size_t total_len;
	uint16_t mtu;

	if (smp_ble.conn_handle == NULL) {
		return -ENOTCONN;
	}

	cbor_buf_writer_init(&writer, smp_ble.cmd.buffer,
			     sizeof(smp_ble.cmd.buffer));
	cbor_encoder_init(&cbor, &writer.enc, 0);

	do {
		err |= cbor_encoder_create_map(&cbor, &cbor_map,
					       CborIndefiniteLength);
		err |= cbor_encode_attribute(&cbor, Index);
		err |= cbor_encoder_close_container(&cbor, &cbor_map);
		if (err < 0) {
			break;
		}

		payload_len = (size_t)(writer.ptr - smp_ble.cmd.buffer);
		total_len = sizeof(smp_ble.cmd.header) + payload_len;
		mtu = bt_gatt_get_mtu(smp_ble.conn_handle);
		if (total_len > BT_MAX_PAYLOAD(mtu)) {
			err = -EMSGSIZE;
			break;
		}

		smp_ble.cmd.header.op = MGMT_OP_NOTIFY;
		smp_ble.cmd.header.flags = 0;
		smp_ble.cmd.header.len_h8 =
			(uint8_t)((payload_len >> 8) & 0xFF);
		smp_ble.cmd.header.len_l8 =
			(uint8_t)((payload_len >> 0) & 0xFF);
		smp_ble.cmd.header.group_h8 = 0;
		smp_ble.cmd.header.group_l8 = CONFIG_MGMT_GROUP_ID_SENTRIUS;
		smp_ble.cmd.header.seq = 0;
		smp_ble.cmd.header.id = SENTRIUS_MGMT_ID_GET_PARAMETER;

		err = smp_bt_tx_rsp(smp_ble.conn_handle, &smp_ble.cmd,
				    total_len);
	} while (0);

	return err;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int sentrius_mgmt_init(const struct device *device)
{
	ARG_UNUSED(device);

	mgmt_register_group(&sentrius_mgmt_group);

	smp_ble.conn_callbacks.connected = smp_ble_connected;
	smp_ble.conn_callbacks.disconnected = smp_ble_disconnected;
	bt_conn_cb_register(&smp_ble.conn_callbacks);

	return 0;
}

static void smp_ble_connected(struct bt_conn *conn, uint8_t err)
{
	/* Did a central connect to us? */
	if (!lbt_peripheral_role(conn)) {
		return;
	}

	if (err) {
		bt_conn_unref(conn);
		smp_ble.conn_handle = NULL;
	} else {
		smp_ble.conn_handle = bt_conn_ref(conn);
	}
}

static void smp_ble_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (!lbt_peripheral_role(conn)) {
		return;
	}

	bt_conn_unref(conn);
	smp_ble.conn_handle = NULL;

	attr_disable_notify();
}

static int get_parameter(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	long long unsigned int param_id = INVALID_PARAM_ID;

	struct cbor_attr_t params_attr[] = {
		{
			.attribute = "p1",
			.type = CborAttrUnsignedIntegerType,
			.addr.uinteger = &param_id,
			.nodefault = true,
		},
		END_OF_CBOR_ATTR_ARRAY
	};

	/* Get the parameter ID */
	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	if (param_id == INVALID_PARAM_ID) {
		return MGMT_ERR_EINVAL;
	}

	/* Encode the response */
	err |= cbor_encode_attribute(&ctxt->encoder, param_id);

	return MGMT_STATUS_CHECK(err);
}

/*
 * Map the attribute type to the CBOR type, get the value, and then encode it.
 */
static int cbor_encode_attribute(CborEncoder *encoder,
				 long long unsigned int param_id)
{
	CborError err = 0;
	int size = -EINVAL;
	attr_id_t id = (attr_id_t)param_id;
	enum attr_type type = attr_get_type(id);

	err |= cbor_encode_text_stringz(encoder, "id");
	err |= cbor_encode_uint(encoder, param_id);
	err |= cbor_encode_text_stringz(encoder, "r1");

	switch (type) {
	case ATTR_TYPE_S8:
	case ATTR_TYPE_S16:
	case ATTR_TYPE_S32:
	case ATTR_TYPE_S64:
		size = attr_get(id, &param.integer, sizeof(param.integer));
		err |= cbor_encode_int(encoder, param.integer);
		break;

	case ATTR_TYPE_U8:
	case ATTR_TYPE_U16:
	case ATTR_TYPE_U32:
	case ATTR_TYPE_U64:
		size = attr_get(id, &param.uinteger, sizeof(param.uinteger));
		err |= cbor_encode_uint(encoder, param.uinteger);
		break;

	case ATTR_TYPE_STRING:
		size = attr_get(id, param.buf, MAX_PBUF_SIZE);
		err |= cbor_encode_text_stringz(encoder, param.buf);
		break;

	case ATTR_TYPE_FLOAT:
		size = attr_get(id, &param.fval, sizeof(param.fval));
		err |= cbor_encode_floating_point(encoder, CborFloatType,
						  &param.fval);
		break;

	case ATTR_TYPE_BOOL:
		size = attr_get(id, &param.boolean, sizeof(param.boolean));
		err |= cbor_encode_boolean(encoder, param.boolean);
		break;

	case ATTR_TYPE_BYTE_ARRAY:
		size = attr_get(id, param.buf, MAX_PBUF_SIZE);
		err |= cbor_encode_byte_string(encoder, param.buf,
					       MAX(0, size));
		break;

	default:
		/* Add dummy r1 parameter */
		param.uinteger = 0;
		err |= cbor_encode_uint(encoder, param.uinteger);
		size = -EINVAL;
		break;
	}

	err |= cbor_encode_text_stringz(encoder, "result");
	err |= cbor_encode_int(encoder, size);

	return err;
}

static int rev_echo(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	char echo_buf[64] = { 0 };
	char rev_buf[64] = { 0 };
	size_t echo_len = 0;

	const struct cbor_attr_t attrs[] = {
		{
			.attribute = "d",
			.type = CborAttrTextStringType,
			.addr.string = echo_buf,
			.len = sizeof echo_buf,
			.nodefault = true,
		},
		END_OF_CBOR_ATTR_ARRAY
	};

	echo_buf[0] = '\0';

	if (cbor_read_object(&ctxt->it, attrs) != 0) {
		return MGMT_ERR_EINVAL;
	}

	echo_len = strlen(echo_buf);
	size_t i;
	for (i = 0; i < echo_len; i++) {
		rev_buf[i] = echo_buf[echo_len - 1 - i];
	}

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_text_string(&ctxt->encoder, rev_buf, echo_len);

	return MGMT_STATUS_CHECK(err);
}

static int set_parameter(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	long long unsigned int param_id = INVALID_PARAM_ID;
	struct CborValue saved_context = ctxt->it;
	int setResult = -1;

	struct cbor_attr_t params_attr[] = {
		{ .attribute = "p1",
		  .type = CborAttrUnsignedIntegerType,
		  .addr.uinteger = &param_id,
		  .nodefault = true },
		END_OF_CBOR_ATTR_ARRAY
	};

	struct cbor_attr_t expected[] = {
		{ .attribute = "p2", .nodefault = true }, END_OF_CBOR_ATTR_ARRAY
	};

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	if (param_id == INVALID_PARAM_ID) {
		return MGMT_ERR_EINVAL;
	}

	map_attr_to_cbor_attr((attr_id_t)param_id, expected);

	/* If the type of object isn't found in the CBOR, then the client could
	 * be trying to write the wrong type of value.
	 */
	if (cbor_read_object(&saved_context, expected) != 0) {
		return MGMT_ERR_EINVAL;
	}
	setResult = set_attribute((attr_id_t)param_id, expected);

	err |= cbor_encode_text_stringz(&ctxt->encoder, "id");
	err |= cbor_encode_uint(&ctxt->encoder, param_id);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "result");
	err |= cbor_encode_int(&ctxt->encoder, setResult);

	return MGMT_STATUS_CHECK(err);
}

static int led_lest(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	int r = -EINVAL;
	long long unsigned int duration = ULLONG_MAX;

	struct cbor_attr_t params_attr[] = {
		{ .attribute = "p1",
		  .type = CborAttrUnsignedIntegerType,
		  .addr.uinteger = &duration,
		  .nodefault = true },
		END_OF_CBOR_ATTR_ARRAY
	};

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	if (duration < UINT32_MAX) {
		r = sentrius_mgmt_led_test(duration);
	}

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, r);

	return MGMT_STATUS_CHECK(err);
}

static int calibrate_thermistor(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	int r = -EINVAL;
	float c1 = 0.0;
	float c2 = 0.0;
	float ge = 0.0;
	float oe = 0.0;

	struct cbor_attr_t params_attr[] = { { .attribute = "p1",
					       .type = CborAttrFloatType,
					       .addr.fval = &c1,
					       .nodefault = true },
					     { .attribute = "p2",
					       .type = CborAttrFloatType,
					       .addr.fval = &c2,
					       .nodefault = true },
					     END_OF_CBOR_ATTR_ARRAY };

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	r = sentrius_mgmt_calibrate_thermistor(c1, c2, &ge, &oe);

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, r);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "ge");
	err |= cbor_encode_floating_point(&ctxt->encoder, CborFloatType, &ge);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "oe");
	err |= cbor_encode_floating_point(&ctxt->encoder, CborFloatType, &oe);

	return MGMT_STATUS_CHECK(err);
}

static int calibrate_thermistor_version2(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	int r = -EINVAL;
	long long unsigned int c1 = 0;
	long long unsigned int c2 = 0;
	float ge = 0.0;
	float oe = 0.0;

	struct cbor_attr_t params_attr[] = {
		{ .attribute = "p1",
		  .type = CborAttrUnsignedIntegerType,
		  .addr.uinteger = &c1,
		  .nodefault = true },
		{ .attribute = "p2",
		  .type = CborAttrUnsignedIntegerType,
		  .addr.uinteger = &c2,
		  .nodefault = true },
		END_OF_CBOR_ATTR_ARRAY
	};

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	r = sentrius_mgmt_calibrate_thermistor(((float)c1 / 10000.0),
					       ((float)c2 / 10000.0), &ge, &oe);

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, r);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "ge");
	err |= cbor_encode_floating_point(&ctxt->encoder, CborFloatType, &ge);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "oe");
	err |= cbor_encode_floating_point(&ctxt->encoder, CborFloatType, &oe);

	return MGMT_STATUS_CHECK(err);
}

static int set_rtc(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	int r = -EPERM;
	int t = 0;
	long long unsigned int epoch = ULLONG_MAX;

	struct cbor_attr_t params_attr[] = {
		{ .attribute = "p1",
		  .type = CborAttrUnsignedIntegerType,
		  .addr.uinteger = &epoch,
		  .nodefault = true },
		END_OF_CBOR_ATTR_ARRAY
	};

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	if (epoch < UINT32_MAX) {
		r = attr_set_uint32(ATTR_ID_qrtcLastSet, epoch);
		t = lcz_qrtc_set_epoch(epoch);
	}

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, r);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "t");
	err |= cbor_encode_int(&ctxt->encoder, t);

	return MGMT_STATUS_CHECK(err);
}

static int get_rtc(struct mgmt_ctxt *ctxt)
{
	int t = lcz_qrtc_get_epoch();
	CborError err = 0;

	err |= cbor_encode_text_stringz(&ctxt->encoder, "t");
	err |= cbor_encode_int(&ctxt->encoder, t);

	return MGMT_STATUS_CHECK(err);
}

static int load_parameter_file(struct mgmt_ctxt *ctxt)
{
	int r = -EPERM;
	CborError err = 0;

	/* The input file is an optional parameter. */
	strncpy(param.buf, attr_get_quasi_static(ATTR_ID_loadPath),
		sizeof(param.buf));

	struct cbor_attr_t params_attr[] = { { .attribute = "p1",
					       .type = CborAttrTextStringType,
					       .addr.string = param.buf,
					       .len = sizeof(param.buf),
					       .nodefault = false },
					     END_OF_CBOR_ATTR_ARRAY };

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	r = attr_load(param.buf);

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, r);

	return MGMT_STATUS_CHECK(err);
}

static int dump_parameter_file(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	int r = -EPERM;
	long long unsigned int type = ULLONG_MAX;
	char *fstr = NULL;

	/* The output file is an optional parameter. */
	strncpy(param.buf, attr_get_quasi_static(ATTR_ID_dumpPath),
		sizeof(param.buf));

	struct cbor_attr_t params_attr[] = {
		{ .attribute = "p1",
		  .type = CborAttrUnsignedIntegerType,
		  .addr.uinteger = &type,
		  .nodefault = true },
		{ .attribute = "p2",
		  .type = CborAttrTextStringType,
		  .addr.string = param.buf,
		  .len = sizeof(param.buf),
		  .nodefault = false },
		END_OF_CBOR_ATTR_ARRAY
	};

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	/* This will malloc a string as large as maximum parameter file size. */
	if (type < UINT8_MAX) {
		r = attr_prepare_then_dump(&fstr, type);
		if (r >= 0) {
			r = fsu_write_abs(param.buf, fstr, strlen(fstr));
			k_free(fstr);
		}
	}

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, r);
	if (r >= 0) {
		err |= cbor_encode_text_stringz(&ctxt->encoder, "n");
		err |= cbor_encode_text_string(&ctxt->encoder, param.buf,
					       strlen(param.buf));
	}

	return MGMT_STATUS_CHECK(err);
}

static int prepare_log(struct mgmt_ctxt *ctxt)
{
	ARG_UNUSED(ctxt);

	return MGMT_ERR_ENOTSUP;
}

static int ack_log(struct mgmt_ctxt *ctxt)
{
	ARG_UNUSED(ctxt);

	return MGMT_ERR_ENOTSUP;
}

static int generate_test_log(struct mgmt_ctxt *ctxt)
{
	ARG_UNUSED(ctxt);

	return MGMT_ERR_ENOTSUP;
}

static int factory_reset(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	int r = sentrius_mgmt_factory_reset();

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, r);

	return MGMT_STATUS_CHECK(err);
}

static int sha_256(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	int r = -EPERM;
	uint8_t hash[FSU_HASH_SIZE] = { 0x00 };
	size_t name_length;
	size_t file_size;

#ifdef CONFIG_FSU_HASH
	memset(param.buf, 0, sizeof(param.buf));

	struct cbor_attr_t params_attr[] = { { .attribute = "p1",
					       .type = CborAttrTextStringType,
					       .addr.string = param.buf,
					       .len = sizeof(param.buf),
					       .nodefault = true },
					     END_OF_CBOR_ATTR_ARRAY };

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	name_length = strlen(param.buf);
	if ((name_length > 0) &&
	    (name_length < CONFIG_FSU_MAX_FILE_NAME_SIZE)) {
		file_size = fsu_get_file_size_abs(param.buf);
		if (file_size > 0) {
			r = fsu_sha256_abs(hash, param.buf, file_size);
		}
	}
#endif

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, r);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "h");
	err |= cbor_encode_byte_string(&ctxt->encoder, hash, FSU_HASH_SIZE);

	return MGMT_STATUS_CHECK(err);
}

static int set_notify(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	long long unsigned int param_id = INVALID_PARAM_ID;
	/* Use an integer to check if the boolean type was found. */
	union {
		long long int integer;
		bool boolean;
	} value;
	value.integer = NOT_A_BOOL;

	struct cbor_attr_t params_attr[] = {
		{
			.attribute = "p1",
			.type = CborAttrUnsignedIntegerType,
			.addr.uinteger = &param_id,
			.nodefault = true,
		},
		{
			.attribute = "p2",
			.type = CborAttrBooleanType,
			.addr.boolean = &value.boolean,
			.nodefault = true,
		},
		END_OF_CBOR_ATTR_ARRAY
	};

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	if (param_id == INVALID_PARAM_ID || value.integer == NOT_A_BOOL) {
		return MGMT_ERR_EINVAL;
	}

	err |= cbor_encode_text_stringz(&ctxt->encoder, "id");
	err |= cbor_encode_uint(&ctxt->encoder, param_id);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder,
			       attr_set_notify((attr_id_t)param_id,
					       value.boolean));

	return MGMT_STATUS_CHECK(err);
}

static int get_notify(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	long long unsigned int param_id = INVALID_PARAM_ID;

	struct cbor_attr_t params_attr[] = {
		{ .attribute = "p1",
		  .type = CborAttrUnsignedIntegerType,
		  .addr.uinteger = &param_id,
		  .nodefault = true },
		END_OF_CBOR_ATTR_ARRAY
	};

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	if (param_id == INVALID_PARAM_ID) {
		return MGMT_ERR_EINVAL;
	}

	err |= cbor_encode_text_stringz(&ctxt->encoder, "id");
	err |= cbor_encode_uint(&ctxt->encoder, param_id);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_boolean(&ctxt->encoder,
				   attr_get_notify((attr_id_t)param_id));

	return MGMT_STATUS_CHECK(err);
}

static int disable_notify(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, attr_disable_notify());

	return MGMT_STATUS_CHECK(err);
}

#ifdef CONFIG_SHELL_BACKEND_SERIAL
static int uart_log_halt(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	/* Use an integer to check if the boolean type was found. */
	union {
		long long int integer;
		bool boolean;
	} value;
	value.integer = NOT_A_BOOL;
	int r = -EPERM;

	struct cbor_attr_t params_attr[] = {
		{
			.attribute = "p1",
			.type = CborAttrBooleanType,
			.addr.boolean = &value.boolean,
			.nodefault = true,
		},
		END_OF_CBOR_ATTR_ARRAY
	};

	if (cbor_read_object(&ctxt->it, params_attr) != 0) {
		return MGMT_ERR_EINVAL;
	}

	if (value.integer == NOT_A_BOOL) {
		return MGMT_ERR_EINVAL;
	}

	if (value.boolean) {
		r = shell_execute_cmd(shell_backend_uart_get_ptr(), "log halt");
	} else {
		r = shell_execute_cmd(shell_backend_uart_get_ptr(), "log go");
	}

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, r);

	return MGMT_STATUS_CHECK(err);
}
#else
static int uart_log_halt(struct mgmt_ctxt *ctxt)
{
	ARG_UNUSED(ctxt);

	return MGMT_ERR_ENOTSUP;
}
#endif

#ifdef CONFIG_LCZ_MEMFAULT_FILE
static int generate_memfault_file(struct mgmt_ctxt *ctxt)
{
	CborError err = 0;
	size_t file_size = 0;
	bool has_core_dump = false;
	int r = lcz_memfault_save_data_to_file(
		CONFIG_SENTRIUS_MGMT_MEMFAULT_FILE_NAME, &file_size,
		&has_core_dump);

	err |= cbor_encode_text_stringz(&ctxt->encoder, "r");
	err |= cbor_encode_int(&ctxt->encoder, r);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "s");
	err |= cbor_encode_int(&ctxt->encoder, file_size);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "c");
	err |= cbor_encode_boolean(&ctxt->encoder, has_core_dump);
	err |= cbor_encode_text_stringz(&ctxt->encoder, "f");
	err |= cbor_encode_text_string(
		&ctxt->encoder, CONFIG_SENTRIUS_MGMT_MEMFAULT_FILE_NAME,
		strlen(CONFIG_SENTRIUS_MGMT_MEMFAULT_FILE_NAME));
	err |= cbor_encode_text_stringz(&ctxt->encoder, "k");
	err |= cbor_encode_text_string(&ctxt->encoder,
				       CONFIG_MEMFAULT_NCS_PROJECT_KEY,
				       strlen(CONFIG_MEMFAULT_NCS_PROJECT_KEY));

	return MGMT_STATUS_CHECK(err);
}
#else
static int generate_memfault_file(struct mgmt_ctxt *ctxt)
{
	ARG_UNUSED(ctxt);

	return MGMT_ERR_ENOTSUP;
}
#endif

/* Get the CBOR type from the attribute type.
 * Map the CBOR attribute data structure to the type that is expected.
 * (Then read the CBOR again looking for the expected type.)
 */
static void map_attr_to_cbor_attr(attr_id_t param_id,
				  struct cbor_attr_t *cbor_attr)
{
	enum attr_type type = attr_get_type(param_id);

	switch (type) {
	case ATTR_TYPE_BOOL:
		param.boolean = false;
		cbor_attr->type = CborAttrBooleanType;
		cbor_attr->addr.boolean = &param.boolean;
		break;
	case ATTR_TYPE_S8:
	case ATTR_TYPE_S16:
	case ATTR_TYPE_S32:
	case ATTR_TYPE_S64:
		param.integer = LLONG_MAX;
		cbor_attr->type = CborAttrIntegerType;
		cbor_attr->addr.integer = &param.integer;
		break;
	case ATTR_TYPE_U8:
	case ATTR_TYPE_U16:
	case ATTR_TYPE_U32:
	case ATTR_TYPE_U64:
		param.uinteger = ULLONG_MAX;
		cbor_attr->type = CborAttrUnsignedIntegerType;
		cbor_attr->addr.integer = &param.uinteger;
		break;
	case ATTR_TYPE_STRING:
		memset(param.buf, 0, MAX_PBUF_SIZE);
		cbor_attr->type = CborAttrTextStringType;
		cbor_attr->addr.string = param.buf;
		cbor_attr->len = sizeof(param.buf);
		break;
	case ATTR_TYPE_FLOAT:
		param.fval = FLOAT_MAX;
		cbor_attr->type = CborAttrFloatType;
		cbor_attr->addr.fval = &param.fval;
		break;
	case ATTR_TYPE_BYTE_ARRAY:
		memset(param.buf, 0, MAX_PBUF_SIZE);
		cbor_attr->type = CborAttrArrayType;
		cbor_attr->addr.bytestring.data = param.buf;
		cbor_attr->addr.bytestring.len = &buf_size;
		break;
	default:
		cbor_attr->type = CborAttrNullType;
		cbor_attr->attribute = NULL;
		break;
	};
}

static int set_attribute(attr_id_t id, struct cbor_attr_t *cbor_attr)
{
	int status = -EINVAL;
	enum attr_type type = ATTR_TYPE_ANY;

	/* It is safe to use ATTR_TYPE_ANY here because map_attr_to_cbor_attr
	 * and subsequent cbor_read_object validate the type.
	 */

	switch (cbor_attr->type) {
	case CborAttrIntegerType:
		status = attr_set(id, type, cbor_attr->addr.integer,
				  sizeof(int64_t));
		break;

	case CborAttrUnsignedIntegerType:
		status = attr_set(id, type, cbor_attr->addr.uinteger,
				  sizeof(uint64_t));
		break;

	case CborAttrTextStringType:
		status = attr_set(id, type, cbor_attr->addr.string,
				  strlen(cbor_attr->addr.string));
		break;

	case CborAttrFloatType:
		status =
			attr_set(id, type, cbor_attr->addr.fval, sizeof(float));
		break;

	case CborAttrBooleanType:
		status = attr_set(id, type, cbor_attr->addr.boolean,
				  sizeof(bool));
		break;

	case CborAttrByteStringType:
		status = attr_set(id, type, cbor_attr->addr.bytestring.data,
				  *(cbor_attr->addr.bytestring.len));
		break;

	default:
		break;
	}
	return status;
}

/******************************************************************************/
/* Override in application                                                    */
/******************************************************************************/
__weak int sentrius_mgmt_calibrate_thermistor(float c1, float c2, float *ge,
					      float *oe)
{
	return -EPERM;
}

__weak int sentrius_mgmt_led_test(uint32_t duration)
{
	return -EPERM;
}

__weak int sentrius_mgmt_factory_reset(void)
{
	return -EPERM;
}
