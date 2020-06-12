/**
 * @file ble_motion_service.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#include <sensor.h>

#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(mg100_motion_svc);

#define MOTION_SVC_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define MOTION_SVC_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define MOTION_SVC_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define MOTION_SVC_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

#define MOTION_ALARM_ACTIVE     1
#define MOTION_ALARM_INACTIVE   0
#define INACTIVITY_TIMER_PERIOD K_MSEC(30000)
#define MOTION_DEFAULT_THS  8
#define MOTION_DEFAULT_DUR  6

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>

#include "mg100_common.h"
#include "laird_bluetooth.h"
#include "ble_motion_service.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define MOTION_SVC_BASE_UUID_128(_x_)                                           \
	BT_UUID_INIT_128(0x66, 0x9a, 0x0c, 0x20, 0x00, 0x08, 0x6e, 0x8b, 0xea, 0x11, \
            0x1a, 0xac, LSB_16(_x_), MSB_16(_x_), 0xce, 0xad)

static struct bt_uuid_128 MOTION_SVC_UUID = MOTION_SVC_BASE_UUID_128(0x0a30);
static struct bt_uuid_128 MOTION_ALARM_UUID = MOTION_SVC_BASE_UUID_128(0x0a31);

struct ble_motion_service {
    u16_t motion_alarm_index;
	u8_t motion_alarm;
};

struct ccc_table {
	struct lbt_ccc_element motion_alarm;
};

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct ble_motion_service bms;
static struct ccc_table ccc;
static struct bt_conn *(*get_connection_handle_fptr)(void);
static struct k_timer motion_timer;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void motion_alarm_ccc_handler(const struct bt_gatt_attr *attr,
                    u16_t value);
static void motion_sensor_trig_handler(struct device *dev,
					struct sensor_trigger *trigger);
static void motion_timer_callback(struct k_timer *timer_id);

/******************************************************************************/
/* Motion Service Declaration                                                 */
/******************************************************************************/
static struct bt_gatt_attr motion_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&MOTION_SVC_UUID),
	BT_GATT_CHARACTERISTIC(
		&MOTION_ALARM_UUID.uuid, BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_NONE, NULL, NULL, &bms.motion_alarm),
	LBT_GATT_CCC(motion_alarm)
};

static struct bt_gatt_service motion_svc = BT_GATT_SERVICE(motion_attrs);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void motion_svc_assign_connection_handler_getter(
	struct bt_conn *(*function)(void))
{
	get_connection_handle_fptr = function;
}

static void motion_svc_notify(bool notify, u16_t index, u16_t length)
{
	if (get_connection_handle_fptr == NULL) {
		return;
	}

	struct bt_conn *connection_handle = get_connection_handle_fptr();
	if (connection_handle != NULL) {
		if (notify) {
			bt_gatt_notify(connection_handle,
				       &motion_svc.attrs[index],
				       motion_svc.attrs[index].user_data,
				       length);
		}
	}
}

void motion_svc_set_alarm_state(u8_t alarmState)
{
	bms.motion_alarm = alarmState;
	motion_svc_notify(ccc.motion_alarm.notify, bms.motion_alarm_index,
			 sizeof(bms.motion_alarm));
}

void motion_svc_init()
{
    struct sensor_trigger trigger;
	struct device *sensor = device_get_binding(DT_ST_LIS2DH_0_LABEL);
	struct sensor_value sVal;
	int status = 0;

    bms.motion_alarm = MOTION_ALARM_INACTIVE;
	bt_gatt_service_register(&motion_svc);
	size_t gatt_size = (sizeof(motion_attrs) / sizeof(motion_attrs[0]));
	bms.motion_alarm_index = lbt_find_gatt_index(&MOTION_ALARM_UUID.uuid, motion_attrs,
						gatt_size);

   	k_timer_init(&motion_timer, motion_timer_callback, NULL);

	sVal.val1 = MOTION_DEFAULT_THS;
	sVal.val2 = 0;

	status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SLOPE_TH, &sVal);
	if (status < 0)
	{
		MOTION_SVC_LOG_ERR("Failed to set threshold in the accelerometer.");
	}
	sVal.val1 = MOTION_DEFAULT_DUR;
	status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SLOPE_DUR, &sVal);
	if (status < 0)
	{
		MOTION_SVC_LOG_ERR("Failed to set duration in the accelerometer.");
	}
	trigger.chan = SENSOR_CHAN_ACCEL_XYZ;
	trigger.type = SENSOR_TRIG_DELTA;
	status = sensor_trigger_set(sensor, &trigger, motion_sensor_trig_handler);
	if (status < 0)
	{
		MOTION_SVC_LOG_ERR("Failed to setup the trigger for the accelerometer.");
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void motion_alarm_ccc_handler(const struct bt_gatt_attr *attr, u16_t value)
{
	ccc.motion_alarm.notify = IS_NOTIFIABLE(value);
}

static void motion_timer_callback(struct k_timer *timer_id)
{
    k_timer_stop(&motion_timer);
    motion_svc_set_alarm_state(MOTION_ALARM_INACTIVE);
}

void motion_sensor_trig_handler(struct device *dev,
					 struct sensor_trigger *trigger)
{
    k_timer_stop(&motion_timer);
    motion_svc_set_alarm_state(MOTION_ALARM_ACTIVE);
    k_timer_start(&motion_timer, INACTIVITY_TIMER_PERIOD,
			      INACTIVITY_TIMER_PERIOD);
	MOTION_SVC_LOG_DBG("Movement of the gateway detected.");
}
