/**
 * @file ct_wdt.c
 * @brief
 *
 * Copyright (c) 2015 Intel Corporation
 * Copyright (c) 2018 Nordic Semiconductor
 * Copyright (c) 2019 Centaur Analytics, Inc
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <logging/log.h>
LOG_MODULE_REGISTER(wdt, CONFIG_WDT_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <device.h>
#include <init.h>
#include <drivers/watchdog.h>
#include <logging/log_ctrl.h>

#include "lcz_memfault.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
/*
 * To use this, either the devicetree's /aliases must have a
 * 'watchdog0' property, or one of the following watchdog compatibles
 * must have an enabled node.
 */
#if DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
#define WDT_NODE DT_ALIAS(watchdog0)
#elif DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_watchdog)
#define WDT_NODE DT_INST(0, nordic_nrf_watchdog)
#endif

#ifdef WDT_NODE
#define WDT_DEV_NAME DT_LABEL(WDT_NODE)
#else
#error "Unsupported SoC and no watchdog0 alias in zephyr.dts"
#endif

#define WDT_FEED_RATE_MS (CONFIG_WDT_TIMEOUT_MILLISECONDS / 3)

#define WDT_MAX_USERS 31

#define WDT_NOT_INITIALIZED_MSG "WDT module not initialized"

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static K_THREAD_STACK_DEFINE(wdt_workq_stack, CONFIG_WDT_WORK_QUEUE_STACK_SIZE);

struct wdt_obj {
	bool initialized;
	atomic_t users;
	atomic_t check_ins;
	atomic_t check_mask;
	int force_id;
	const struct device *dev;
	int channel_id;
	struct k_work_q work_q;
	struct k_delayed_work feed;
};

static struct wdt_obj wdt;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int wdt_initialize(const struct device *device);
static void wdt_feeder(struct k_work *work);
static bool wdt_valid_user_id(int id);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
SYS_INIT(wdt_initialize, APPLICATION, CONFIG_WDT_INIT_PRIORITY);

int wdt_get_user_id(void)
{
	if (!wdt.initialized) {
		LOG_ERR(WDT_NOT_INITIALIZED_MSG);
		return -EPERM;
	}

	int id = (int)atomic_inc(&wdt.users);

	if (wdt_valid_user_id(id)) {
		return id;
	} else {
		return -EPERM;
	}
}

int wdt_check_in(int id)
{
	if (!wdt.initialized) {
		LOG_ERR(WDT_NOT_INITIALIZED_MSG);
		return -EPERM;
	}

	if (wdt_valid_user_id(id)) {
		atomic_set_bit(&wdt.check_ins, id);
		atomic_set_bit(&wdt.check_mask, id);
		return 0;
	} else {
		return -EINVAL;
	}
}

int wdt_pause(int id)
{
	if (!wdt.initialized) {
		LOG_ERR(WDT_NOT_INITIALIZED_MSG);
		return -EPERM;
	}

	if (wdt_valid_user_id(id)) {
		atomic_clear_bit(&wdt.check_ins, id);
		atomic_clear_bit(&wdt.check_mask, id);
		return 0;
	} else {
		return -EINVAL;
	}
}

int wdt_force(void)
{
	if (!wdt.initialized) {
		LOG_ERR(WDT_NOT_INITIALIZED_MSG);
		return -EPERM;
	}

	LOG_PANIC();
	LOG_INF("waiting for reset...");
	atomic_set_bit(&wdt.check_mask, wdt.force_id);

	return 0;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
int wdt_initialize(const struct device *device)
{
	ARG_UNUSED(device);
	struct wdt_timeout_cfg wdt_config;
	int r = 0;

	LOG_DBG("initializing watchdog");
	wdt.dev = device_get_binding(WDT_DEV_NAME);

	wdt.force_id = atomic_inc(&wdt.users);

	do {
		if (!wdt.dev) {
			LOG_ERR("cannot get WDT device binding");
			r = -EPERM;
			break;
		}

		wdt_config.flags = WDT_FLAG_RESET_SOC;
		wdt_config.window.min = 0;
		wdt_config.window.max = CONFIG_WDT_TIMEOUT_MILLISECONDS;
		wdt_config.callback = NULL;

		wdt.channel_id = wdt_install_timeout(wdt.dev, &wdt_config);
		if (wdt.channel_id < 0) {
			LOG_ERR("watchdog install error");
			r = -EPERM;
			break;
		}

		k_work_q_start(&wdt.work_q, wdt_workq_stack,
			       K_THREAD_STACK_SIZEOF(wdt_workq_stack),
			       K_LOWEST_APPLICATION_THREAD_PRIO);

		k_delayed_work_init(&wdt.feed, wdt_feeder);
		r = k_delayed_work_submit_to_queue(&wdt.work_q, &wdt.feed,
						   K_NO_WAIT);
		if (r < 0) {
			LOG_ERR("watchdog feeder init error: %d", r);
			break;
		}

		r = wdt_setup(wdt.dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
		if (r < 0) {
			LOG_ERR("watchdog setup error");
			break;
		}

		r = LCZ_MEMFAULT_WATCHDOG_UPDATE_TIMEOUT(
			CONFIG_WDT_TIMEOUT_MILLISECONDS -
			CONFIG_WDT_MEMFAULT_PRE_FIRE_MS);
		if (r < 0) {
			LOG_ERR("Unable to set memfault software watchdog time");
			break;
		}

		r = LCZ_MEMFAULT_WATCHDOG_ENABLE();
		if (r < 0) {
			LOG_ERR("Unable to enable memfault software watchdog");
			break;
		}

		wdt.initialized = true;

		LOG_WRN("Watchdog timer started with timeout of %u ms",
			CONFIG_WDT_TIMEOUT_MILLISECONDS);
	} while (0);

#ifdef CONFIG_WDT_TEST
	wdt_force();
#endif

	return r;
}

static void wdt_feeder(struct k_work *work)
{
	struct wdt_obj *w = CONTAINER_OF(work, struct wdt_obj, feed);
	int r = 0;

	if (atomic_cas(&w->check_ins, w->check_mask, 0)) {
		(void)LCZ_MEMFAULT_WATCHDOG_FEED();
		r = wdt_feed(w->dev, w->channel_id);
	}

	if (r < 0) {
		LOG_ERR("Unable to feed watchdog");
	}

	k_delayed_work_submit_to_queue(&w->work_q, &w->feed,
				       K_MSEC(WDT_FEED_RATE_MS));
}

static bool wdt_valid_user_id(int id)
{
	if (id < WDT_MAX_USERS) {
		return true;
	} else {
		LOG_ERR("Invalid wdt user id");
		return false;
	}
}