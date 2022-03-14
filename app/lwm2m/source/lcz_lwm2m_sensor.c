/**
 * @file lcz_lwm2m_sensor.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lwm2m_sensor, CONFIG_LCZ_LWM2M_SENSOR_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <sys/atomic.h>
#include <net/lwm2m.h>

#include "lwm2m_resource_ids.h"
#include "ipso_filling_sensor.h"

#include "lcz_bt_scan.h"
#include "lcz_sensor_event.h"
#include "lcz_sensor_adv_format.h"
#include "lcz_sensor_adv_match.h"
#include "lcz_lwm2m_client.h"
#include "lcz_lwm2m_gateway.h"
#include "errno_str.h"
#ifdef CONFIG_LWM2M_UCIFI_BATTERY
#include "lcz_lwm2m_battery.h"
#endif

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define LWM2M_INSTANCES_PER_SENSOR_MAX 4
#define MAX_INSTANCES                                                          \
	(CONFIG_LCZ_LWM2M_SENSOR_MAX * LWM2M_INSTANCES_PER_SENSOR_MAX)

/* clang-format off */
#define LWM2M_BT610_TEMPERATURE_UNITS "C"
#define LWM2M_BT610_TEMPERATURE_MIN   -40.0
#define LWM2M_BT610_TEMPERATURE_MAX   125.0

#define LWM2M_BT610_CURRENT_UNITS "A"
#define LWM2M_BT610_CURRENT_MIN   0.0
#define LWM2M_BT610_CURRENT_MAX   500.0

#define LWM2M_BT610_PRESSURE_UNITS "PSI"
#define LWM2M_BT610_PRESSURE_MIN   0
#define LWM2M_BT610_PRESSURE_MAX   1000.0
/* clang-format on */

struct lwm2m_sensor_table {
	bt_addr_t addr;
	uint8_t last_record_type;
	uint16_t last_event_id;
	uint16_t base; /* instance */
	uint16_t product_id;
	char name[SENSOR_NAME_MAX_SIZE];
};

#define CONFIGURATOR(_type, _instance, _units, _min, _max, _skip)              \
	cfg.type = (_type);                                                    \
	cfg.instance = (_instance);                                            \
	cfg.units = (_units);                                                  \
	cfg.min = (_min);                                                      \
	cfg.max = (_max);                                                      \
	cfg.skip_secondary = (_skip)

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct {
	bool initialized;
	int scan_user_id;
	uint32_t ads;
	uint32_t legacy_ads;
	uint32_t rsp_ads;
	uint32_t coded_ads;
	uint32_t accepted_ads;
	uint8_t sensor_count;
	struct lwm2m_sensor_table table[CONFIG_LCZ_LWM2M_SENSOR_MAX];
	bool not_enough_instances;
	bool gen_instance_error;
} ls;

static struct lwm2m_sensor_table *const lst = ls.table;

/* Each BT610 can have multiple sensors.
 * Identical sensors must have different instances.
 * Different sensors connected to the same BT610 can have the same instance.
 *
 * The BT610 can only have 1 ultrasonic sensor.
 * For simplicity, this generates some bits that won't be used.
 * This also preserves the Bluetooth address to instance conversion.
 */
static ATOMIC_DEFINE(temperature_created, MAX_INSTANCES);
static ATOMIC_DEFINE(current_created, MAX_INSTANCES);
static ATOMIC_DEFINE(pressure_created, MAX_INSTANCES);
static ATOMIC_DEFINE(ultrasonic_created, MAX_INSTANCES);

static ATOMIC_DEFINE(ls_gateway_created, CONFIG_LCZ_LWM2M_SENSOR_MAX);
static ATOMIC_DEFINE(product_id_valid, CONFIG_LCZ_LWM2M_SENSOR_MAX);
static ATOMIC_DEFINE(battery_created, CONFIG_LCZ_LWM2M_SENSOR_MAX);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void ad_handler(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
		       struct net_buf_simple *ad);

static bool ad_discard(LczSensorAdEvent_t *p);
static void ad_filter(LczSensorAdEvent_t *p, int8_t rssi);
static void ad_process(LczSensorAdEvent_t *p, uint8_t idx, int8_t rssi);

static int get_index(const bt_addr_t *addr, bool allow_gen);
static int generate_new_base(const bt_addr_t *addr, size_t idx);
static bool valid_base(uint16_t instance);

static int create_sensor_obj(atomic_t *created,
			     struct lwm2m_sensor_obj_cfg *cfg, uint8_t idx,
			     uint8_t offset);

static int create_gateway_obj(uint8_t idx, int8_t rssi);

static void obj_not_found_handler(int status, atomic_t *created, uint8_t idx,
				  uint8_t offset);

static void name_handler(const bt_addr_le_t *addr, struct net_buf_simple *ad);
static void rsp_handler(const bt_addr_le_t *addr, LczSensorRsp_t *p);

#ifdef CONFIG_LWM2M_UCIFI_BATTERY
static uint8_t get_bt610_battery_level(double voltage);
static uint8_t get_bt510_battery_level(double voltage);
static int create_battery_obj(uint8_t idx, uint8_t level, double voltage);
static int lwm2m_set_battery_data(uint16_t instance, uint8_t level,
				  double voltage);
#endif
static int lwm2m_set_sensor_data(uint16_t product_id, uint16_t type,
				 uint16_t instance, float value);
static int set_sensor_data(uint16_t type, uint16_t instance, float value);
static int set_filling_sensor_data(uint16_t type, uint16_t instance,
				   float value);

static void configure_filling_sensor(uint16_t instance);

static int register_post_write_callback(uint16_t type, uint16_t instance,
					uint16_t resource,
					lwm2m_engine_set_data_cb_t cb);
static int fill_sensor_write_cb(uint16_t obj_inst_id, uint16_t res_id,
				uint16_t res_inst_id, uint8_t *data,
				uint16_t data_len, bool last_block,
				size_t total_size);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void lcz_lwm2m_sensor_init(void)
{
	if (!lcz_bt_scan_register(&ls.scan_user_id, ad_handler)) {
		LOG_ERR("LWM2M sensor module failed to register with scan module");
	}

	lcz_bt_scan_start(ls.scan_user_id);
}

/******************************************************************************/
/* Occurs in BT RX Thread context                                             */
/******************************************************************************/
static void ad_handler(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
		       struct net_buf_simple *ad)
{
	AdHandle_t handle = AdFind_Type(
		ad->data, ad->len, BT_DATA_MANUFACTURER_DATA, BT_DATA_INVALID);

	ls.ads += 1;

	if (lcz_sensor_adv_match_1m(&handle)) {
		ls.legacy_ads += 1;
		ad_filter((LczSensorAdEvent_t *)handle.pPayload, rssi);
	}

	if (lcz_sensor_adv_match_rsp(&handle)) {
		ls.rsp_ads += 1;
		rsp_handler(
			addr,
			&(((LczSensorRspWithHeader_t *)handle.pPayload)->rsp));
	}

	if (lcz_sensor_adv_match_coded(&handle)) {
		ls.coded_ads += 1;
		/* The coded phy contains the TLVs of the 1M ad and scan response */
		LczSensorAdCoded_t *coded =
			(LczSensorAdCoded_t *)handle.pPayload;
		ad_filter(&coded->ad, rssi);
		rsp_handler(addr, &coded->rsp);
	}

	name_handler(addr, ad);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void name_handler(const bt_addr_le_t *addr, struct net_buf_simple *ad)
{
	AdHandle_t handle = AdFind_Name(ad->data, ad->len);
	char *name = "?";
	int i;
	int r;

	if (handle.pPayload == NULL) {
		return;
	}

	/* Only process names for devices already in the table */
	i = get_index(&addr->a, false);
	if (i < 0) {
		return;
	}

	/* Don't start processing name until after gateway object has been
	 * created.  Then the name will be updated only if it changes.
	 */
	if (!atomic_test_bit(ls_gateway_created, i)) {
		return;
	}

	name = ls.table[i].name;
	if (strncmp(name, handle.pPayload, MAX(strlen(name), handle.size)) !=
	    0) {
		memset(name, 0, SENSOR_NAME_MAX_SIZE);
		strncpy(name, handle.pPayload,
			MIN(SENSOR_NAME_MAX_STR_LEN, handle.size));
		r = lcz_lwm2m_gateway_id_set(lst[i].base, name);
		LOG_INF("Updating name in table: %s idx: %d inst: %d lwm2m status: %d",
			log_strdup(name), i, lst[i].base, r);
	}
}

/**
 * @brief The scan response is used to determine the sensor type.
 */
static void rsp_handler(const bt_addr_le_t *addr, LczSensorRsp_t *p)
{
	if (p == NULL) {
		return;
	}

	/* Only process responses for devices already in the table because
	 * they have ad event types that can be processed.
	 */
	int i = get_index(&addr->a, false);
	if (i < 0) {
		return;
	}

	ls.table[i].product_id = p->productId;
	atomic_set_bit(product_id_valid, i);
}

static void ad_filter(LczSensorAdEvent_t *p, int8_t rssi)
{
	if (p == NULL) {
		return;
	}

	if (ad_discard(p)) {
		return;
	}

	int i = get_index(&p->addr, true);
	if (i < 0) {
		return;
	}

	/* Filter out duplicate events.
	 * If both devices have just powered-up, don't filter event 0.
	 */
	if (p->id != 0 && p->id == lst[i].last_event_id &&
	    p->recordType == lst[i].last_record_type) {
		return;
	}

	LOG_INF("%s idx: %d base: %u RSSI: %d",
		lcz_sensor_event_get_string(p->recordType), i, lst[i].base,
		rssi);

	lst[i].last_event_id = p->id;
	lst[i].last_record_type = p->recordType;
	ls.accepted_ads += 1;

	ad_process(p, i, rssi);
}

/* Don't create a table entry for a sensor reporting
 * events that aren't going to be processed.
 */
static bool ad_discard(LczSensorAdEvent_t *p)
{
	switch (p->recordType) {
#ifdef CONFIG_LCZ_LWM2M_SENSOR_ALLOW_BT510
	case SENSOR_EVENT_TEMPERATURE:
		return false;
#endif
	case SENSOR_EVENT_BATTERY_GOOD:
	case SENSOR_EVENT_BATTERY_BAD:
	case SENSOR_EVENT_TEMPERATURE_1:
	case SENSOR_EVENT_TEMPERATURE_2:
	case SENSOR_EVENT_TEMPERATURE_3:
	case SENSOR_EVENT_TEMPERATURE_4:
	case SENSOR_EVENT_CURRENT_1:
	case SENSOR_EVENT_CURRENT_2:
	case SENSOR_EVENT_CURRENT_3:
	case SENSOR_EVENT_CURRENT_4:
	case SENSOR_EVENT_PRESSURE_1:
	case SENSOR_EVENT_PRESSURE_2:
	case SENSOR_EVENT_ULTRASONIC_1:
		return false;
	default:
		return true;
	}
}

/* The event type determines the LwM2M object type.
 * The address in advertisement is used to generate instance.
 * Objects are created as advertisements are processed.
 */
static void ad_process(LczSensorAdEvent_t *p, uint8_t idx, int8_t rssi)
{
	int r = 0;
	uint8_t offset = 0;
	float f = p->data.f;
	atomic_t *created = NULL;
	struct lwm2m_sensor_obj_cfg cfg;

	switch (p->recordType) {
#ifdef CONFIG_LWM2M_UCIFI_BATTERY
	case SENSOR_EVENT_BATTERY_GOOD:
	case SENSOR_EVENT_BATTERY_BAD:
		if (atomic_test_bit(product_id_valid, idx)) {
			switch (lst[idx].product_id) {
			case BT510_PRODUCT_ID:
				f = ((float)((uint32_t)p->data.u16)) / 1000.0;
				break;
			case BT6XX_PRODUCT_ID:
				f = ((float)((uint32_t)p->data.s32)) / 1000.0;
				break;
			default:
				r = -ENOTSUP;
				break;
			}
			created = battery_created;
			CONFIGURATOR(UCIFI_OBJECT_BATTERY_ID, lst[idx].base, "",
				     0, 0, true);
		} else {
			r = -ENOTSUP;
		}
		break;
#endif
	case SENSOR_EVENT_TEMPERATURE:
		created = temperature_created;
		f = ((float)((int16_t)p->data.u16)) / 100.0;
		CONFIGURATOR(IPSO_OBJECT_TEMP_SENSOR_ID, lst[idx].base,
			     LWM2M_TEMPERATURE_UNITS, LWM2M_TEMPERATURE_MIN,
			     LWM2M_TEMPERATURE_MAX, false);
		break;
	case SENSOR_EVENT_TEMPERATURE_1:
	case SENSOR_EVENT_TEMPERATURE_2:
	case SENSOR_EVENT_TEMPERATURE_3:
	case SENSOR_EVENT_TEMPERATURE_4:
		created = temperature_created;
		offset = (p->recordType - SENSOR_EVENT_TEMPERATURE_1);
		CONFIGURATOR(IPSO_OBJECT_TEMP_SENSOR_ID, lst[idx].base + offset,
			     LWM2M_BT610_TEMPERATURE_UNITS,
			     LWM2M_BT610_TEMPERATURE_MIN,
			     LWM2M_BT610_TEMPERATURE_MAX, false);
		break;
	case SENSOR_EVENT_CURRENT_1:
	case SENSOR_EVENT_CURRENT_2:
	case SENSOR_EVENT_CURRENT_3:
	case SENSOR_EVENT_CURRENT_4:
		created = current_created;
		offset = (p->recordType - SENSOR_EVENT_CURRENT_1);
		CONFIGURATOR(IPSO_OBJECT_CURRENT_SENSOR_ID,
			     lst[idx].base + offset, LWM2M_BT610_CURRENT_UNITS,
			     LWM2M_BT610_CURRENT_MIN, LWM2M_BT610_CURRENT_MAX,
			     false);
		break;
	case SENSOR_EVENT_PRESSURE_1:
	case SENSOR_EVENT_PRESSURE_2:
		created = pressure_created;
		offset = (p->recordType - SENSOR_EVENT_PRESSURE_1);
		CONFIGURATOR(IPSO_OBJECT_PRESSURE_ID, (lst[idx].base + offset),
			     LWM2M_BT610_PRESSURE_UNITS,
			     LWM2M_BT610_PRESSURE_MIN, LWM2M_BT610_PRESSURE_MAX,
			     false);
		break;
	case SENSOR_EVENT_ULTRASONIC_1:
		created = ultrasonic_created;
		/* Convert from mm (reported) to cm (filling sensor) */
		f /= 10.0;
		/* Units/min/max not used because filling sensor object has
		 * different resources.
		 */
		CONFIGURATOR(IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID, lst[idx].base,
			     "", 0, 0, true);
		break;
	default:
		/* Only some of the events are processed */
		LOG_WRN("Event type not supported");
		r = -ENOTSUP;
		break;
	}

	/* Update the sensor data */
	if (r == 0) {
		r = create_sensor_obj(created, &cfg, idx, offset);

		if (r == 0) {
			r = lwm2m_set_sensor_data(lst[idx].product_id, cfg.type,
						  cfg.instance, f);
			obj_not_found_handler(r, created, idx, offset);
		}

		if (r < 0) {
			LOG_ERR("Unable to set LwM2M sensor data: %d", r);
		}
	}

	/* Update the RSSI in the gateway object */
	if (r == 0) {
		r = create_gateway_obj(idx, rssi);

		if (r == 0) {
			r = lcz_lwm2m_gateway_rssi_set(lst[idx].base, rssi);
		}

		if (r < 0) {
			LOG_ERR("Unable to set LwM2M rssi: %d", r);
		}
	}
}

/* Don't create sensor object instances until data is received.
 *
 * The number of instances of each type of sensor object
 * is limited at compile time.
 */
static int create_sensor_obj(atomic_t *created,
			     struct lwm2m_sensor_obj_cfg *cfg, uint8_t idx,
			     uint8_t offset)
{
	uint32_t index_with_offset =
		(idx * LWM2M_INSTANCES_PER_SENSOR_MAX) + offset;
	int r = 0;

	if (created == NULL) {
		LOG_ERR("Invalid atomic created array");
		return -EIO;
	}

	if (atomic_test_bit(created, index_with_offset)) {
		return 0;
	}

#ifdef CONFIG_LWM2M_UCIFI_BATTERY
	if (cfg->type == UCIFI_OBJECT_BATTERY_ID) {
		r = create_battery_obj(idx, 0, 0);
	} else
#endif
	{
		r = lwm2m_create_sensor_obj(cfg);
	}
	if (r == 0) {
		atomic_set_bit(created, index_with_offset);
		if (cfg->type == IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID) {
			configure_filling_sensor(cfg->instance);
		}
	} else if (r == -EEXIST) {
		r = 0;
		atomic_set_bit(created, index_with_offset);
		LOG_WRN("object already exists");
	} else if (r == -ENOMEM) {
		ls.not_enough_instances = true;
	}

	return r;
}

static void obj_not_found_handler(int status, atomic_t *created, uint8_t idx,
				  uint8_t offset)
{
	uint32_t index_with_offset =
		(idx * LWM2M_INSTANCES_PER_SENSOR_MAX) + offset;

	if (status == -ENOENT) {
		/* Objects can be deleted from cloud */
		LOG_WRN("object not found after creation");
		if (created != NULL) {
			atomic_clear_bit(created, index_with_offset);
		}
	}
}

static int get_index(const bt_addr_t *addr, bool allow_gen)
{
	size_t i;
	for (i = 0; i < ls.sensor_count; i++) {
		if (bt_addr_cmp(addr, &lst[i].addr) == 0) {
			return i;
		}
	}

	if (allow_gen && i < CONFIG_LCZ_LWM2M_SENSOR_MAX) {
		return generate_new_base(addr, i);
	} else {
		return -1;
	}
}

static int generate_new_base(const bt_addr_t *addr, size_t idx)
{
	uint16_t instance = 0;

	/* Instance is limited to 16 bits by LwM2M specification
	 * 0-3 are reserved
	 * Allow up to 4 instances of each sensor per BTx.
	 * The address is used because instance must remain constant.
	 */
	memcpy(&instance, &addr->val, 2);
	instance <<= 2;

	if (valid_base(instance)) {
		bt_addr_copy(&lst[idx].addr, addr);
		lst[idx].base = instance;
		ls.sensor_count += 1;
		return idx;
	} else {
		LOG_ERR("Unable to generate valid instance");
		return -1;
	}
}

/* This cannot prevent a duplicate from assuming the role of another sensor
 * if a new sensor is added when the gateway is disabled.
 */
static bool valid_base(uint16_t instance)
{
	size_t i;

	if (instance < LWM2M_INSTANCE_SENSOR_START) {
		ls.gen_instance_error = true;
		return false;
	}

	/* Don't allow duplicates */
	for (i = 0; i < ls.sensor_count; i++) {
		if (instance == lst[i].base) {
			ls.gen_instance_error = true;
			return false;
		}
	}

	return true;
}

/* Create object if it doesn't exist */
static int create_gateway_obj(uint8_t idx, int8_t rssi)
{
	int r = 0;
	char prefix[CONFIG_LWM2M_GATEWAY_PREFIX_MAX_STR_SIZE];
	struct lwm2m_gateway_obj_cfg cfg = {
		.instance = lst[idx].base,
		.id = lst[idx].name,
		.prefix = prefix,
		.iot_device_objects = NULL,
		.rssi = rssi,
	};

	if (atomic_test_bit(ls_gateway_created, idx)) {
		return r;
	}

#ifdef CONFIG_LCZ_LWM2M_SENSOR_ADD_PREFIX_TO_BDA
	snprintk(prefix, sizeof(prefix), "n-%u-%02X%02X%02X%02X%02X%02X",
		 lst[idx].addr.val[5], lst[idx].addr.val[4],
		 lst[idx].addr.val[3], lst[idx].addr.val[2],
		 lst[idx].addr.val[1], lst[idx].addr.val[0], lst[idx].base);
#else
	snprintk(prefix, sizeof(prefix), "n-%u", lst[idx].base);
#endif

	r = lcz_lwm2m_gateway_create(&cfg);
	if (r == 0) {
		atomic_set_bit(ls_gateway_created, idx);
	} else if (r == -EEXIST) {
		r = 0;
		atomic_set_bit(ls_gateway_created, idx);
		LOG_WRN("gateway object already exists");
	} else if (r == -ENOMEM) {
		ls.not_enough_instances = true;
	}

	return r;
}

#ifdef CONFIG_LWM2M_UCIFI_BATTERY
static uint8_t get_bt610_battery_level(double voltage)
{
	uint8_t level = 0;

	if (voltage >= 3.376) {
		level = 100;
	} else if (3.351 <= voltage && voltage <= 3.375) {
		level = 90;
	} else if (3.326 <= voltage && voltage <= 3.350) {
		level = 80;
	} else if (3.301 <= voltage && voltage <= 3.325) {
		level = 70;
	} else if (3.251 <= voltage && voltage <= 3.300) {
		level = 60;
	} else if (3.201 <= voltage && voltage <= 3.250) {
		level = 50;
	} else if (3.151 <= voltage && voltage <= 3.200) {
		level = 40;
	} else if (3.101 <= voltage && voltage <= 3.150) {
		level = 30;
	} else if (3.001 <= voltage && voltage <= 3.100) {
		level = 20;
	} else if (2.501 <= voltage && voltage <= 3.000) {
		level = 10;
	} else if (voltage <= 2.500) {
		level = 0;
	}

	return level;
}

static uint8_t get_bt510_battery_level(double voltage)
{
	uint8_t level = 0;

	if (voltage >= 3.176) {
		level = 100;
	} else if (3.151 <= voltage && voltage <= 3.175) {
		level = 90;
	} else if (3.126 <= voltage && voltage <= 3.150) {
		level = 80;
	} else if (3.101 <= voltage && voltage <= 3.125) {
		level = 70;
	} else if (3.051 <= voltage && voltage <= 3.100) {
		level = 60;
	} else if (3.001 <= voltage && voltage <= 3.050) {
		level = 50;
	} else if (2.951 <= voltage && voltage <= 3.000) {
		level = 40;
	} else if (2.901 <= voltage && voltage <= 2.950) {
		level = 30;
	} else if (2.851 <= voltage && voltage <= 2.900) {
		level = 20;
	} else if (2.501 <= voltage && voltage <= 2.850) {
		level = 10;
	} else if (voltage <= 2.500) {
		level = 0;
	}

	return level;
}

/* Create battery object if it doesn't exist */
static int create_battery_obj(uint8_t idx, uint8_t level, double voltage)
{
	struct lwm2m_battery_obj_cfg cfg = {
		.instance = lst[idx].base,
		.level = level,
		.voltage = voltage,
	};

	LOG_DBG("Create battery obj %d", cfg.instance);

	return lcz_lwm2m_battery_create(&cfg);
}

static int lwm2m_set_battery_data(uint16_t instance, uint8_t level,
				  double voltage)
{
	int r;

	r = lcz_lwm2m_battery_level_set(instance, level);
	if (r < 0) {
		return r;
	}
	return lcz_lwm2m_battery_voltage_set(instance, &voltage);
}
#endif

static int lwm2m_set_sensor_data(uint16_t product_id, uint16_t type,
				 uint16_t instance, float value)
{
#ifdef CONFIG_LWM2M_UCIFI_BATTERY
	uint8_t batt_level = 0;
#endif
	switch (type) {
	case IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID:
		return set_filling_sensor_data(type, instance, value);
#ifdef CONFIG_LWM2M_UCIFI_BATTERY
	case UCIFI_OBJECT_BATTERY_ID:
		switch (product_id) {
		case BT510_PRODUCT_ID:
			batt_level = get_bt510_battery_level((double)value);
			break;
		case BT6XX_PRODUCT_ID:
			batt_level = get_bt610_battery_level((double)value);
			break;
		default:
			return -ENOTSUP;
		}
		return lwm2m_set_battery_data(instance, batt_level,
					      (double)value);
#endif
	default:
		return set_sensor_data(type, instance, value);
	}
}

static int set_sensor_data(uint16_t type, uint16_t instance, float value)
{
	char path[CONFIG_LWM2M_PATH_MAX_SIZE];
	uint16_t resource = SENSOR_VALUE_RID;
	/* LwM2M uses doubles */
	double d = (double)value;

	snprintk(path, sizeof(path), "%u/%u/%u", type, instance, resource);

	return lwm2m_engine_set_float(path, &d);
}

static int set_filling_sensor_data(uint16_t type, uint16_t instance,
				   float value)
{
	char path[CONFIG_LWM2M_PATH_MAX_SIZE];
	double fill_percent;
	uint16_t resource;
	uint32_t distance = (uint32_t)value;
	uint32_t height = 0;
	uint32_t level;

	/* Read the height so that the fill level can be calculated */
	resource = CONTAINER_HEIGHT_FILLING_SENSOR_RID;
	snprintk(path, sizeof(path), "%u/%u/%u", type, instance, resource);

	if (lwm2m_engine_get_u32(path, &height) != 0) {
		LOG_ERR("Unable to read height");
		return -ENOENT;
	}

	/* Don't allow a negative level (height of substance) to be reported */
	if (distance >= height) {
		level = 0;
	} else {
		level = height - distance;
	}
	fill_percent = (double)(((float)level / (float)height) * 100.0);

	/* The suggested sensor has a minimum range of 50 cm */
	LOG_DBG("height: %u level: %u measured distance: %u percent: %d",
		height, level, distance, (uint32_t)fill_percent);

	/* Write optional resource (don't care if it fails) */
	resource = ACTUAL_FILL_LEVEL_FILLING_SENSOR_RID;
	snprintk(path, sizeof(path), "%u/%u/%u", type, instance, resource);
	lwm2m_engine_set_u32(path, level);

	/* Writing this resource will cause full/empty to be re-evaluated */
	resource = ACTUAL_FILL_PERCENTAGE_FILLING_SENSOR_RID;
	snprintk(path, sizeof(path), "%u/%u/%u", type, instance, resource);
	return lwm2m_engine_set_float(path, &fill_percent);
}

/*
 * Save and load filling sensor config to the file system
 */
static void configure_filling_sensor(uint16_t instance)
{
	const uint16_t OBJ_ID = IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID;

	/* If it exists, restore configuration */
	lwm2m_load(OBJ_ID, instance, CONTAINER_HEIGHT_FILLING_SENSOR_RID,
		   sizeof(uint32_t));
	lwm2m_load(OBJ_ID, instance,
		   HIGH_THRESHOLD_PERCENTAGE_FILLING_SENSOR_RID,
		   sizeof(double));
	lwm2m_load(OBJ_ID, instance,
		   LOW_THRESHOLD_PERCENTAGE_FILLING_SENSOR_RID, sizeof(double));

	/* Callback is used to save config to nv */
	register_post_write_callback(OBJ_ID, instance,
				     CONTAINER_HEIGHT_FILLING_SENSOR_RID,
				     fill_sensor_write_cb);
	register_post_write_callback(
		OBJ_ID, instance, HIGH_THRESHOLD_PERCENTAGE_FILLING_SENSOR_RID,
		fill_sensor_write_cb);
	register_post_write_callback(
		OBJ_ID, instance, LOW_THRESHOLD_PERCENTAGE_FILLING_SENSOR_RID,
		fill_sensor_write_cb);

	/* Delete unused resources so they don't show up in Cumulocity. */
	lwm2m_delete_resource_inst(OBJ_ID, instance,
				   AVERAGE_FILL_SPEED_FILLING_SENSOR_RID, 0);
	lwm2m_delete_resource_inst(OBJ_ID, instance,
				   FORECAST_FULL_DATE_FILLING_SENSOR_RID, 0);
	lwm2m_delete_resource_inst(OBJ_ID, instance,
				   FORECAST_EMPTY_DATE_FILLING_SENSOR_RID, 0);
	lwm2m_delete_resource_inst(OBJ_ID, instance,
				   CONTAINER_OUT_OF_LOCATION_FILLING_SENSOR_RID,
				   0);
	lwm2m_delete_resource_inst(OBJ_ID, instance,
				   CONTAINER_OUT_OF_POSITION_FILLING_SENSOR_RID,
				   0);
}

static int register_post_write_callback(uint16_t type, uint16_t instance,
					uint16_t resource,
					lwm2m_engine_set_data_cb_t cb)
{
	char path[CONFIG_LWM2M_PATH_MAX_SIZE];

	snprintk(path, sizeof(path), "%u/%u/%u", type, instance, resource);

	return lwm2m_engine_register_post_write_callback(path, cb);
}

static int fill_sensor_write_cb(uint16_t obj_inst_id, uint16_t res_id,
				uint16_t res_inst_id, uint8_t *data,
				uint16_t data_len, bool last_block,
				size_t total_size)
{
	lwm2m_save(IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID, obj_inst_id, res_id,
		   data, data_len);

	return 0;
}
