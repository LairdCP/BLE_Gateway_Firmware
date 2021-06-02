/**
 * @file lcz_lwm2m_client.c
 * @brief
 *
 * Copyright (c) 2017 Linaro Limited
 * Copyright (c) 2017-2019 Foundries.io
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(lwm2m_client);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <zephyr/types.h>
#include <drivers/gpio.h>
#include <net/lwm2m.h>
#include <string.h>
#include <stddef.h>
#include <random/rand32.h>

#include "lcz_dns.h"
#include "led_configuration.h"
#include "dis.h"
#include "lcz_qrtc.h"
#include "lcz_software_reset.h"
#include "attr.h"
#include "lcz_lwm2m_client.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#if !defined(CONFIG_NET_IPV6) && !defined(CONFIG_NET_IPV4)
#error LwM2M requires either IPV6 or IPV4 support
#endif

#if defined(CONFIG_LWM2M_DTLS_SUPPORT)
#define TLS_TAG CONFIG_LWM2M_PSK_TAG
#endif /* CONFIG_LWM2M_DTLS_SUPPORT */

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static uint8_t led_state;
static uint32_t lwm2m_time;
static struct lwm2m_ctx client;
static bool lwm2m_initialized;
static char server_addr[CONFIG_DNS_RESOLVER_ADDR_MAX_SIZE];

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void lwm2m_client_init_internal(void);

static int device_reboot_cb(uint16_t obj_inst_id, uint8_t *args,
			    uint16_t args_len);
static int device_factory_default_cb(uint16_t obj_inst_id, uint8_t *args,
				     uint16_t args_len);
static int lwm2m_setup(const char *serial_number, const char *id);
static void rd_client_event(struct lwm2m_ctx *client,
			    enum lwm2m_rd_client_event client_event);
static int led_on_off_cb(uint16_t obj_inst_id, uint16_t res_id,
			 uint16_t res_inst_id, uint8_t *data, uint16_t data_len,
			 bool last_block, size_t total_size);
static int resolve_server_address(void);
static void create_bl654_sensor_objects(void);
static struct float32_value make_float_value(float v);
static size_t lwm2m_str_size(const char *s);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void lwm2m_client_init(void)
{
	lwm2m_client_init_internal();
}

int lwm2m_set_bl654_sensor_data(float temperature, float humidity,
				float pressure)
{
	int result = 0;

	if (lwm2m_initialized) {
		struct float32_value float_value;

#ifdef CONFIG_LWM2M_IPSO_TEMP_SENSOR
		float_value = make_float_value(temperature);
		result += lwm2m_engine_set_float32("3303/0/5700", &float_value);
#endif

		/* Temperature is used to test generic sensor */
#ifdef CONFIG_LWM2M_IPSO_GENERIC_SENSOR
		float_value = make_float_value(temperature);
		result += lwm2m_engine_set_float32("3303/0/5700", &float_value);
#endif

#ifdef CONFIG_LWM2M_IPSO_HUMIDITY_SENSOR
		float_value = make_float_value(humidity);
		result += lwm2m_engine_set_float32("3304/0/5700", &float_value);
#endif

#ifdef CONFIG_LWM2M_IPSO_PRESSURE_SENSOR
		float_value = make_float_value(pressure);
		result += lwm2m_engine_set_float32("3323/0/5700", &float_value);
#endif
	}
	return result;
}

int lwm2m_generate_psk(void)
{
	int r = -EPERM;
	uint8_t psk[ATTR_LWM2M_PSK_SIZE];
	uint8_t cmd = attr_get_uint32(ATTR_ID_generatePsk,
				      GENERATE_PSK_LWM2M_DEFAULT);

	switch (cmd) {
	case GENERATE_PSK_LWM2M_DEFAULT:
		LOG_DBG("Setting PSK to default");
		r = attr_default(ATTR_ID_lwm2mPsk);

		break;

	case GENERATE_PSK_LWM2M_RANDOM:
		LOG_DBG("Generating a new LwM2M PSK");
		r = sys_csrand_get(psk, ATTR_LWM2M_PSK_SIZE);
		if (r == 0) {
			r = attr_set_byte_array(ATTR_ID_lwm2mPsk, psk,
						sizeof(psk));
		}
		break;

	default:
		LOG_DBG("Unhandled PSK operation");
		break;
	}

	if (r != 0) {
		LOG_ERR("Error generating PSK: %d", r);
	}

	return r;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int device_reboot_cb(uint16_t obj_inst_id, uint8_t *args,
			    uint16_t args_len)
{
	ARG_UNUSED(obj_inst_id);
	ARG_UNUSED(args);
	ARG_UNUSED(args_len);

	lcz_software_reset(0);
	return 0;
}

static int device_factory_default_cb(uint16_t obj_inst_id, uint8_t *args,
				     uint16_t args_len)
{
	ARG_UNUSED(obj_inst_id);
	ARG_UNUSED(args);
	ARG_UNUSED(args_len);

	LOG_INF("DEVICE: FACTORY DEFAULT");
	return -1;
}

static void *current_time_read_cb(uint16_t obj_inst_id, uint16_t res_id,
				  uint16_t res_inst_id, size_t *data_len)
{
	/* The device object doesn't allow this to be set because
	 * reads are intercepted */
	ARG_UNUSED(obj_inst_id);
	lwm2m_time = lcz_qrtc_get_epoch();
	*data_len = sizeof(lwm2m_time);
	return &lwm2m_time;
}

static int lwm2m_setup(const char *serial_number, const char *id)
{
	int ret;
	char *server_url;
	uint16_t server_url_len;
	uint8_t server_url_flags;

	/* setup SECURITY object */

	/* Server URL */
	ret = lwm2m_engine_get_res_data("0/0/0", (void **)&server_url,
					&server_url_len, &server_url_flags);
	if (ret < 0) {
		return ret;
	}

	snprintk(server_url, server_url_len, "coap%s//%s",
		 IS_ENABLED(CONFIG_LWM2M_DTLS_SUPPORT) ? "s:" : ":",
		 server_addr);
	LOG_WRN("Server URL: %s", log_strdup(server_url));

	/* Security Mode */
	lwm2m_engine_set_u8("0/0/2",
			    IS_ENABLED(CONFIG_LWM2M_DTLS_SUPPORT) ? 0 : 3);
#if defined(CONFIG_LWM2M_DTLS_SUPPORT)
	lwm2m_engine_set_string(
		"0/0/3", (char *)attr_get_quasi_static(ATTR_ID_lwm2mClientId));
	lwm2m_engine_set_opaque("0/0/5",
				attr_get_quasi_static(ATTR_ID_lwm2mPsk),
				attr_get_size(ATTR_ID_lwm2mPsk));
#endif /* CONFIG_LWM2M_DTLS_SUPPORT */

#if defined(CONFIG_LWM2M_RD_CLIENT_SUPPORT_BOOTSTRAP)
	/* Mark 1st instance of security object as a bootstrap server */
	lwm2m_engine_set_u8("0/0/1", 1);

	/* Create 2nd instance of security object needed for bootstrap */
	lwm2m_engine_create_obj_inst("0/1");
#else
	/* Match Security object instance with a Server object instance with
	 * Short Server ID.
	 */
	lwm2m_engine_set_u16("0/0/10", 101);
	lwm2m_engine_set_u16("1/0/0", 101);
#endif

	/* setup SERVER object */

	/* setup DEVICE object */
	char *s;
	s = (char *)dis_get_manufacturer_name();
	lwm2m_engine_set_res_data("3/0/0", s, lwm2m_str_size(s),
				  LWM2M_RES_DATA_FLAG_RO);
	s = (char *)dis_get_model_number();
	lwm2m_engine_set_res_data("3/0/1", s, lwm2m_str_size(s),
				  LWM2M_RES_DATA_FLAG_RO);
	s = (char *)dis_get_software_revision();
	lwm2m_engine_set_res_data("3/0/2", (char *)serial_number,
				  lwm2m_str_size(serial_number),
				  LWM2M_RES_DATA_FLAG_RO);
	lwm2m_engine_set_res_data("3/0/3", s, lwm2m_str_size(s),
				  LWM2M_RES_DATA_FLAG_RO);
	lwm2m_engine_register_exec_callback("3/0/4", device_reboot_cb);
	lwm2m_engine_register_exec_callback("3/0/5", device_factory_default_cb);
	lwm2m_engine_register_read_callback("3/0/13", current_time_read_cb);

	/* IPSO: Light Control object */
	lwm2m_engine_create_obj_inst("3311/0");
	lwm2m_engine_register_post_write_callback("3311/0/5850", led_on_off_cb);

	/* setup objects for remote sensors */
	create_bl654_sensor_objects();

	return 0;
}

static void rd_client_event(struct lwm2m_ctx *client,
			    enum lwm2m_rd_client_event client_event)
{
	switch (client_event) {
	case LWM2M_RD_CLIENT_EVENT_NONE:
		/* do nothing */
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_FAILURE:
		LOG_DBG("Bootstrap registration failure!");
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_COMPLETE:
		LOG_DBG("Bootstrap registration complete");
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_TRANSFER_COMPLETE:
		LOG_DBG("Bootstrap transfer complete");
		break;

	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
		LOG_DBG("Registration failure!");
		break;

	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE:
		LOG_DBG("Registration complete");
		break;

	case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_FAILURE:
		LOG_DBG("Registration update failure!");
		break;

	case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE:
		LOG_DBG("Registration update complete");
		break;

	case LWM2M_RD_CLIENT_EVENT_DEREGISTER_FAILURE:
		LOG_DBG("Deregister failure!");
		break;

	case LWM2M_RD_CLIENT_EVENT_DISCONNECT:
		LOG_DBG("Disconnected");
		break;

	case LWM2M_RD_CLIENT_EVENT_QUEUE_MODE_RX_OFF:
		/* do nothing */
		break;

	case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
		LOG_DBG("Network Error");
		break;
	}
}

static int resolve_server_address(void)
{
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
	};

	static struct addrinfo *result;
	int ret = dns_resolve_server_addr(
		attr_get_quasi_static(ATTR_ID_lwm2mPeerUrl), NULL, &hints,
		&result);
	if (ret == 0) {
		ret = dns_build_addr_string(server_addr, result);
	}
	freeaddrinfo(result);
	return ret;
}

static void lwm2m_client_init_internal(void)
{
	uint32_t flags;
	int ret;

	if (lwm2m_initialized) {
		return;
	}

	flags = IS_ENABLED(CONFIG_LWM2M_RD_CLIENT_SUPPORT_BOOTSTRAP) ?
			      LWM2M_RD_CLIENT_FLAG_BOOTSTRAP :
			      0;

	ret = resolve_server_address();
	if (ret < 0) {
		return;
	}

	ret = lwm2m_setup(
#ifdef CONFIG_MODEM_HL7800
			  attr_get_quasi_static(ATTR_ID_lteSerialNumber),
#else
			  /* For non-Pinnacle 100 devices which do not contain
			   * modems, use the gateway ID as the serial number
			   */
			  attr_get_quasi_static(ATTR_ID_gatewayId),
#endif
			  attr_get_quasi_static(ATTR_ID_gatewayId));
	if (ret < 0) {
		LOG_ERR("Cannot setup LWM2M fields (%d)", ret);
		return;
	}

	(void)memset(&client, 0x0, sizeof(client));
#if defined(CONFIG_LWM2M_DTLS_SUPPORT)
	client.tls_tag = TLS_TAG;
#endif

	/* client.sec_obj_inst is 0 as a starting point */
	char endpoint_name[CONFIG_LWM2M_CLIENT_ENDPOINT_MAX_SIZE];
	memset(endpoint_name, 0, sizeof(endpoint_name));
	snprintk(endpoint_name, CONFIG_LWM2M_CLIENT_ENDPOINT_MAX_SIZE, "%s_%s",
		 dis_get_model_number(),
		 (char *)attr_get_quasi_static(ATTR_ID_gatewayId));
	LOG_DBG("Endpoint name: %s", log_strdup(endpoint_name));
	lwm2m_rd_client_start(&client, endpoint_name, flags, rd_client_event);

	lwm2m_initialized = true;
}

static int led_on_off_cb(uint16_t obj_inst_id, uint16_t res_id,
			 uint16_t res_inst_id, uint8_t *data, uint16_t data_len,
			 bool last_block, size_t total_size)
{
	uint8_t led_val = *(uint8_t *)data;
	if (led_val != led_state) {
		if (led_val) {
			lcz_led_turn_on(CLOUD_LED);
		} else {
			lcz_led_turn_off(CLOUD_LED);
		}
		led_state = led_val;
		/* reset time on counter */
		lwm2m_engine_set_s32("3311/0/5852", 0);
	}

	return 0;
}

static void create_bl654_sensor_objects(void)
{
	/* The BL654 Sensor contains a BME 280. */
	/* 5603 and 5604 are the range of values supported by sensor. */
	struct float32_value float_value;
#ifdef CONFIG_LWM2M_IPSO_TEMP_SENSOR
	lwm2m_engine_create_obj_inst("3303/0");
	lwm2m_engine_set_string("3303/0/5701", "C");
	float_value.val1 = -40;
	lwm2m_engine_set_float32("3303/0/5603", &float_value);
	float_value.val1 = 85;
	lwm2m_engine_set_float32("3303/0/5604", &float_value);
#endif

#ifdef CONFIG_LWM2M_IPSO_GENERIC_SENSOR
	/* temperature used for test */
	lwm2m_engine_create_obj_inst("3303/0");
	lwm2m_engine_set_string("3303/0/5701", "C");
	float_value.val1 = -40;
	lwm2m_engine_set_float32("3303/0/5603", &float_value);
	float_value.val1 = 85;
	lwm2m_engine_set_float32("3303/0/5604", &float_value);
#endif

#ifdef CONFIG_LWM2M_IPSO_HUMIDITY_SENSOR
	lwm2m_engine_create_obj_inst("3304/0");
	lwm2m_engine_set_string("3304/0/5701", "%");
	float_value.val1 = 0;
	lwm2m_engine_set_float32("3304/0/5603", &float_value);
	float_value.val1 = 100;
	lwm2m_engine_set_float32("3304/0/5604", &float_value);
#endif

#ifdef CONFIG_LWM2M_IPSO_PRESSURE_SENSOR
	lwm2m_engine_create_obj_inst("3323/0");
	lwm2m_engine_set_string("3323/0/5701", "Pa");
	float_value.val1 = 300;
	lwm2m_engine_set_float32("3323/0/5603", &float_value);
	float_value.val1 = 1100000;
	lwm2m_engine_set_float32("3323/0/5604", &float_value);
#endif
}

static struct float32_value make_float_value(float v)
{
	struct float32_value f;

	f.val1 = (int32_t)v;
	f.val2 = (int32_t)(LWM2M_FLOAT32_DEC_MAX * (v - f.val1));

	return f;
}

static size_t lwm2m_str_size(const char *s)
{
	return strlen(s) + 1;
}
