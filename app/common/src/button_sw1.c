/**
 * @file button_sw1.c
 * @brief SW1 is labelled SW2 on the Pinnacle DVK
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(button_sw1, CONFIG_BUTTON_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>
#include <device.h>
#include <drivers/gpio.h>

#include "button_sw1.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define GPIO_PIN_ACTIVE 1
#define GPIO_PIN_INACTIVE 0

#define SW1_NODE DT_ALIAS(sw1)

#if DT_NODE_HAS_STATUS(SW1_NODE, okay)
#define SW1_GPIO_LABEL DT_GPIO_LABEL(SW1_NODE, gpios)
#define SW1_GPIO_PIN DT_GPIO_PIN(SW1_NODE, gpios)
#define SW1_GPIO_FLAGS (GPIO_INPUT | DT_GPIO_FLAGS(SW1_NODE, gpios))
#else
#error "Unsupported board: sw1 devicetree alias is not defined"
#define SW1_GPIO_LABEL ""
#define SW1_GPIO_PIN 0
#define SW1_GPIO_FLAGS 0
#endif

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void on_button_release_isr(int64_t time);
static void on_button_press_isr(void);

static void button_pressed_isr(const struct device *dev,
			       struct gpio_callback *cb, uint32_t pins);

static bool min_hold_met(int64_t time, int64_t duration);
static bool max_hold_met(int64_t time, int64_t duration);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct gpio_callback button_sw1_cb_data;
static int64_t button_sw1_edge_time;
static const button_sw1_config_t *button_sw1_config;
static size_t button_sw1_config_count;
static int (*on_press)(void);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int button_sw1_initialize(const button_sw1_config_t *const config, size_t count,
			  int (*on_press_callback)(void))
{
	const struct device *button_sw1;
	int ret = -EPERM;

	button_sw1_config = config;
	button_sw1_config_count = count;
	on_press = on_press_callback;

	do {
		button_sw1 = device_get_binding(SW1_GPIO_LABEL);
		if (button_sw1 == NULL) {
			LOG_ERR("Error: didn't find %s device", SW1_GPIO_LABEL);
			break;
		}

		ret = gpio_pin_configure(button_sw1, SW1_GPIO_PIN, SW1_GPIO_FLAGS);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to configure %s pin %d", ret,
				SW1_GPIO_LABEL, SW1_GPIO_PIN);
			break;
		}

		ret = gpio_pin_interrupt_configure(button_sw1, SW1_GPIO_PIN,
						   GPIO_INT_EDGE_BOTH);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to configure interrupt on %s pin %d",
				ret, SW1_GPIO_LABEL, SW1_GPIO_PIN);
			break;
		}

		gpio_init_callback(&button_sw1_cb_data, button_pressed_isr,
				   BIT(SW1_GPIO_PIN));
		ret = gpio_add_callback(button_sw1, &button_sw1_cb_data);

	} while (0);

	LOG_DBG("Set up button_sw1 at %s pin %d val %d status: %d", SW1_GPIO_LABEL,
		SW1_GPIO_PIN, gpio_pin_get(button_sw1, SW1_GPIO_PIN), ret);

	return ret;
}

/******************************************************************************/
/* Interrupt Service Routines                                                 */
/******************************************************************************/
static void button_pressed_isr(const struct device *dev,
			       struct gpio_callback *cb, uint32_t pins)
{
	int ps = gpio_pin_get(dev, SW1_GPIO_PIN);
	if (ps == GPIO_PIN_ACTIVE) {
		LOG_DBG("Pressed");
		(void)k_uptime_delta(&button_sw1_edge_time);
		on_button_press_isr();
	} else if (ps == GPIO_PIN_INACTIVE) {
		LOG_DBG("Released");
		on_button_release_isr(k_uptime_delta(&button_sw1_edge_time));
	} else {
		LOG_ERR("Error: %d", ps);
	}
}

static void on_button_release_isr(int64_t time)
{
	size_t i;

	LOG_DBG("delta: %i ms", (int32_t)time);

	for (i = 0; i < button_sw1_config_count; i++) {
		if (min_hold_met(time, button_sw1_config[i].min_hold) &&
		    max_hold_met(time, button_sw1_config[i].max_hold)) {
			if (button_sw1_config[i].callback != NULL) {
				button_sw1_config[i].callback();
			}
			break;
		}
	}
}

static bool min_hold_met(int64_t time, int64_t duration)
{
	if (duration <= 0) {
		return true;
	} else if (time > duration) {
		return true;
	}
	return false;
}

static bool max_hold_met(int64_t time, int64_t duration)
{
	if (duration <= 0) {
		return true;
	} else if (time < duration) {
		return true;
	}
	return false;
}

static void on_button_press_isr(void)
{
	if (on_press != NULL) {
		(void)on_press();
	}
}
