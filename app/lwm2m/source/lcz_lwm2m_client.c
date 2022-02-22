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
#include "lwm2m_resource_ids.h"
#include "lwm2m_obj_gateway.h"

#include <string.h>
#include <stddef.h>
#include <random/rand32.h>

#include "lcz_dns.h"
#include "led_configuration.h"
#include "dis.h"
#include "lcz_qrtc.h"
#include "lcz_software_reset.h"
#include "attr.h"
#include "file_system_utilities.h"
#include "lcz_lwm2m_sensor.h"
#include "lcz_lwm2m_client.h"
#include "lcz_lwm2m_fw_update.h"
#include "lcz_lwm2m_conn_mon.h"
#ifdef CONFIG_LWM2M_UCIFI_BATTERY
#include "lcz_lwm2m_battery.h"
#endif

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#if !defined(CONFIG_NET_IPV6) && !defined(CONFIG_NET_IPV4)
#error LwM2M requires either IPV6 or IPV4 support
#endif

#if defined(CONFIG_LWM2M_DTLS_SUPPORT)
#define TLS_TAG CONFIG_LWM2M_PSK_TAG
#endif /* CONFIG_LWM2M_DTLS_SUPPORT */

enum create_state { CREATE_ALLOW = 0, CREATE_OK = 1, CREATE_FAIL = 2 };

#define ESS_SENSOR_TEMPERATURE_PATH                                            \
	STRINGIFY(IPSO_OBJECT_TEMP_SENSOR_ID)                                  \
	"/" STRINGIFY(LWM2M_INSTANCE_ESS_SENSOR) "/" STRINGIFY(                \
		SENSOR_VALUE_RID)

#define ESS_SENSOR_GENERIC_PATH                                                \
	STRINGIFY(IPSO_OBJECT_TEMP_SENSOR_ID)                                  \
	"/" STRINGIFY(LWM2M_INSTANCE_TEST) "/" STRINGIFY(SENSOR_VALUE_RID)

#define ESS_SENSOR_HUMIDITY_PATH                                               \
	STRINGIFY(IPSO_OBJECT_HUMIDITY_SENSOR_ID)                              \
	"/" STRINGIFY(LWM2M_INSTANCE_ESS_SENSOR) "/" STRINGIFY(                \
		SENSOR_VALUE_RID)

#define ESS_SENSOR_PRESSURE_PATH                                               \
	STRINGIFY(IPSO_OBJECT_PRESSURE_ID)                                     \
	"/" STRINGIFY(LWM2M_INSTANCE_ESS_SENSOR) "/" STRINGIFY(                \
		SENSOR_VALUE_RID)

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
		enum create_state ess_sensor;
		enum create_state board_temperature;
		enum create_state board_battery;
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
static int create_ess_sensor_objects(void);
static size_t lwm2m_str_size(const char *s);
static int ess_sensor_set_error_handler(int status, char *obj_inst_str);
static bool enable_bootstrap(void);

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
		return lwm2m_setup(attr_get_quasi_static(ATTR_ID_gateway_id));
	} else {
		return 0;
	}
}

int lwm2m_create_sensor_obj(struct lwm2m_sensor_obj_cfg *cfg)
{
	int r = -EAGAIN;
	char path[CONFIG_LWM2M_PATH_MAX_SIZE];

	if (lw.setup_complete) {
		snprintk(path, sizeof(path), "%u/%u", cfg->type, cfg->instance);
		r = lwm2m_engine_create_obj_inst(path);
		if (r < 0) {
			return r;
		}

		LWM2M_CLIENT_REREGISTER;

		if (!cfg->skip_secondary) {
			snprintk(path, sizeof(path), "%u/%u/5701", cfg->type,
				 cfg->instance);
			lwm2m_engine_set_string(path, cfg->units);

			/* 5603 and 5604 are the range of values supported by sensor. */
			snprintk(path, sizeof(path), "%u/%u/5603", cfg->type,
				 cfg->instance);
			lwm2m_engine_set_float(path, &cfg->min);

			snprintk(path, sizeof(path), "%u/%u/5604", cfg->type,
				 cfg->instance);
			lwm2m_engine_set_float(path, &cfg->max);
		}
	}

	return r;
}

int lwm2m_set_ess_sensor_data(float temperature, float humidity,
				float pressure)
{
	int result = 0;
	double d;

	/* Don't keep trying to create objects after a failure */
	if (lw.cs.ess_sensor == CREATE_ALLOW) {
		result = create_ess_sensor_objects();
		if (result == 0) {
			lw.cs.ess_sensor = CREATE_OK;
		} else if (result != -EAGAIN) {
			lw.cs.ess_sensor = CREATE_FAIL;
		}
	}

	if (lw.cs.ess_sensor != CREATE_OK) {
		return -EPERM;
	}

#ifdef CONFIG_LWM2M_IPSO_TEMP_SENSOR
	d = (double)temperature;
	result = lwm2m_engine_set_float(ESS_SENSOR_TEMPERATURE_PATH, &d);
	if (result < 0) {
		return ess_sensor_set_error_handler(
			result, ESS_SENSOR_TEMPERATURE_PATH);
	}
#endif

#ifdef CONFIG_LWM2M_IPSO_GENERIC_SENSOR
	/* Temperature is used to test generic sensor */
	d = (double)temperature;
	result = lwm2m_engine_set_float(ESS_SENSOR_GENERIC_PATH, &d);
	if (result < 0) {
		return ess_sensor_set_error_handler(
			result, ESS_SENSOR_GENERIC_PATH);
	}
#endif

#ifdef CONFIG_LWM2M_IPSO_HUMIDITY_SENSOR
	d = (double)humidity;
	result = lwm2m_engine_set_float(ESS_SENSOR_HUMIDITY_PATH, &d);
	if (result < 0) {
		return ess_sensor_set_error_handler(
			result, ESS_SENSOR_HUMIDITY_PATH);
	}
#endif

#ifdef CONFIG_LWM2M_IPSO_PRESSURE_SENSOR
	d = (double)pressure;
	result = lwm2m_engine_set_float(ESS_SENSOR_PRESSURE_PATH, &d);
	if (result < 0) {
		return ess_sensor_set_error_handler(
			result, ESS_SENSOR_PRESSURE_PATH);
	}
#endif

	return result;
}

#if defined(CONFIG_BOARD_MG100) && defined(CONFIG_LWM2M_IPSO_TEMP_SENSOR) &&   \
	defined(CONFIG_LCZ_MOTION_TEMPERATURE)

int lwm2m_set_board_temperature(double *temperature)
{
	int result = 0;
	struct lwm2m_sensor_obj_cfg cfg = {
		.instance = LWM2M_INSTANCE_BOARD,
		.type = IPSO_OBJECT_TEMP_SENSOR_ID,
		.skip_secondary = false,
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
		result = lwm2m_engine_set_float("3303/0/5700", temperature);
	}

	if (result == -ENOENT) {
		/* The object can be deleted from the cloud */
		lw.cs.board_temperature = CREATE_ALLOW;
		LOG_WRN("Board temperature obj appears to have been deleted");
	} else if (result < 0) {
		LOG_ERR("Unable to set board temperature %d", result);
	}
	return result;
}
#endif

#if defined(CONFIG_BOARD_MG100) && defined(CONFIG_LWM2M_UCIFI_BATTERY)
int lwm2m_set_board_battery(double *voltage, uint8_t level)
{
	int result = 0;
	struct lwm2m_battery_obj_cfg cfg = {
		.instance = LWM2M_INSTANCE_BOARD,
		.level = 0,
		.voltage = 0,
	};

	if (lw.cs.board_battery == CREATE_ALLOW) {
		result = lcz_lwm2m_battery_create(&cfg);
		if (result == 0) {
			lw.cs.board_battery = CREATE_OK;
		} else if (result != -EAGAIN) {
			lw.cs.board_temperature = CREATE_FAIL;
		}
	}

	if (lw.cs.board_battery != CREATE_OK) {
		return -EPERM;
	}

	if (result == 0) {
		result = lcz_lwm2m_battery_level_set(LWM2M_INSTANCE_BOARD,
						     level);

		if (result == -ENOENT) {
			/* The object can be deleted from the cloud */
			lw.cs.board_battery = CREATE_ALLOW;
		}

		result = lcz_lwm2m_battery_voltage_set(LWM2M_INSTANCE_BOARD,
						       voltage);

		if (result == -ENOENT) {
			/* The object can be deleted from the cloud */
			lw.cs.board_battery = CREATE_ALLOW;
		}
	}

	return result;
}
#endif

int lwm2m_generate_psk(void)
{
	int r = -EPERM;
	uint8_t psk[ATTR_LWM2M_PSK_SIZE];
	uint8_t cmd = attr_get_uint32(ATTR_ID_generate_psk,
				      GENERATE_PSK_LWM2M_DEFAULT);

	switch (cmd) {
	case GENERATE_PSK_LWM2M_DEFAULT:
		LOG_DBG("Setting PSK to default");
		r = attr_default(ATTR_ID_lwm2m_psk);

		break;

	case GENERATE_PSK_LWM2M_RANDOM:
		LOG_DBG("Generating a new LwM2M PSK");
		r = sys_csrand_get(psk, ATTR_LWM2M_PSK_SIZE);
		if (r == 0) {
			r = attr_set_byte_array(ATTR_ID_lwm2m_psk, psk,
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
		flags = enable_bootstrap() ? LWM2M_RD_CLIENT_FLAG_BOOTSTRAP : 0;

		(void)memset(&lw.client, 0, sizeof(lw.client));
#if defined(CONFIG_LWM2M_DTLS_SUPPORT)
		lw.client.tls_tag = TLS_TAG;
#endif

		lwm2m_rd_client_start(
			&lw.client,
			attr_get_quasi_static(ATTR_ID_lwm2m_client_id), flags,
			rd_client_event, NULL);
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

int lwm2m_disconnect_and_deregister(void)
{
	lwm2m_rd_client_stop(&lw.client, rd_client_event, true);
	lw.connection_started = false;
	lw.connected = false;

	return 0;
}

int lwm2m_load(uint16_t type, uint16_t instance, uint16_t resource,
	       uint16_t data_len)
{
	int r = -EPERM;
	char path[CONFIG_LWM2M_PATH_MAX_SIZE];
	char fname[LWM2M_CFG_FILE_NAME_MAX_SIZE];
	char data[CONFIG_LCZ_LWM2M_MAX_LOAD_SIZE];

	if (data_len == 0) {
		return -EINVAL;
	}

	if (data_len > sizeof(data)) {
		LOG_ERR("Unsupported size");
		return -ENOMEM;
	}

	/* Use path as file name.
	 * For example, "3435.62812.1" is for filling sensor
	 * instance 62812 and resource container height.
	 */
	snprintk(path, sizeof(path), "%u/%u/%u", type, instance, resource);
	snprintk(fname, sizeof(fname), CONFIG_FSU_MOUNT_POINT "/%u.%u.%u", type,
		 instance, resource);
	r = (int)fsu_read_abs(fname, data, data_len);
	if (r < 0) {
		LOG_ERR("Unable to load %s", log_strdup(fname));
		return r;
	}

	r = lwm2m_engine_set_opaque(path, data, data_len);
	if (r < 0) {
		LOG_ERR("Unable to set %s", path);
		return r;
	}

	return r;
}

int lwm2m_save(uint16_t type, uint16_t instance, uint16_t resource,
	       uint8_t *data, uint16_t data_len)
{
	char fname[LWM2M_CFG_FILE_NAME_MAX_SIZE];
	int r = -EPERM;

	if (data == NULL) {
		r = -EIO;
	} else if (data_len == 0) {
		r = -EINVAL;
	} else if (fsu_lfs_mount() == 0) {
		snprintk(fname, sizeof(fname),
			 CONFIG_FSU_MOUNT_POINT "/%u.%u.%u", type, instance,
			 resource);

		r = (int)fsu_write_abs(fname, data, data_len);
	}

	LOG_INF("Config save for %s status: %d", log_strdup(fname), r);

	return r;
}

int lwm2m_delete_resource_inst(uint16_t type, uint16_t instance,
			       uint16_t resource, uint16_t resource_inst)
{
	char path[LWM2M_CFG_FILE_NAME_MAX_SIZE];

	snprintk(path, sizeof(path), "/%u/%u/%u/%u", type, instance, resource,
		 resource_inst);

	return lwm2m_engine_delete_res_inst(path);
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
		 (char *)attr_get_quasi_static(ATTR_ID_lwm2m_peer_url));
	LOG_INF("Server URL: %s", log_strdup(server_url));

	/* Security Mode */
	lwm2m_engine_set_u8("0/0/2",
			    IS_ENABLED(CONFIG_LWM2M_DTLS_SUPPORT) ? 0 : 3);
#if defined(CONFIG_LWM2M_DTLS_SUPPORT)
	lwm2m_engine_set_string(
		"0/0/3", (char *)attr_get_quasi_static(ATTR_ID_lwm2m_client_id));
	lwm2m_engine_set_opaque("0/0/5",
				attr_get_quasi_static(ATTR_ID_lwm2m_psk),
				attr_get_size(ATTR_ID_lwm2m_psk));
#endif /* CONFIG_LWM2M_DTLS_SUPPORT */

	if (enable_bootstrap()) {
		/* Mark 1st instance of security object as a bootstrap server */
		lwm2m_engine_set_u8("0/0/1", 1);

		/* Create 2nd instance of security object needed for bootstrap */
		lwm2m_engine_create_obj_inst("0/1");
	} else {
		/* Match Security object instance with a Server object instance with
	 	 * Short Server ID.
	 	 */
		lwm2m_engine_set_u16("0/0/10", 101);
		lwm2m_engine_set_u16("1/0/0", 101);
	}

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
	/* State of LED is not saved/restored */
	lwm2m_engine_register_post_write_callback("3311/0/5850", led_on_off_cb);
	/* Delete unused optional resources */
	lwm2m_engine_delete_res_inst("3311/0/5851/0");
	lwm2m_engine_delete_res_inst("3311/0/5805/0");
	lwm2m_engine_delete_res_inst("3311/0/5820/0");
	lwm2m_engine_delete_res_inst("3311/0/5706/0");
	lwm2m_engine_delete_res_inst("3311/0/5701/0");
	lwm2m_engine_delete_res_inst("3311/0/5750/0");

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
		lwm2m_disconnect();
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
		lwm2m_disconnect();
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

static int create_ess_sensor_objects(void)
{
	/* The BL654 Sensor contains a BME 280. */
	int r = -EPERM;
	struct lwm2m_sensor_obj_cfg cfg;

#ifdef CONFIG_LWM2M_IPSO_TEMP_SENSOR
	cfg.type = IPSO_OBJECT_TEMP_SENSOR_ID;
	cfg.instance = LWM2M_INSTANCE_ESS_SENSOR;
	cfg.skip_secondary = false;
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
	cfg.instance = LWM2M_INSTANCE_ESS_SENSOR;
	cfg.skip_secondary = false;
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
	cfg.instance = LWM2M_INSTANCE_ESS_SENSOR;
	cfg.skip_secondary = false;
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
	cfg.instance = LWM2M_INSTANCE_ESS_SENSOR;
	cfg.skip_secondary = false;
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

static size_t lwm2m_str_size(const char *s)
{
	return strlen(s) + 1;
}

static int ess_sensor_set_error_handler(int status, char *obj_inst_str)
{
	if (status == -ENOENT) {
		LOG_DBG("Object deletion by client not supported for ESS Sensor: %s",
			obj_inst_str);
	} else {
		LOG_ERR("Unable to set %s: %d", obj_inst_str, status);
	}

	return status;
}

static bool enable_bootstrap(void)
{
	if (IS_ENABLED(CONFIG_LWM2M_RD_CLIENT_SUPPORT_BOOTSTRAP) &&
	    attr_get_uint32(ATTR_ID_lwm2m_enable_bootstrap, false)) {
		return true;
	} else {
		return false;
	}
}
