/* power.c - Voltage measurement control
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#include <logging/log_output.h>
#include <logging/log_ctrl.h>
#define LOG_LEVEL LOG_LEVEL_INF
LOG_MODULE_REGISTER(oob_power);

#define POWER_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define POWER_LOG_ERR(...) LOG_ERR(__VA_ARGS__)

//=============================================================================
// Includes
//=============================================================================

#include <stdio.h>
#include <zephyr/types.h>
#include <kernel.h>
#include <gpio.h>
#include <hal/nrf_saadc.h>
#include <adc.h>
#ifdef CONFIG_REBOOT
#include <misc/reboot.h>
#endif
#include "power.h"
#include "ble_power_service.h"

//=============================================================================
// Local Constant, Macro and Type Definitions
//=============================================================================

#define ADC_RESOLUTION			12
#define ADC_ACQUISITION_TIME		ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)
#define ADC_CHANNEL_ID			0
#define ADC_SATURATION			2048
#define ADC_LIMIT_VALUE			4095.0
#define ADC_REFERENCE_VOLTAGE		0.6
#define ADC_VOLTAGE_TOP_RESISTOR	14.1
#define ADC_VOLTAGE_BOTTOM_RESISTOR	1.1
#define ADC_DECIMAL_DIVISION_FACTOR	100.0 //Keeps to 2 decimal places
#define ADC_GAIN_FACTOR_TWO		2.0
#define ADC_GAIN_FACTOR_ONE		1.0
#define ADC_GAIN_FACTOR_HALF		0.5
#define MEASURE_STATUS_ENABLE		1
#define MEASURE_STATUS_DISABLE		0
#define GPREGRET_BOOTLOADER_VALUE	0xb1

//=============================================================================
// Local Data Definitions
//=============================================================================

//NEED CONFIG_ADC_CONFIGURABLE_INPUTS

static struct adc_channel_cfg m_1st_channel_cfg = {
	.reference        = ADC_REF_INTERNAL,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id       = ADC_CHANNEL_ID,
	.input_positive   = NRF_SAADC_INPUT_AIN5
};

static s16_t m_sample_buffer;
static struct k_mutex adc_mutex;
static struct k_timer power_timer;
static struct k_work power_work;
static bool timer_enabled;

//=============================================================================
// Local Function Prototypes
//=============================================================================

static void power_adc_to_voltage(s16_t adc, float scaling, u8_t *voltage_int,
				 u8_t *voltage_dec);
static bool power_measure_adc(struct device *adc_dev, enum adc_gain gain,
			      const struct adc_sequence sequence);
static void power_run(void);
static void system_workq_power_timer_handler(struct k_work *item);
static void power_timer_callback(struct k_timer *timer_id);

//=============================================================================
// Global Function Definitions
//=============================================================================

void power_init(void)
{
	int ret;

	/* Setup mutex work-queue and repetitive timer */
	k_mutex_init(&adc_mutex);
	k_timer_init(&power_timer, power_timer_callback, NULL);
	k_work_init(&power_work, system_workq_power_timer_handler);

	/* Configure the VIN_ADC_EN pin as an output set low to disable the
	   power supply voltage measurement */
	ret = gpio_pin_configure(device_get_binding(MEASURE_ENABLE_PORT),
				 MEASURE_ENABLE_PIN, (GPIO_DIR_OUT));
	if (ret) {
		LOG_ERR("Error configuring power GPIO");
		return;
	}

	ret = gpio_pin_write(device_get_binding(MEASURE_ENABLE_PORT),
			     MEASURE_ENABLE_PIN, MEASURE_STATUS_DISABLE);
	if (ret) {
		LOG_ERR("Error setting power GPIO");
		return;
	}
}

void power_mode_set(bool enable)
{
	if (enable == true && timer_enabled == false) {
		k_timer_start(&power_timer, POWER_TIMER_PERIOD,
			      POWER_TIMER_PERIOD);
	}
	else if (enable == false && timer_enabled == true) {
		k_timer_stop(&power_timer);
	}
	timer_enabled = enable;

	if (enable == true) {
		/* Take a reading right away */
		power_run();
	}
}

#ifdef CONFIG_REBOOT
void power_reboot_module(u8_t type)
{
	/* Log panic will cause all buffered logs to be output */
	LOG_INF("Rebooting module%s...", (type == REBOOT_TYPE_BOOTLOADER ?
		" into UART bootloader" : ""));
	log_panic();

	/* And reboot the module */
	sys_reboot((type == REBOOT_TYPE_BOOTLOADER ?
		    GPREGRET_BOOTLOADER_VALUE : 0));
}
#endif

//=============================================================================
// Local Function Definitions
//=============================================================================

static void power_adc_to_voltage(s16_t adc, float scaling, u8_t *voltage_int,
				 u8_t *voltage_dec)
{
	float voltage = (float)adc / ADC_LIMIT_VALUE * ADC_REFERENCE_VOLTAGE *
			ADC_VOLTAGE_TOP_RESISTOR / ADC_VOLTAGE_BOTTOM_RESISTOR
			* scaling;

	*voltage_int = voltage;
	*voltage_dec = ((voltage - (float)(*voltage_int)) *
		       ADC_DECIMAL_DIVISION_FACTOR);
}

static bool power_measure_adc(struct device *adc_dev, enum adc_gain gain,
			      const struct adc_sequence sequence)
{
	int ret = 0;

	/* Setup ADC with desired gain */
	m_1st_channel_cfg.gain = gain;
	ret = adc_channel_setup(adc_dev, &m_1st_channel_cfg);
	if (ret) {
		LOG_ERR("adc_channel_setup failed with %d", ret);
		return false;
	}

	/* Take ADC reading */
	ret = adc_read(adc_dev, &sequence);
	if (ret) {
		LOG_ERR("adc_read failed with %d", ret);
		return false;
	}

	return true;
}

static void power_run(void)
{
	int ret;
	u8_t voltage_int;
	u8_t voltage_dec;
	bool finished = false;

	/* Find the ADC device */
	struct device *adc_dev = device_get_binding(DT_ADC_0_NAME);
	if (adc_dev == NULL) {
		LOG_ERR("ADC device name %s not found", DT_ADC_0_NAME);
		return;
	}

	(void)memset(&m_sample_buffer, 0, sizeof(m_sample_buffer));

	const struct adc_sequence sequence = {
		.channels    = BIT(ADC_CHANNEL_ID),
		.buffer      = &m_sample_buffer,
		.buffer_size = sizeof(m_sample_buffer),
		.resolution  = ADC_RESOLUTION,
	};

	/* Prevent other ADC uses */
	k_mutex_lock(&adc_mutex, K_FOREVER);

	/* Enable power supply voltage to be monitored */
	ret = gpio_pin_write(device_get_binding(MEASURE_ENABLE_PORT),
			     MEASURE_ENABLE_PIN, MEASURE_STATUS_ENABLE);
	if (ret) {
		LOG_ERR("Error setting power GPIO");
		return;
	}

	/* Measure voltage with 1/2 scaling which is suitable for higher
	   voltage supplies */
	power_measure_adc(adc_dev, ADC_GAIN_1_2, sequence);
	power_adc_to_voltage(m_sample_buffer, ADC_GAIN_FACTOR_TWO,
			     &voltage_int, &voltage_dec);

	if (m_sample_buffer >= ADC_SATURATION)
	{
		/* We have reached saturation point, do not try the next ADC
		   scaling */
		finished = true;
	}

	if (finished == false) {
		/* Measure voltage with unity scaling which is suitable for
		   medium voltage supplies */
		power_measure_adc(adc_dev, ADC_GAIN_1, sequence);
		power_adc_to_voltage(m_sample_buffer, ADC_GAIN_FACTOR_ONE,
				     &voltage_int, &voltage_dec);

		if (m_sample_buffer >= ADC_SATURATION)
		{
			/* We have reached saturation point, do not try the
			   next ADC scaling */
			finished = true;
		}
	}

	if (finished == false) {
		/* Measure voltage with double scaling which is suitable for
		   low voltage supplies, such as 2xAA batteries */
		power_measure_adc(adc_dev, ADC_GAIN_2, sequence);
		power_adc_to_voltage(m_sample_buffer, ADC_GAIN_FACTOR_HALF,
				     &voltage_int, &voltage_dec);
	}

	/* Disable the voltage monitoring FET */
	ret = gpio_pin_write(device_get_binding(MEASURE_ENABLE_PORT),
			     MEASURE_ENABLE_PIN, MEASURE_STATUS_DISABLE);

	if (ret) {
		LOG_ERR("Error setting power GPIO");
	}
	k_mutex_unlock(&adc_mutex);
	power_svc_set_voltage(voltage_int, voltage_dec);
}

static void system_workq_power_timer_handler(struct k_work *item)
{
	power_run();
}

//=============================================================================
// Interrupt Service Routines
//=============================================================================

static void power_timer_callback(struct k_timer *timer_id)
{
	// Add item to system work queue so that it can be handled in task
	// context because ADC cannot be used in interrupt context (mutex).
	k_work_submit(&power_work);
}
