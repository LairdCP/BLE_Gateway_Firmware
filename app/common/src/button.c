/**
 * @file button.c
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(button, CONFIG_BUTTON_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>
#include <device.h>
#include <drivers/gpio.h>

#include "button.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define GPIO_PIN_ACTIVE 1
#define GPIO_PIN_INACTIVE 0

#define SW0_NODE DT_ALIAS(sw0)

#if DT_NODE_HAS_STATUS(SW0_NODE, okay)
#define SW0_GPIO_LABEL DT_GPIO_LABEL(SW0_NODE, gpios)
#define SW0_GPIO_PIN DT_GPIO_PIN(SW0_NODE, gpios)
#define SW0_GPIO_FLAGS (GPIO_INPUT | DT_GPIO_FLAGS(SW0_NODE, gpios))
#else
#error "Unsupported board: sw0 devicetree alias is not defined"
#define SW0_GPIO_LABEL ""
#define SW0_GPIO_PIN 0
#define SW0_GPIO_FLAGS 0
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
static struct gpio_callback button_cb_data;
static int64_t button_edge_time;
static const button_config_t *button_config;
static size_t button_config_count;
static int (*on_press)(void);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int button_initialize(const button_config_t *const config, size_t count,
		      int (*on_press_callback)(void))
{
	const struct device *button;
	int ret = -EPERM;

	button_config = config;
	button_config_count = count;
	on_press = on_press_callback;

	do {
		button = device_get_binding(SW0_GPIO_LABEL);
		if (button == NULL) {
			LOG_ERR("Error: didn't find %s device", SW0_GPIO_LABEL);
			break;
		}

		ret = gpio_pin_configure(button, SW0_GPIO_PIN, SW0_GPIO_FLAGS);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to configure %s pin %d", ret,
				SW0_GPIO_LABEL, SW0_GPIO_PIN);
			break;
		}

		ret = gpio_pin_interrupt_configure(button, SW0_GPIO_PIN,
						   GPIO_INT_EDGE_BOTH);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to configure interrupt on %s pin %d",
				ret, SW0_GPIO_LABEL, SW0_GPIO_PIN);
			break;
		}

		gpio_init_callback(&button_cb_data, button_pressed_isr,
				   BIT(SW0_GPIO_PIN));
		ret = gpio_add_callback(button, &button_cb_data);

	} while (0);

	LOG_DBG("Set up button at %s pin %d val %d status: %d", SW0_GPIO_LABEL,
		SW0_GPIO_PIN, gpio_pin_get(button, SW0_GPIO_PIN), ret);

	return ret;
}

/******************************************************************************/
/* Interrupt Service Routines                                                 */
/******************************************************************************/
static void button_pressed_isr(const struct device *dev,
			       struct gpio_callback *cb, uint32_t pins)
{
	int ps = gpio_pin_get(dev, SW0_GPIO_PIN);
	if (ps == GPIO_PIN_ACTIVE) {
		LOG_DBG("Pressed");
		(void)k_uptime_delta(&button_edge_time);
		on_button_press_isr();
	} else if (ps == GPIO_PIN_INACTIVE) {
		LOG_DBG("Released");
		on_button_release_isr(k_uptime_delta(&button_edge_time));
	} else {
		LOG_ERR("Error: %d", ps);
	}
}

static void on_button_release_isr(int64_t time)
{
	size_t i;

	LOG_DBG("delta: %i ms", (int32_t)time);

	for (i = 0; i < button_config_count; i++) {
		if (min_hold_met(time, button_config[i].min_hold) &&
		    max_hold_met(time, button_config[i].max_hold)) {
			if (button_config[i].callback != NULL) {
				button_config[i].callback();
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
