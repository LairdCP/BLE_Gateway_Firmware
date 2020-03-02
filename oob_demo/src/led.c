/* led.c - LED control
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_INF
LOG_MODULE_REGISTER(oob_led);

#define LED_LOG_ERR(...) LOG_ERR(__VA_ARGS__)

//=============================================================================
// Includes
//=============================================================================

#include <gpio.h>
#include <kernel.h>

#include "led.h"

//=============================================================================
// Local Constant, Macro and Type Definitions
//=============================================================================

#define CHECK_INDEX() __ASSERT(index < NUMBER_OF_LEDS, "Invalid LED index")

#define TAKE_MUTEX(m)                                                          \
	do {                                                                   \
		__ASSERT(!k_is_in_isr(), "Can't use mutex in ISR context");    \
		k_mutex_lock(&m, K_FOREVER);                                   \
	} while (0)

#define GIVE_MUTEX(m) k_mutex_unlock(&m)

#define MINIMUM_ON_TIME K_MSEC(1)
#define MINIMUM_OFF_TIME K_MSEC(1)

enum led_state {
	ON = true,
	OFF = false,
};

enum led_blink_state {
	BLINK = true,
	DONT_BLINK = false,
};

struct led {
	enum led_state state;
	struct device *device_handle;
	u32_t pin;
	bool pattern_busy;
	struct led_blink_pattern pattern;
	struct k_timer timer;
	struct k_work work;
	void (*pattern_complete_function)(void);
};

//=============================================================================
// Global Data Definitions
//=============================================================================

//=============================================================================
// Local Data Definitions
//=============================================================================

static struct k_mutex led_mutex;
static struct led led[NUMBER_OF_LEDS];

//=============================================================================
// Local Function Prototypes
//=============================================================================

static void bsp_led_init(void);
static void led_timer_callback(struct k_timer *timer_id);
static void system_workq_led_timer_handler(struct k_work *item);
static void turn_on(struct led *pLed);
static void turn_off(struct led *pLed);
static void change_state(struct led *pLed, bool state, bool blink);

//=============================================================================
// Global Function Definitions
//=============================================================================

void led_init(void)
{
	k_mutex_init(&led_mutex);
	TAKE_MUTEX(led_mutex);
	bsp_led_init();

	size_t i;
	for (i = 0; i < NUMBER_OF_LEDS; i++) {
		k_timer_init(&led[i].timer, led_timer_callback, NULL);
		k_timer_user_data_set(&led[i].timer, &led[i]);
		k_work_init(&led[i].work, system_workq_led_timer_handler);
		change_state(&led[i], OFF, DONT_BLINK);
	}
	GIVE_MUTEX(led_mutex);
}

void led_turn_on(enum led_index index)
{
	CHECK_INDEX();
	TAKE_MUTEX(led_mutex);
	change_state(&led[index], ON, DONT_BLINK);
	GIVE_MUTEX(led_mutex);
}

void led_turn_off(enum led_index index)
{
	CHECK_INDEX();
	TAKE_MUTEX(led_mutex);
	change_state(&led[index], OFF, DONT_BLINK);
	GIVE_MUTEX(led_mutex);
}

void led_blink(enum led_index index, struct led_blink_pattern const *pPattern)
{
	CHECK_INDEX();
	__ASSERT_NO_MSG(pPattern != NULL);
	TAKE_MUTEX(led_mutex);
	led[index].pattern_busy = true;
	memcpy(&led[index].pattern, pPattern, sizeof(struct led_blink_pattern));
	led[index].pattern.on_time =
		MAX(led[index].pattern.on_time, MINIMUM_ON_TIME);
	led[index].pattern.off_time =
		MAX(led[index].pattern.off_time, MINIMUM_OFF_TIME);
	change_state(&led[index], ON, BLINK);
	GIVE_MUTEX(led_mutex);
}

void led_register_pattern_complete_function(enum led_index index,
					    void (*function)(void))
{
	CHECK_INDEX();
	TAKE_MUTEX(led_mutex);
	led[index].pattern_complete_function = function;
	GIVE_MUTEX(led_mutex);
}

bool led_pattern_busy(enum led_index index)
{
	bool result = false;
	CHECK_INDEX();
	TAKE_MUTEX(led_mutex);
	result = led[index].pattern_busy;
	GIVE_MUTEX(led_mutex);
	return result;
}

//=============================================================================
// Local Function Definitions
//=============================================================================

static void led_bind_device(enum led_index index, const char *name)
{
	CHECK_INDEX();
	led[index].device_handle = device_get_binding(name);
	if (!led[index].device_handle) {
		LED_LOG_ERR("Cannot find %s!", name);
	}
}

static void led_configure_pin(enum led_index index, u32_t pin)
{
	CHECK_INDEX();
	int ret;
	led[index].pin = pin;
	ret = gpio_pin_configure(led[index].device_handle, led[index].pin,
				 (GPIO_DIR_OUT));
	if (ret) {
		LED_LOG_ERR("Error configuring GPIO");
	}
	ret = gpio_pin_write(led[index].device_handle, led[index].pin, LED_OFF);
	if (ret) {
		LED_LOG_ERR("Error setting GPIO state");
	}
}

static void bsp_led_init(void)
{
	led_bind_device(BLUE_LED1, LED1_DEV);
	led_bind_device(GREEN_LED2, LED2_DEV);
	led_bind_device(RED_LED3, LED3_DEV);
	led_bind_device(GREEN_LED4, LED4_DEV);

	led_configure_pin(BLUE_LED1, LED1);
	led_configure_pin(GREEN_LED2, LED2);
	led_configure_pin(RED_LED3, LED3);
	led_configure_pin(GREEN_LED4, LED4);
}

static void system_workq_led_timer_handler(struct k_work *item)
{
	TAKE_MUTEX(led_mutex);
	struct led *pLed = CONTAINER_OF(item, struct led, work);
	if (pLed->pattern.repeat_count == 0) {
		change_state(pLed, OFF, DONT_BLINK);
		if (pLed->pattern_complete_function != NULL) {
			pLed->pattern_busy = false;
			pLed->pattern_complete_function();
		}
	} else {
		// Blink patterns start with the LED on, so check the repeat count after the
		// first on->off cycle has completed (when the repeat count is non-zero).
		if (pLed->state == ON) {
			change_state(pLed, OFF, BLINK);
		} else {
			if (pLed->pattern.repeat_count != REPEAT_INDEFINITELY) {
				pLed->pattern.repeat_count -= 1;
			}
			change_state(pLed, ON, BLINK);
		}
	}
	GIVE_MUTEX(led_mutex);
}

static void change_state(struct led *pLed, bool state, bool blink)
{
	if (state == ON) {
		pLed->state = ON;
		turn_on(pLed);
	} else {
		pLed->state = OFF;
		turn_off(pLed);
	}

	if (!blink) {
		pLed->pattern.repeat_count = 0;
		k_timer_stop(&pLed->timer);
	} else {
		if (state == ON) {
			k_timer_start(&pLed->timer, pLed->pattern.on_time, 0);
		} else {
			k_timer_start(&pLed->timer, pLed->pattern.off_time, 0);
		}
	}

	LOG_DBG("%s %s", state ? "On" : "Off", blink ? "blink" : "Don't blink");
}

static void turn_on(struct led *pLed)
{
	gpio_pin_write(pLed->device_handle, pLed->pin, LED_ON);
}

static void turn_off(struct led *pLed)
{
	gpio_pin_write(pLed->device_handle, pLed->pin, LED_OFF);
}

//=============================================================================
// Interrupt Service Routines
//=============================================================================

static void led_timer_callback(struct k_timer *timer_id)
{
	// Add item to system work queue so that it can be handled in task
	// context because LEDs cannot be handed in interrupt context (mutex).
	struct led *pLed = (struct led *)k_timer_user_data_get(timer_id);
	k_work_submit(&pLed->work);
}
