/**
 * @file ble_motion_service.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(ble_motion_svc, CONFIG_BLE_MOTION_SVC_LOG_LEVEL);

#define MOTION_SVC_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define MOTION_SVC_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define MOTION_SVC_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define MOTION_SVC_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>
#include <drivers/sensor.h>

#include "laird_bluetooth.h"
#include "ble_motion_service.h"
#include "nv.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define MOTION_ALARM_ACTIVE 1
#define MOTION_ALARM_INACTIVE 0
#define INACTIVITY_TIMER_PERIOD K_MSEC(30000)

#define MOTION_SVC_BASE_UUID_128(_x_)                                          \
	BT_UUID_INIT_128(0x66, 0x9a, 0x0c, 0x20, 0x00, 0x08, 0x6e, 0x8b, 0xea, \
			 0x11, 0x1a, 0xac, LSB_16(_x_), MSB_16(_x_), 0xce,     \
			 0xad)

static struct bt_uuid_128 MOTION_SVC_UUID = MOTION_SVC_BASE_UUID_128(0x0a30);
static struct bt_uuid_128 MOTION_ALARM_UUID = MOTION_SVC_BASE_UUID_128(0x0a31);

struct ble_motion_service {
	uint16_t motion_alarm_index;
	uint8_t motion_alarm;
};

struct ccc_table {
	struct lbt_ccc_element motion_alarm;
};

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct ble_motion_service bms;
static struct ccc_table ccc;
static struct bt_conn *motion_svc_conn;
static struct k_timer motion_timer;
static struct motion_status motionStatus;

/* This array maps ODR values to real sampling frequency values. To keep the
 * implementation common between various Laird Connectivity products for the
 * device shadow, we present ODR values rather than the real sampling frequency
 * values. However, the sensor framework expects the real sampling frequency
 * values. This mapping was copied from the ST LIS2DH driver in Zephyr which
 * translates these back into ODR values. These are the supported sampling
 * frequency / ODR settings supported by the ST LISxDH parts.
 */
static uint16_t lis2dh_odr_map[] = { 0,	  1,   10,   25,   50,	100,
				     200, 400, 1620, 1344, 5376 };

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void motion_alarm_ccc_handler(const struct bt_gatt_attr *attr,
				     uint16_t value);
static void motion_sensor_trig_handler(const struct device *dev,
				       struct sensor_trigger *trigger);
static void motion_timer_callback(struct k_timer *timer_id);
static void motion_svc_connected(struct bt_conn *conn, uint8_t err);
static void motion_svc_disconnected(struct bt_conn *conn, uint8_t reason);

/******************************************************************************/
/* Motion Service Declaration                                                 */
/******************************************************************************/
static struct bt_conn_cb motion_svc_conn_callbacks = {
	.connected = motion_svc_connected,
	.disconnected = motion_svc_disconnected,
};

static struct bt_gatt_attr motion_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&MOTION_SVC_UUID),
	BT_GATT_CHARACTERISTIC(&MOTION_ALARM_UUID.uuid, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL,
			       &bms.motion_alarm),
	LBT_GATT_CCC(motion_alarm)
};

static struct bt_gatt_service motion_svc = BT_GATT_SERVICE(motion_attrs);
static const struct device *sensor = NULL;
/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
static void motion_svc_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		return;
	}

	if (!lbt_slave_role(conn)) {
		return;
	}

	motion_svc_conn = bt_conn_ref(conn);
}

static void motion_svc_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (!lbt_slave_role(conn)) {
		return;
	}

	if (motion_svc_conn) {
		bt_conn_unref(motion_svc_conn);
		motion_svc_conn = NULL;
	}
}

/* The weak implementation can be used for single peripheral designs. */
__weak struct bt_conn *motion_svc_get_conn(void)
{
	return motion_svc_conn;
}

bool UpdateOdr(int Value)
{
	bool ret = true;
	int Status = 0;
	struct sensor_value sVal;

	sVal.val1 = lis2dh_odr_map[Value];
	sVal.val2 = 0;

	Status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_SAMPLING_FREQUENCY, &sVal);
	nvStoreAccelODR(Value);

	LOG_DBG("ODR = %d, Status = %d", Value, Status);

	return (ret);
}

bool UpdateScale(int Value)
{
	bool ret = true;
	int Status = 0;

	struct sensor_value sVal;

	sensor_g_to_ms2(Value, &sVal);
	Status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_FULL_SCALE, &sVal);

	nvStoreAccelScale(Value);

	LOG_DBG("Scale = %d, Status = %d", Value, Status);

	return (ret);
}

bool UpdateActivityThreshold(int Value)
{
	bool ret = true;
	int Status = 0;
	struct sensor_value sVal;

	sVal.val1 = Value;
	sVal.val2 = 0;

	Status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_SLOPE_TH, &sVal);
	nvStoreAccelThresh(Value);

	LOG_DBG("Activity Threshold = %d, Status = %d", Value, Status);

	return (ret);
}

int GetOdr()
{
	int Value = 0;
	nvReadAccelODR(&Value);
	LOG_DBG("ODR = %d", Value);

	return (Value);
}

int GetScale()
{
	int Value = 0;
	nvReadAccelScale(&Value);
	LOG_DBG("Scale = %d", Value);

	return (Value);
}

int GetActivityThreshold()
{
	int Value = 0;
	nvReadAccelThresh(&Value);
	LOG_DBG("Threshold = %d", Value);

	return (Value);
}

struct motion_status *motionGetStatus()
{
	motionStatus.motion = bms.motion_alarm;
	motionStatus.scale = GetScale();
	motionStatus.odr = GetOdr();
	motionStatus.thr = GetActivityThreshold();

	return (&motionStatus);
}

static void motion_svc_notify(bool notify, uint16_t index, uint16_t length)
{
	struct bt_conn *connection_handle = motion_svc_get_conn();
	if (connection_handle != NULL) {
		if (notify) {
			bt_gatt_notify(connection_handle,
				       &motion_svc.attrs[index],
				       motion_svc.attrs[index].user_data,
				       length);
		}
	}
}

void motion_svc_set_alarm_state(uint8_t alarmState)
{
	bms.motion_alarm = alarmState;
	motion_svc_notify(ccc.motion_alarm.notify, bms.motion_alarm_index,
			  sizeof(bms.motion_alarm));
}

void motion_svc_init()
{
	struct sensor_trigger trigger;
	struct sensor_value sVal;
	int status = 0;
	size_t gatt_size;

	sensor = device_get_binding(DT_LABEL(DT_INST(0, st_lis2dh)));
	if (sensor == NULL) {
		MOTION_SVC_LOG_ERR("Could not get st_lis2dh binding");
		return;
	}

	k_timer_init(&motion_timer, motion_timer_callback, NULL);
	bms.motion_alarm = MOTION_ALARM_INACTIVE;

	/* configure the sensor */

	/* NOTE: the zephyr sensor framework expects a frequency value rather
	 * than ODR value directly
	 */
	sVal.val1 = lis2dh_odr_map[GetOdr()];
	sVal.val2 = 0;
	status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_SAMPLING_FREQUENCY, &sVal);
	if (status < 0) {
		MOTION_SVC_LOG_ERR("Failed to set ODR in the accelerometer.");
		return;
	}

	/* configure the scale */
	/* NOTE: the zephyr sensor framework expects a value in m/S^2: 9.80665 m/s^2 = 1G */
	sensor_g_to_ms2(GetScale(), &sVal);
	status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_FULL_SCALE, &sVal);
	if (status < 0) {
		MOTION_SVC_LOG_ERR(
			"Failed to set the scale in the accelerometer.");
		return;
	}

	/* configure the threshold value */
	sVal.val1 = GetActivityThreshold();
	sVal.val2 = 0;
	status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_SLOPE_TH, &sVal);
	if (status < 0) {
		MOTION_SVC_LOG_ERR(
			"Failed to set the threshold in the accelerometer.");
		return;
	}

	/* configure the duration value */
	sVal.val1 = MOTION_DEFAULT_DUR;
	sVal.val2 = 0;
	status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_SLOPE_DUR, &sVal);
	if (status < 0) {
		MOTION_SVC_LOG_ERR(
			"Failed to set the duration in the accelerometer.");
		return;
	}

	/* configure the motion trigger */
	trigger.chan = SENSOR_CHAN_ACCEL_XYZ;
	trigger.type = SENSOR_TRIG_DELTA;
	status = sensor_trigger_set(sensor, &trigger,
				    motion_sensor_trig_handler);
	if (status < 0) {
		MOTION_SVC_LOG_ERR(
			"Failed to configure the trigger for the accelerometer.");
		return;
	}

	gatt_size = (sizeof(motion_attrs) / sizeof(motion_attrs[0]));
	bms.motion_alarm_index = lbt_find_gatt_index(&MOTION_ALARM_UUID.uuid,
						     motion_attrs, gatt_size);
	bt_gatt_service_register(&motion_svc);
	bt_conn_cb_register(&motion_svc_conn_callbacks);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void motion_alarm_ccc_handler(const struct bt_gatt_attr *attr,
				     uint16_t value)
{
	ccc.motion_alarm.notify = IS_NOTIFIABLE(value);
}

static void motion_timer_callback(struct k_timer *timer_id)
{
	k_timer_stop(&motion_timer);
	motion_svc_set_alarm_state(MOTION_ALARM_INACTIVE);
}

void motion_sensor_trig_handler(const struct device *dev,
				struct sensor_trigger *trigger)
{
	MOTION_SVC_LOG_DBG("Movement of the gateway detected.");
	k_timer_stop(&motion_timer);
	motion_svc_set_alarm_state(MOTION_ALARM_ACTIVE);
	k_timer_start(&motion_timer, INACTIVITY_TIMER_PERIOD,
		      INACTIVITY_TIMER_PERIOD);
}
