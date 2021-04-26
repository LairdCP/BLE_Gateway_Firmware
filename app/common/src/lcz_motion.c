/**
 * @file lcz_motion.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lcz_motion, CONFIG_LCZ_MOTION_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <drivers/sensor.h>

#include "attr.h"
#include "lcz_motion.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define MOTION_ALARM_ACTIVE 1
#define MOTION_ALARM_INACTIVE 0
#define INACTIVITY_TIMER_PERIOD K_MSEC(30000)

#define BREAK_ON_ERROR(x)                                                      \
	if (x < 0) {                                                           \
		break;                                                         \
	}

/* This array maps ODR values to real sampling frequency values. To keep the
 * implementation common between various Laird Connectivity products for the
 * device shadow, we present ODR values rather than the real sampling frequency
 * values. However, the sensor framework expects the real sampling frequency
 * values. This mapping was copied from the ST LIS2DH driver in Zephyr which
 * translates these back into ODR values. These are the supported sampling
 * frequency / ODR settings supported by the ST LISxDH parts.
 */
static const uint16_t LIS2DH_ODR_MAP[] = { 0,	1,   10,   25,	 50,  100,
					   200, 400, 1620, 1344, 5376 };

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct k_timer motion_timer;
static struct motion_status motion_status;
static const struct device *sensor = NULL;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void motion_sensor_trig_handler(const struct device *dev,
				       struct sensor_trigger *trigger);

static void motion_timer_callback(struct k_timer *timer_id);

static int configure_motion_trigger(void);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int lcz_motion_init(void)
{
	int status = 0;

	do {
		sensor = device_get_binding(DT_LABEL(DT_INST(0, st_lis2dh)));
		if (sensor == NULL) {
			LOG_ERR("Could not get st_lis2dh binding");
			break;
		}

		k_timer_init(&motion_timer, motion_timer_callback, NULL);

		motion_status.alarm = MOTION_ALARM_INACTIVE;
		lcz_motion_set_alarm_state(motion_status.alarm);

		/*
         * configure the sensor
         */

		status = lcz_motion_update_odr();
		BREAK_ON_ERROR(status);

		status = lcz_motion_update_scale();
		BREAK_ON_ERROR(status);

		status = lcz_motion_update_threshold();
		BREAK_ON_ERROR(status);

		status = lcz_motion_update_duration();
		BREAK_ON_ERROR(status);

		status = configure_motion_trigger();

	} while (0);

	if (status < 0) {
		LOG_ERR("Unable to configure motion sensor");
	} else {
		motion_status.initialized = true;
	}

	return status;
}

int lcz_motion_set_and_update_odr(int value)
{
	int status;

	status = attr_set_uint32(ATTR_ID_motionOdr, value);
	if (status == 0) {
		status = lcz_motion_update_odr();
	}
	return status;
}

int lcz_motion_update_odr(void)
{
	int status;
	struct sensor_value sensor_value;

	motion_status.odr =
		attr_get_uint32(ATTR_ID_motionOdr, MOTION_DEFAULT_ODR);

	/* NOTE: the zephyr sensor framework expects a frequency value rather
	 * than ODR value directly
	 */
	sensor_value.val1 = LIS2DH_ODR_MAP[motion_status.odr];
	sensor_value.val2 = 0;

	status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_SAMPLING_FREQUENCY, &sensor_value);

	LOG_DBG("ODR = %d, status = %d", motion_status.odr, status);
	return status;
}

int lcz_motion_set_and_update_scale(int value)
{
	int status;

	status = attr_set_uint32(ATTR_ID_motionScale, value);
	if (status == 0) {
		status = lcz_motion_update_scale();
	}
	return status;
}

int lcz_motion_update_scale(void)
{
	int status;
	struct sensor_value sensor_value;

	motion_status.scale =
		attr_get_uint32(ATTR_ID_motionScale, MOTION_DEFAULT_SCALE);

	sensor_g_to_ms2(motion_status.scale, &sensor_value);
	status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_FULL_SCALE, &sensor_value);

	LOG_DBG("Scale = %d, status = %d", motion_status.scale, status);
	return status;
}

int lcz_motion_set_and_update_threshold(int value)
{
	int status;

	status = attr_set_uint32(ATTR_ID_motionThresh, value);
	if (status == 0) {
		status = lcz_motion_update_threshold();
	}
	return status;
}

int lcz_motion_update_threshold(void)
{
	int status;
	struct sensor_value sensor_value;

	motion_status.thr =
		attr_get_uint32(ATTR_ID_motionThresh, MOTION_DEFAULT_THS);

	sensor_value.val1 = motion_status.thr;
	sensor_value.val2 = 0;

	status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_SLOPE_TH, &sensor_value);

	LOG_DBG("Activity Threshold = %d, status = %d", motion_status.thr,
		status);
	return status;
}

int lcz_motion_set_and_update_duration(int value)
{
	int status;

	status = attr_set_uint32(ATTR_ID_motionDuration, value);
	if (status == 0) {
		status = lcz_motion_update_duration();
	}
	return status;
}

int lcz_motion_update_duration(void)
{
	int status;
	struct sensor_value sensor_value;

	motion_status.dur =
		attr_get_uint32(ATTR_ID_motionDuration, MOTION_DEFAULT_DUR);

	sensor_value.val1 = motion_status.dur;
	sensor_value.val2 = 0;
	status = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
				 SENSOR_ATTR_SLOPE_DUR, &sensor_value);

	LOG_DBG("Duration = %d, status = %d", motion_status.dur, status);
	return status;
}

int lcz_motion_get_odr(void)
{
	return motion_status.odr;
}

int lcz_motion_get_scale(void)
{
	return motion_status.scale;
}

int lcz_motion_get_threshold(void)
{
	return motion_status.thr;
}

int lcz_motion_get_duration(void)
{
	return motion_status.dur;
}

struct motion_status *lcz_motion_get_status(void)
{
	return &motion_status;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void motion_timer_callback(struct k_timer *timer_id)
{
	ARG_UNUSED(timer_id);

	k_timer_stop(&motion_timer);
	motion_status.alarm = MOTION_ALARM_INACTIVE;
	lcz_motion_set_alarm_state(motion_status.alarm);
}

static void motion_sensor_trig_handler(const struct device *dev,
				       struct sensor_trigger *trigger)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(trigger);

	LOG_DBG("Movement of the gateway detected.");
	k_timer_stop(&motion_timer);
	motion_status.alarm = MOTION_ALARM_ACTIVE;
	lcz_motion_set_alarm_state(motion_status.alarm);
	k_timer_start(&motion_timer, INACTIVITY_TIMER_PERIOD,
		      INACTIVITY_TIMER_PERIOD);
}

static int configure_motion_trigger(void)
{
	struct sensor_trigger trigger;
	int status;

	trigger.chan = SENSOR_CHAN_ACCEL_XYZ;
	trigger.type = SENSOR_TRIG_DELTA;
	status = sensor_trigger_set(sensor, &trigger,
				    motion_sensor_trig_handler);

	if (status < 0) {
		LOG_ERR("Failed to configure the trigger for the accelerometer.");
	}
	return status;
}

/******************************************************************************/
/* Weak implementation - override in application                              */
/******************************************************************************/
__weak void lcz_motion_set_alarm_state(uint8_t state)
{
	return;
}
