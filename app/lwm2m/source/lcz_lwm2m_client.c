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
LOG_MODULE_REGISTER(lwm2m_client, CONFIG_LCZ_LWM2M_LOG_LEVEL);

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
#include "lcz_lwm2m_fw_update.h"
#include "lcz_lwm2m_conn_mon.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#if !defined(CONFIG_NET_IPV6) && !defined(CONFIG_NET_IPV4)
#error LwM2M requires either IPV6 or IPV4 support
#endif

#if defined(CONFIG_LWM2M_DTLS_SUPPORT)
#define TLS_TAG CONFIG_LWM2M_PSK_TAG
#endif /* CONFIG_LWM2M_DTLS_SUPPORT */

/* /65535/65535/65535 */
#define PATH_STR_MAX_SIZE 19

enum create_state { CREATE_ALLOW = 0, CREATE_OK = 1, CREATE_FAIL = 2 };

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct {
	uint8_t led_state;
	uint32_t time;
	struct lwm2m_ctx client;
	bool dns_resolved;
	bool connection_started;
	bool connected;
	bool setup_complete;
	struct {
		enum create_state bl654_sensor;
		enum create_state board_temperature;
	} cs;
} lw;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int device_reboot_cb(uint16_t obj_inst_id, uint8_t *args,
			    uint16_t args_len);
static int device_factory_default_cb(uint16_t obj_inst_id, uint8_t *args,
				     uint16_t args_len);
static int lwm2m_setup(const char *id);
static void rd_client_event(struct lwm2m_ctx *client,
			    enum lwm2m_rd_client_event client_event);
static int led_on_off_cb(uint16_t obj_inst_id, uint16_t res_id,
			 uint16_t res_inst_id, uint8_t *data, uint16_t data_len,
			 bool last_block, size_t total_size);
static int create_bl654_sensor_objects(void);
static struct float32_value make_float_value(float v);
static size_t lwm2m_str_size(const char *s);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void client_acknowledge(void)
{
	lwm2m_acknowledge(&lw.client);
}

int lwm2m_client_init(void)
{
	if (!lw.setup_complete) {
		return lwm2m_setup(attr_get_quasi_static(ATTR_ID_gatewayId));
	} else {
		return 0;
	}
}

int lwm2m_set_sensor_data(uint16_t type, uint16_t instance, float value)
{
	char path[PATH_STR_MAX_SIZE];
	struct float32_value float_value = make_float_value(value);

	snprintk(path, sizeof(path), "%u/%u/5700", type, instance);

	return lwm2m_engine_set_float32(path, &float_value);
}

int lwm2m_create_sensor_obj(struct lwm2m_sensor_obj_cfg *cfg)
{
	int r = -EAGAIN;
	struct float32_value float_value;
	char path[PATH_STR_MAX_SIZE];

	if (lw.setup_complete) {
		snprintk(path, sizeof(path), "%u/%u", cfg->type, cfg->instance);
		r = lwm2m_engine_create_obj_inst(path);
		if (r < 0) {
			return r;
		}

		snprintk(path, sizeof(path), "%u/%u/5701", cfg->type,
			 cfg->instance);
		lwm2m_engine_set_string(path, cfg->units);

		/* 5603 and 5604 are the range of values supported by sensor. */
		snprintk(path, sizeof(path), "%u/%u/5603", cfg->type,
			 cfg->instance);
		float_value = make_float_value(cfg->min);
		lwm2m_engine_set_float32(path, &float_value);

		snprintk(path, sizeof(path), "%u/%u/5604", cfg->type,
			 cfg->instance);
		float_value = make_float_value(cfg->max);
		lwm2m_engine_set_float32(path, &float_value);
	}

	return r;
}

int lwm2m_set_bl654_sensor_data(float temperature, float humidity,
				float pressure)
{
	int result = 0;
	struct float32_value float_value;

	/* Don't keep trying to create objects after a failure */
	if (lw.cs.bl654_sensor == CREATE_ALLOW) {
		if (create_bl654_sensor_objects() == 0) {
			lw.cs.bl654_sensor = CREATE_OK;
		} else if (result != -EAGAIN) {
			lw.cs.bl654_sensor = CREATE_FAIL;
		}
	}

	if (lw.cs.bl654_sensor != CREATE_OK) {
		return -EPERM;
	}

#ifdef CONFIG_LWM2M_IPSO_TEMP_SENSOR
	float_value = make_float_value(temperature);
	result = lwm2m_engine_set_float32("3303/1/5700", &float_value);
	if (result < 0) {
		return result;
	}
#endif

	/* Temperature is used to test generic sensor */
#ifdef CONFIG_LWM2M_IPSO_GENERIC_SENSOR
	float_value = make_float_value(temperature);
	result = lwm2m_engine_set_float32("3303/2/5700", &float_value);
	if (result < 0) {
		return result;
	}
#endif

#ifdef CONFIG_LWM2M_IPSO_HUMIDITY_SENSOR
	float_value = make_float_value(humidity);
	result = lwm2m_engine_set_float32("3304/1/5700", &float_value);
	if (result < 0) {
		return result;
	}
#endif

#ifdef CONFIG_LWM2M_IPSO_PRESSURE_SENSOR
	float_value = make_float_value(pressure);
	result = lwm2m_engine_set_float32("3323/1/5700", &float_value);
	if (result < 0) {
		return result;
	}
#endif

	return result;
}

#if defined(CONFIG_BOARD_MG100) && defined(CONFIG_LWM2M_IPSO_TEMP_SENSOR) &&   \
	defined(CONFIG_LCZ_MOTION_TEMPERATURE)

int lwm2m_set_temperature(float temperature)
{
	int result = 0;
	struct float32_value float_value;
	struct lwm2m_sensor_obj_cfg cfg = {
		.instance = LWM2M_INSTANCE_BOARD,
		.type = IPSO_OBJECT_TEMP_SENSOR_ID,
		.units = LWM2M_TEMPERATURE_UNITS,
		.min = LWM2M_TEMPERATURE_MIN,
		.max = LWM2M_TEMPERATURE_MAX,
	};

	if (lw.cs.board_temperature == CREATE_ALLOW) {
		result = lwm2m_create_sensor_obj(&cfg);
		if (result == 0) {
			lw.cs.board_temperature = CREATE_OK;
		} else if (result != -EAGAIN) {
			lw.cs.board_temperature = CREATE_FAIL;
		}
	}

	if (lw.cs.board_temperature != CREATE_OK) {
		return -EPERM;
	}

	if (result == 0) {
		float_value = make_float_value(temperature);
		result = lwm2m_engine_set_float32("3303/0/5700", &float_value);
	}

	if (result < 0) {
		LOG_ERR("Unable to set board temperature");
	}
	return result;
}

#endif

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

bool lwm2m_connected(void)
{
	return lw.connected;
}

int lwm2m_connect(void)
{
	uint32_t flags;

	if (!lw.connection_started) {
		flags = IS_ENABLED(CONFIG_LWM2M_RD_CLIENT_SUPPORT_BOOTSTRAP) ?
				      LWM2M_RD_CLIENT_FLAG_BOOTSTRAP :
				      0;

		(void)memset(&lw.client, 0, sizeof(lw.client));
#if defined(CONFIG_LWM2M_DTLS_SUPPORT)
		lw.client.tls_tag = TLS_TAG;
#endif

		lwm2m_rd_client_start(
			&lw.client,
			attr_get_quasi_static(ATTR_ID_lwm2mClientId), flags,
			rd_client_event);
		lw.connection_started = true;
	}

	return 0;
}

int lwm2m_disconnect(void)
{
	lwm2m_rd_client_stop(&lw.client, rd_client_event, false);
	lw.connection_started = false;
	lw.connected = false;

	return 0;
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
	 * reads are intercepted
	 */
	ARG_UNUSED(obj_inst_id);
	lw.time = lcz_qrtc_get_epoch();
	*data_len = sizeof(lw.time);
	return &lw.time;
}

static int lwm2m_setup(const char *id)
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
		 (char *)attr_get_quasi_static(ATTR_ID_lwm2mPeerUrl));
	LOG_INF("Server URL: %s", log_strdup(server_url));

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
	lwm2m_engine_set_res_data("3/0/2", (char *)id, lwm2m_str_size(id),
				  LWM2M_RES_DATA_FLAG_RO);
	lwm2m_engine_set_res_data("3/0/3", s, lwm2m_str_size(s),
				  LWM2M_RES_DATA_FLAG_RO);
	lwm2m_engine_register_exec_callback("3/0/4", device_reboot_cb);
	lwm2m_engine_register_exec_callback("3/0/5", device_factory_default_cb);
	lwm2m_engine_register_read_callback("3/0/13", current_time_read_cb);

	/* IPSO: Light Control object */
	lwm2m_engine_create_obj_inst("3311/0");
	lwm2m_engine_register_post_write_callback("3311/0/5850", led_on_off_cb);

#ifdef CONFIG_LCZ_LWM2M_SENSOR
	lcz_lwm2m_sensor_init();
#endif

#if defined(CONFIG_LCZ_LWM2M_FW_UPDATE)
	lcz_lwm2m_fw_update_init();
#endif

#if defined(CONFIG_LWM2M_CONN_MON_OBJ_SUPPORT)
	lcz_lwm2m_conn_mon_update_values();
#endif

	lw.setup_complete = true;
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
		lw.connected = false;
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_COMPLETE:
		LOG_DBG("Bootstrap registration complete");
		lw.connected = true;
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_TRANSFER_COMPLETE:
		LOG_DBG("Bootstrap transfer complete");
		lw.connected = true;
		break;

	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
		LOG_DBG("Registration failure!");
		lw.connected = false;
		break;

	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE:
		LOG_DBG("Registration complete");
		lw.connected = true;
		break;

	case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_FAILURE:
		LOG_DBG("Registration update failure!");
		lw.connected = false;
		break;

	case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE:
		LOG_DBG("Registration update complete");
		lw.connected = true;
		break;

	case LWM2M_RD_CLIENT_EVENT_DEREGISTER_FAILURE:
		LOG_DBG("Deregister failure!");
		lwm2m_disconnect();
		break;

	case LWM2M_RD_CLIENT_EVENT_DISCONNECT:
		LOG_DBG("Disconnected");
		lw.connected = false;
		break;

	case LWM2M_RD_CLIENT_EVENT_QUEUE_MODE_RX_OFF:
		/* do nothing */
		break;

	case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
		LOG_DBG("Network Error");
		lw.connected = false;
		break;
	}
}

static int led_on_off_cb(uint16_t obj_inst_id, uint16_t res_id,
			 uint16_t res_inst_id, uint8_t *data, uint16_t data_len,
			 bool last_block, size_t total_size)
{
	uint8_t led_val = *(uint8_t *)data;
	if (led_val != lw.led_state) {
		if (led_val) {
			lcz_led_turn_on(CLOUD_LED);
		} else {
			lcz_led_turn_off(CLOUD_LED);
		}
		lw.led_state = led_val;
		/* reset time on counter */
		lwm2m_engine_set_s32("3311/0/5852", 0);
	}

	return 0;
}

static int create_bl654_sensor_objects(void)
{
	/* The BL654 Sensor contains a BME 280. */
	int r;
	struct lwm2m_sensor_obj_cfg cfg;

#ifdef CONFIG_LWM2M_IPSO_TEMP_SENSOR
	cfg.type = IPSO_OBJECT_TEMP_SENSOR_ID;
	cfg.instance = LWM2M_INSTANCE_BL654_SENSOR;
	cfg.units = LWM2M_TEMPERATURE_UNITS;
	cfg.min = LWM2M_TEMPERATURE_MIN;
	cfg.max = LWM2M_TEMPERATURE_MAX;

	r = lwm2m_create_sensor_obj(&cfg);
	if (r < 0) {
		return r;
	}
#endif

#ifdef CONFIG_LWM2M_IPSO_GENERIC_SENSOR
	/* temperature used for test */
	cfg.type = IPSO_OBJECT_TEMP_SENSOR_ID;
	cfg.instance = LWM2M_INSTANCE_BL654_SENSOR;
	cfg.units = LWM2M_TEMPERATURE_UNITS;
	cfg.min = LWM2M_TEMPERATURE_MIN;
	cfg.max = LWM2M_TEMPERATURE_MAX;

	r = lwm2m_create_sensor_obj(&cfg);
	if (r < 0) {
		return r;
	}
#endif

#ifdef CONFIG_LWM2M_IPSO_HUMIDITY_SENSOR
	cfg.type = IPSO_OBJECT_HUMIDITY_SENSOR_ID;
	cfg.instance = LWM2M_INSTANCE_BL654_SENSOR;
	cfg.units = LWM2M_HUMIDITY_UNITS;
	cfg.min = LWM2M_HUMIDITY_MIN;
	cfg.max = LWM2M_HUMIDITY_MAX;

	r = lwm2m_create_sensor_obj(&cfg);
	if (r < 0) {
		return r;
	}
#endif

#ifdef CONFIG_LWM2M_IPSO_PRESSURE_SENSOR
	cfg.type = IPSO_OBJECT_PRESSURE_ID;
	cfg.instance = LWM2M_INSTANCE_BL654_SENSOR;
	cfg.units = LWM2M_PRESSURE_UNITS;
	cfg.min = LWM2M_PRESSURE_MIN;
	cfg.max = LWM2M_PRESSURE_MAX;

	r = lwm2m_create_sensor_obj(&cfg);
	if (r < 0) {
		return r;
	}
#endif

	return r;
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
