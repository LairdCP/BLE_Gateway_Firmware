/**
 * @file lcz_motion_temperature.c
 * @brief Get the ambient temperature from the LIS3DHTR sensor
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lmt, CONFIG_LCZ_MOTION_TEMPERATURE_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <init.h>
#include <drivers/sensor.h>

#include "attr.h"

#ifdef CONFIG_LWM2M
#include "lcz_lwm2m_client.h"
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static const struct device *lmt_sensor;

static struct k_work_delayable lmt_work;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int lmt_init(const struct device *device);
static void lmt_work_handler(struct k_work *work);
static void lmt_read_sensor(void);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
SYS_INIT(lmt_init, APPLICATION, CONFIG_LCZ_MOTION_TEMPERATURE_INIT_PRIORITY);

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int lmt_init(const struct device *device)
{
	ARG_UNUSED(device);
	int r = 0;

	lmt_sensor = device_get_binding(DT_LABEL(DT_INST(0, st_lis2dh)));

	if (lmt_sensor) {
		k_work_init_delayable(&lmt_work, lmt_work_handler);
		r = k_work_reschedule(&lmt_work, K_SECONDS(1));
	}

	return r;
}

static void lmt_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	lmt_read_sensor();

	k_work_reschedule(
		&lmt_work,
		K_SECONDS(CONFIG_LCZ_MOTION_TEMPERATURE_SAMPLE_RATE_SECONDS));
}

static void lmt_read_sensor(void)
{
	int status = 0;
	int32_t t;
	struct sensor_value val;

	status = sensor_sample_fetch_chan(lmt_sensor, SENSOR_CHAN_AMBIENT_TEMP);

	if (status == 0) {
		status = sensor_channel_get(lmt_sensor,
					    SENSOR_CHAN_AMBIENT_TEMP, &val);
	}

	if (status == 0) {
		/* Apply board/chip specific offset to result of LIS3DH */
		t = attr_get_signed32(ATTR_ID_temperatureOffset, 0) + val.val1;

		/* The temperature is used to condition the
		 * battery voltage measurement.
		 * It is actually the board temperature.
		 */
		attr_set_signed32(ATTR_ID_batteryTemperature, t);

#ifdef CONFIG_LWM2M
		lwm2m_set_temperature((float)t);
#endif
	}
}
