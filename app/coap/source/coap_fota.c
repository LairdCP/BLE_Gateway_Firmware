/*
 * @file coap_fota.c
 * @brief
 *
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(coap_fota, LOG_LEVEL_DBG);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <errno.h>
#include <sys/byteorder.h>
#include <stdio.h>
#include <string.h>

#include <net/socket.h>
#include <net/net_mgmt.h>
#include <net/net_ip.h>
#include <net/udp.h>
#include <net/coap.h>

#ifdef CONFIG_COAP_FOTA_BASE64
#include <mbedtls/base64.h>
#endif

#include "lcz_dns.h"
#include "lcz_sock.h"
#include "coap_fota_query.h"
#include "coap_fota_json_parser.h"
#include "coap_fota.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define BIN_TO_BASE64_SIZE(n) (((4 * (n) / 3) + 3) & ~3)

#define COAP_VERSION 1
#define COAP_PAYLOAD_MARKER 0xFF
#define COAP_MIN_HDR_SIZE 4
#define COAP_TOKEN_SIZE 0
#define COAP_ACK_MSG_SIZE COAP_MIN_HDR_SIZE
#define COAP_CON_MSG_SIZE (COAP_MIN_HDR_SIZE + COAP_TOKEN_SIZE)

#define COAP_OCTET_STREAM_FMT 42
#define COAP_JSON_FMT 50

#define LAST_BLOCK_RETVAL 1

/* clang-format off */
#define PRODUCT_QUERY_STR   "productId"
#define IMAGE_QUERY_STR     "imageId"
#define VERSION_QUERY_STR   "versionId"
#define FILENAME_QUERY_STR  "filename"
#define OFFSET_QUERY_STR    "startByte"
#define LENGTH_QUERY_STR    "length"
#define SIZE_QUERY_STR      "size"
#define HASH_QUERY_STR      "hash"
/* clang-format on */

#define JSON_START_STR "{\"result\""
#define JSON_END_CHAR '}'

/* Check the longest name above */
BUILD_ASSERT(sizeof(PRODUCT_QUERY_STR) <= CONFIG_COAP_FOTA_MAX_NAME_SIZE,
	     "COAP_FOTA_MAX_NAME_SIZE too small");

#define MAX_PARAM_SIZE                                                         \
	(CONFIG_COAP_FOTA_MAX_NAME_SIZE + CONFIG_COAP_FOTA_MAX_PARAMETER_SIZE)

#ifdef CONFIG_COAP_FOTA_USE_PSK
static const sec_tag_t COAP_FOTA_TLS_TAG_LIST[] = {
	CONFIG_COAP_FOTA_CLIENT_TAG
};
#else
static const sec_tag_t COAP_FOTA_TLS_TAG_LIST[] = {
	CONFIG_COAP_FOTA_CA_TAG, CONFIG_COAP_FOTA_CLIENT_TAG
};
#endif

#define BREAK_ON_ERROR(x)                                                      \
	if (r < 0) {                                                           \
		break;                                                         \
	}

typedef struct coap_fota {
	bool credentials_loaded;
	sock_info_t sock_info;

	struct coap_packet request;
	uint8_t request_data[CONFIG_COAP_FOTA_MAX_REQUEST_SIZE];
	uint8_t param[MAX_PARAM_SIZE];

	struct coap_block_context block_context;
	char server_addr[CONFIG_DNS_RESOLVER_ADDR_MAX_SIZE];

	struct coap_packet reply;
	uint8_t reply_buffer[CONFIG_COAP_FOTA_MAX_RESPONSE_SIZE];
	int reply_length;
	uint16_t reply_payload_length;
	size_t payload_total;
	const uint8_t *reply_payload_ptr;

#ifdef CONFIG_COAP_FOTA_BASE64
	uint8_t binary_payload[CONFIG_COAP_FOTA_BASE64_TO_BIN_SIZE];
	size_t binary_length;
#endif
} coap_fota_t;

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static coap_fota_t cf;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/

static int get_payload(void);
static int send_get_size(coap_fota_query_t *p);
static int send_get_hash(coap_fota_query_t *p);
static int send_get_firmware(coap_fota_query_t *p);
static int process_get_firmware_reply(coap_fota_query_t *p);

static int coap_start_client(coap_fota_query_t *p);
static void coap_stop_client(void);
static int coap_addr(struct sockaddr *addr, const uint8_t *peer_name,
		     const uint16_t peer_port);
static void coap_hexdump(const uint8_t *str, const uint8_t *packet,
			 size_t length);

static int process_coap_reply(coap_fota_query_t *p);

static int packet_start(coap_fota_query_t *p, uint8_t method);
static int packet_init(uint8_t method);
static int packet_build_ack_from_con(uint8_t code);
static int packet_append_uri_path(const uint8_t *path);
static int packet_append_uri_query(const uint8_t *query);
static int packet_append_string_query(const char *name, const uint8_t *value);
static int packet_append_unsigned_query(const char *name, int value);
static bool valid_string_parameter(const char *str);

static void coap_block_context_init(coap_fota_query_t *p);
static int coap_block_context_update(void);

static int packet_append_get_size_query(coap_fota_query_t *p);
static int packet_append_get_hash_query(coap_fota_query_t *p);
static int packet_append_get_firmware_query(coap_fota_query_t *p);

static int file_manager(const char *abs_path, bool last_block);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void coap_fota_init(void)
{
	lcz_sock_set_name(&cf.sock_info, "coap_fota");
	lcz_sock_set_events(&cf.sock_info, POLLIN);
}

int coap_fota_get_firmware_size(coap_fota_query_t *p)
{
	if (p == NULL) {
		return -EPERM;
	}

	int r = 0;
	while (1) {
		r = coap_start_client(p);
		BREAK_ON_ERROR(r);

		r = send_get_size(p);
		BREAK_ON_ERROR(r);

		r = process_coap_reply(p);
		BREAK_ON_ERROR(r);

		r = get_payload();
		BREAK_ON_ERROR(r);

		r = coap_fota_json_parser_get_size(cf.reply_payload_ptr,
						   SIZE_QUERY_STR);
		if (r < 0) {
			LOG_ERR("JSON parser did not find size");
			/* This can occur if the filename is not valid. */
		} else {
			p->size = r;
			LOG_DBG("file: %s is %u bytes", log_strdup(p->filename),
				p->size);
		}
		break;
	}

	(void)coap_stop_client();
	return r;
}

int coap_fota_get_hash(coap_fota_query_t *p)
{
	if (p == NULL) {
		return -EPERM;
	}

	int r = 0;
	while (1) {
		r = coap_start_client(p);
		BREAK_ON_ERROR(r);

		r = send_get_hash(p);
		BREAK_ON_ERROR(r);

		r = process_coap_reply(p);
		BREAK_ON_ERROR(r);

		r = get_payload();
		BREAK_ON_ERROR(r);

		r = coap_fota_json_parser_get_hash(
			p->expected_hash, cf.reply_payload_ptr, HASH_QUERY_STR);
		if (r < 0) {
			LOG_ERR("JSON parser did not find hash");
		}
		break;
	}

	(void)coap_stop_client();
	return r;
}

int coap_fota_get_firmware(coap_fota_query_t *p)
{
	if (p == NULL) {
		return -EPERM;
	}

	char abs_path[FSU_MAX_ABS_PATH_SIZE];
	(void)fsu_build_full_name(abs_path, sizeof(abs_path), p->fs_path,
				  p->filename);
	p->block_xfer = true;
	cf.reply_payload_length = 0;
	coap_block_context_init(p);
	int r = coap_start_client(p);
	while (r == 0) {
		r = send_get_firmware(p);
		BREAK_ON_ERROR(r);

		/* Append the file before waiting for the next chunk. */
		if (cf.reply_payload_length > 0) {
			r = file_manager(abs_path, false);
			BREAK_ON_ERROR(r);
		}

		r = process_get_firmware_reply(p);
		if (r == LAST_BLOCK_RETVAL) {
			/* All blocks but the last are pipelined. */
			r = file_manager(abs_path, true);
			break;
		}
		BREAK_ON_ERROR(r);
	}

	LOG_DBG("payload_total: %u", cf.payload_total);
	size_t expected = (p->size - p->offset);
	if (cf.payload_total != expected) {
		LOG_ERR("Download payload size did not match downloaded: %u expected: %u total: %u",
			cf.payload_total, expected, p->size);
		r = -1;
	}

	p->block_xfer = false;
	(void)coap_stop_client();
	return r;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int process_coap_reply(coap_fota_query_t *p)
{
	int r = 0;
	while (1) {
		r = lcz_sock_receive(&cf.sock_info, &cf.reply_buffer,
				     sizeof(cf.reply_buffer),
				     CONFIG_COAP_FOTA_RESPONSE_TIMEOUT_MS);
		BREAK_ON_ERROR(r);
		cf.reply_length = r;

		coap_hexdump("Response", cf.reply_buffer, cf.reply_length);

		r = coap_packet_parse(&cf.reply, cf.reply_buffer,
				      (uint16_t)cf.reply_length, NULL, 0);
		BREAK_ON_ERROR(r);

		/* The CoAP bridge acks the request and then forwards it on. */
		uint8_t type = coap_header_get_type(&cf.reply);
		bool is_ack = (type == COAP_TYPE_ACK);
		if (is_ack && cf.reply_length == COAP_MIN_HDR_SIZE) {
			/* Wait for more data */
		} else {
			/* Confirmed responses require an ACK. */
			if (type == COAP_TYPE_CON) {
				uint8_t code =
					p->block_xfer ?
						COAP_CODE_EMPTY :
						coap_header_get_code(&cf.reply);
				r = packet_build_ack_from_con(code);
				if (r == 0) {
					r = lcz_sock_send(&cf.sock_info,
							  cf.request.data,
							  cf.request.offset, 0);
				}
				LOG_DBG("Sent Ack from Con (%d)", r);
			}
			break;
		}
	}
	return r;
}

static void coap_block_context_init(coap_fota_query_t *p)
{
	memset(&cf.block_context, 0, sizeof(cf.block_context));
	cf.payload_total = 0;

	/* @ref coap_block_transfer_init */
	cf.block_context.block_size = p->block_size;
#ifdef CONFIG_COAP_FOTA_BASE64
	cf.block_context.total_size = BIN_TO_BASE64_SIZE(p->size);
	cf.block_context.current = BIN_TO_BASE64_SIZE(p->offset);
#else
	cf.block_context.total_size = p->size;
	cf.block_context.current = p->offset;
#endif

	LOG_WRN("Block xfer init %d of %d", cf.block_context.current,
		cf.block_context.total_size);
}

static int coap_block_context_update(void)
{
	LOG_DBG("%d of %d", cf.block_context.current,
		cf.block_context.total_size);

	int r = coap_update_from_block(&cf.reply, &cf.block_context);
	if (r < 0) {
		LOG_ERR("Update block error %d", r);
		/* Is data formatted as expected? */
	} else {
		size_t block_offset =
			coap_next_block(&cf.reply, &cf.block_context);
		if (block_offset == 0) {
			LOG_DBG("Last Block");
			r = LAST_BLOCK_RETVAL;
		}
	}
	return r;
}

static int process_get_firmware_reply(coap_fota_query_t *p)
{
	int r = 0;
	while (1) {
		r = process_coap_reply(p);
		BREAK_ON_ERROR(r);

		/* In a block-wise transfer the only payload is the block data. */
		r = get_payload();
		BREAK_ON_ERROR(r);

		r = coap_block_context_update();
		BREAK_ON_ERROR(r);

		break;
	}
	return r;
}

/* The response time averaged 250 ms for each packet regardless of
 * size.  With the pipeline this means that opening, appending, and closing
 * the file takes the same amount of time as opening the file once, appending,
 * and automatic file system flushes.  This is because the extra time for the
 * close is absorbed in the wait.
 *
 * When power was removed during a download, the close method rarely resulted
 * in corrupted files that couldn't be restarted.
 */
static int file_manager(const char *abs_path, bool last_block)
{
	int r = 0;
#ifdef CONFIG_COAP_FOTA_BASE64
	/* All of the CoAP block sizes convert to binary without a remainder.
	 * If an unexpected size is received, then the hash will fail.
	 */
	r = mbedtls_base64_decode(cf.binary_payload, sizeof(cf.binary_payload),
				  &cf.binary_length, cf.reply_payload_ptr,
				  cf.reply_payload_length);
	if (r == 0) {
		r = fsu_append_abs(abs_path, cf.binary_payload,
				   cf.binary_length);
		if (r > 0) {
			r = 0;
			cf.payload_total += cf.binary_length;
		} else {
			r = -1;
		}
	} else {
		LOG_ERR("Base64 conversion error");
		r = -1;
	}
#else
	r = fsu_append_abs(abs_path, (void *)cf.reply_payload_ptr,
			   cf.reply_payload_length);
	cf.payload_total += cf.reply_payload_length;
#endif
	return r;
}

static void coap_hexdump(const uint8_t *str, const uint8_t *packet,
			 size_t length)
{
#ifdef CONFIG_COAP_FOTA_HEXDUMP
	if (packet == NULL) {
		LOG_DBG("%s NULL packet", str);
		return;
	}

	if (!length) {
		LOG_DBG("%s zero-length packet", str);
		return;
	} else {
		LOG_DBG("%s length: %u", str, length);
	}

	LOG_HEXDUMP_DBG(packet, length, str);
#else
	ARG_UNUSED(packet);
	LOG_DBG("%s length: %u", str, length);
#endif
}

static void coap_stop_client(void)
{
	lcz_sock_close(&cf.sock_info);
}

static int coap_load_cred(void)
{
	if (cf.credentials_loaded) {
		return 0;
	}

	int r = 0;
#ifdef CONFIG_COAP_FOTA_USE_PSK
	LOG_DBG("Loading CoAP FOTA PSK");
	LOG_WRN("PSK won't work with Californium if other key exchange formats are enabled");
	r = tls_credential_add(CONFIG_COAP_FOTA_CLIENT_TAG,
			       TLS_CREDENTIAL_PSK_ID, CONFIG_COAP_FOTA_PSK_ID,
			       strlen(CONFIG_COAP_FOTA_PSK_ID));
	if (r < 0) {
		LOG_ERR("Failed to add %s: %d", "psk id", r);
		return r;
	}
	r = tls_credential_add(CONFIG_COAP_FOTA_CLIENT_TAG, TLS_CREDENTIAL_PSK,
			       CONFIG_COAP_FOTA_PSK,
			       strlen(CONFIG_COAP_FOTA_PSK));
	if (r < 0) {
		LOG_ERR("Failed to add %s: %d", "psk", r);
		return r;
	}
#endif

	cf.credentials_loaded = true;
	return r;
}

static int coap_start_client(coap_fota_query_t *p)
{
	if (p->dtls) {
		lcz_sock_enable_dtls(&cf.sock_info, coap_load_cred);
		lcz_sock_set_tls_tag_list(&cf.sock_info, COAP_FOTA_TLS_TAG_LIST,
					  sizeof(COAP_FOTA_TLS_TAG_LIST));
	} else {
		lcz_sock_disable_dtls(&cf.sock_info);
	}

	struct sockaddr addr;
	memset(&addr, 0, sizeof(addr));
	int r = coap_addr(&addr, p->domain, p->port);
	if (r >= 0) {
		r = lcz_udp_sock_start(&cf.sock_info, &addr, NULL);
	}
	return r;
}

static int packet_start(coap_fota_query_t *p, uint8_t method)
{
	int r = 0;

	while (1) {
		r = packet_init(method);
		BREAK_ON_ERROR(r);

#ifdef CONFIG_COAP_FOTA_INCLUDE_HOST_PORT_OPTIONS
		r = coap_packet_append_option(&cf.request, COAP_OPTION_URI_HOST,
					      p->domain, strlen(p->domain));
		BREAK_ON_ERROR(r);

		r = coap_packet_append_option(&cf.request, COAP_OPTION_URI_PORT,
					      (uint8_t *)&p->port,
					      sizeof(p->port));
		BREAK_ON_ERROR(r);
#endif

		r = packet_append_uri_path(p->path);
		BREAK_ON_ERROR(r);

		break;
	}
	return r;
}

static int packet_init(uint8_t method)
{
	int r = coap_packet_init(&cf.request, cf.request_data,
				 sizeof(cf.request_data), COAP_VERSION,
				 COAP_TYPE_CON, COAP_TOKEN_SIZE,
				 coap_next_token(), method, coap_next_id());
	if (r < 0) {
		LOG_ERR("Failed to init CoAP message");
	}
	return r;
}

static int packet_build_ack_from_con(uint8_t code)
{
	uint8_t token[COAP_TOKEN_SIZE];
	uint8_t tkl = coap_header_get_token(&cf.reply, token);
	int r = coap_packet_init(&cf.request, cf.request_data,
				 sizeof(cf.request_data), COAP_VERSION,
				 COAP_TYPE_ACK, tkl, token, code,
				 coap_header_get_id(&cf.reply));
	if (r < 0) {
		LOG_ERR("Failed to build CoAP ACK");
	}
	return r;
}

static int packet_append_uri_path(const uint8_t *path)
{
	int r = 0;
	if (valid_string_parameter(path)) {
		uint8_t *s1 = (uint8_t *)path;
		uint8_t *s2 = NULL;
		uint8_t *end = (uint8_t *)path + strlen(path);
		do {
			if (s1 != NULL) {
				s2 = strchr(s1 + 1,
					    COAP_FOTA_QUERY_URI_PATH_DELIMITER);
			}
			if (s2 == NULL) {
				s2 = end;
			}
			r = coap_packet_append_option(
				&cf.request, COAP_OPTION_URI_PATH, s1, s2 - s1);
			if (r < 0) {
				LOG_ERR("Unable add URI path to request");
			} else if (false) {
				LOG_DBG("Adding %u chars of '%s' to URI path", s2 - s1, log_strdup(s1));
			}
			if (s2 != end) {
				s1 = s2 + 1;
			}
		} while (s1 && (s2 != end) && (r >= 0));
	}
	return r;
}

static int packet_append_uri_query(const uint8_t *query)
{
	int r = 0;
	if (valid_string_parameter(query)) {
		r = coap_packet_append_option(&cf.request,
					      COAP_OPTION_URI_QUERY, query,
					      strlen(query));
		if (r < 0) {
			LOG_ERR("Unable to add URI query to request");
		}
	}
	return r;
}

/* Example:
 * GET fw?productId=mg100&imageId=app&version=1.2.0&size=file.bin
 */
static int send_get_size(coap_fota_query_t *p)
{
	int r = 0;
	while (1) {
		r = packet_start(p, COAP_METHOD_GET);
		BREAK_ON_ERROR(r);

		r = packet_append_get_size_query(p);
		BREAK_ON_ERROR(r);

		coap_hexdump("Request", cf.request.data, cf.request.offset);

		r = lcz_sock_send(&cf.sock_info, cf.request.data,
				  cf.request.offset, 0);
		BREAK_ON_ERROR(r);

		break;
	}
	return r;
}

/* Examples:
 * Stage/fw?productId=mg100&appId=hl7800&versionId=4.4.14.99&hash=file.bin
 * Get a partial file hash
 * fw?productId=mg100&appId=hl7800&versionId=4.4.14.99&hash=file.bin&length=500
 */
static int send_get_hash(coap_fota_query_t *p)
{
	int r = 0;
	while (1) {
		r = packet_start(p, COAP_METHOD_GET);
		BREAK_ON_ERROR(r);

		r = packet_append_get_hash_query(p);
		BREAK_ON_ERROR(r);

		coap_hexdump("Request", cf.request.data, cf.request.offset);

		r = lcz_sock_send(&cf.sock_info, cf.request.data,
				  cf.request.offset, 0);
		BREAK_ON_ERROR(r);

		break;
	}
	return r;
}

static int packet_append_get_size_query(coap_fota_query_t *p)
{
	int r = 0;
	while (1) {
		r = packet_append_string_query(PRODUCT_QUERY_STR, p->product);
		BREAK_ON_ERROR(r);

		r = packet_append_string_query(IMAGE_QUERY_STR, p->image);
		BREAK_ON_ERROR(r);

		r = packet_append_string_query(VERSION_QUERY_STR, p->version);
		BREAK_ON_ERROR(r);

		/* Example: size=bt-image-1.2.0.bin */
		r = packet_append_string_query(SIZE_QUERY_STR, p->filename);
		BREAK_ON_ERROR(r);

		break;
	}
	return r;
}

static int packet_append_get_hash_query(coap_fota_query_t *p)
{
	int r = 0;
	while (1) {
		r = packet_append_string_query(PRODUCT_QUERY_STR, p->product);
		BREAK_ON_ERROR(r);

		r = packet_append_string_query(IMAGE_QUERY_STR, p->image);
		BREAK_ON_ERROR(r);

		r = packet_append_string_query(VERSION_QUERY_STR, p->version);
		BREAK_ON_ERROR(r);

		r = packet_append_string_query(HASH_QUERY_STR, p->filename);
		BREAK_ON_ERROR(r);

		/* Get the hash up to the offset */
		if (coap_fota_resumed_download(p)) {
			r = packet_append_unsigned_query(LENGTH_QUERY_STR,
							 p->offset - 1);
			BREAK_ON_ERROR(r);
		}

		break;
	}
	return r;
}

static int send_get_firmware(coap_fota_query_t *p)
{
	int r = 0;
	while (1) {
		r = packet_start(p, COAP_METHOD_GET);
		BREAK_ON_ERROR(r);

		r = packet_append_get_firmware_query(p);
		BREAK_ON_ERROR(r);

		r = coap_append_block2_option(&cf.request, &cf.block_context);
		if (r < 0) {
			LOG_ERR("Unable to add block2 option.");
			break;
		}

		coap_hexdump("Request", cf.request.data, cf.request.offset);

		r = lcz_sock_send(&cf.sock_info, cf.request.data,
				  cf.request.offset, 0);
		BREAK_ON_ERROR(r);

		break;
	}
	return r;
}

/* GET fw?productId=mg100&imageId=app&version=1.2.0
 * &filename=bt-load-1.2.0.bin&startByte=0&length=1024
 *
 * Stage/fw?productId=pinnacle-mg100&imageId=app&versionId=1.2.3
 * &filename=480-00070-R1.2.3.bin&startByte=512&length=512
 */
static int packet_append_get_firmware_query(coap_fota_query_t *p)
{
	int r = 0;
	while (1) {
		r = packet_append_string_query(PRODUCT_QUERY_STR, p->product);
		BREAK_ON_ERROR(r);

		r = packet_append_string_query(IMAGE_QUERY_STR, p->image);
		BREAK_ON_ERROR(r);

		r = packet_append_string_query(VERSION_QUERY_STR, p->version);
		BREAK_ON_ERROR(r);

		r = packet_append_string_query(FILENAME_QUERY_STR, p->filename);
		BREAK_ON_ERROR(r);

		r = packet_append_unsigned_query(OFFSET_QUERY_STR, p->offset);
		BREAK_ON_ERROR(r);

		r = packet_append_unsigned_query(LENGTH_QUERY_STR,
						 cf.block_context.total_size);
		BREAK_ON_ERROR(r);

		break;
	}
	return r;
}

static int get_payload(void)
{
	int r = 0;
	cf.reply_payload_length = 0;
	cf.reply_payload_ptr =
		coap_packet_get_payload(&cf.reply, &cf.reply_payload_length);
	if (cf.reply_payload_ptr == NULL || cf.reply_payload_length == 0) {
		LOG_ERR("No payload");
		r = -1;
	} else {
		LOG_DBG("length: %u", cf.reply_payload_length);
		r = 0;
	}
	return r;
}

static int coap_addr(struct sockaddr *addr, const uint8_t *peer_name,
		     const uint16_t peer_port)
{
	if (peer_name == NULL) {
		return -EPERM;
	}

	struct addrinfo *dns_result;
	struct addrinfo hints = {
#if defined(CONFIG_NET_IPV6) && defined(CONFIG_NET_IPV4)
		.ai_family = AF_UNSPEC,
#elif defined(CONFIG_NET_IPV6)
		.ai_family = AF_INET6,
#elif defined(CONFIG_NET_IPV4)
		.ai_family = AF_INET,
#else
		.ai_family = AF_UNSPEC
#endif /* defined(CONFIG_NET_IPV6) && defined(CONFIG_NET_IPV4) */
		.ai_socktype = SOCK_DGRAM
	};

	int r = 0;
	while (1) {
		r = dns_resolve_server_addr((char *)peer_name, NULL, &hints,
					    &dns_result);
		BREAK_ON_ERROR(r);

		r = dns_build_addr_string(cf.server_addr, dns_result);
		if (r == 0) {
			LOG_DBG("Resolved %s into %s", log_strdup(peer_name),
				log_strdup(cf.server_addr));
		} else {
			break;
		}

		addr->sa_family = dns_result->ai_family;
		if (dns_result->ai_family == AF_INET6) {
			r = net_addr_pton(dns_result->ai_family, cf.server_addr,
					  &net_sin6(addr)->sin6_addr);
			net_sin6(addr)->sin6_port = htons(peer_port);
		} else if (dns_result->ai_family == AF_INET) {
			r = net_addr_pton(dns_result->ai_family, cf.server_addr,
					  &net_sin(addr)->sin_addr);
			net_sin(addr)->sin_port = htons(peer_port);
		}
		if (r < 0) {
			LOG_ERR("Failed to convert resolved address");
		}
		break;
	}

	freeaddrinfo(dns_result);
	return r;
}

static bool valid_string_parameter(const char *str)
{
	if (str != NULL) {
		if (strlen(str) > 0) {
			return true;
		}
	}
	return false;
}

static int packet_append_string_query(const char *name, const uint8_t *value)
{
	int result = 0;
	memset(cf.param, 0, sizeof(cf.param));
	if (valid_string_parameter(name) && valid_string_parameter(value)) {
		result = snprintk(cf.param, sizeof(cf.param), "%s=%s", name,
				  (char *)value);
		if (result > 0) {
			result = packet_append_uri_query(cf.param);
		} else {
			result = -EPERM;
		}
	}
	if (result < 0) {
		LOG_ERR("Unable to add string query");
	}
	return result;
}

static int packet_append_unsigned_query(const char *name, int value)
{
	int result = 0;
	memset(cf.param, 0, sizeof(cf.param));
	if (valid_string_parameter(name) && value >= 0) {
		result = snprintk(cf.param, sizeof(cf.param), "%s=%u", name,
				  value);
		if (result > 0) {
			result = packet_append_uri_query(cf.param);
		} else {
			result = -EPERM;
		}
	}
	if (result < 0) {
		LOG_ERR("Unable to add unsigned query");
	}
	return result;
}
