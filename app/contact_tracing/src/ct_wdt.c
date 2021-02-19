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
LOG_MODULE_REGISTER(ct_wdt, CONFIG_CT_WDT_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <device.h>
#include <drivers/watchdog.h>
#include <logging/log_ctrl.h>

#include "ct_wdt.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
/*
 * To use this sample, either the devicetree's /aliases must have a
 * 'watchdog0' property, or one of the following watchdog compatibles
 * must have an enabled node.
 */
#if DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
#define WDT_NODE DT_ALIAS(watchdog0)
#elif DT_HAS_COMPAT_STATUS_OKAY(st_stm32_window_watchdog)
#define WDT_NODE DT_INST(0, st_stm32_window_watchdog)
#elif DT_HAS_COMPAT_STATUS_OKAY(st_stm32_watchdog)
#define WDT_NODE DT_INST(0, st_stm32_watchdog)
#elif DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_watchdog)
#define WDT_NODE DT_INST(0, nordic_nrf_watchdog)
#elif DT_HAS_COMPAT_STATUS_OKAY(espressif_esp32_watchdog)
#define WDT_NODE DT_INST(0, espressif_esp32_watchdog)
#elif DT_HAS_COMPAT_STATUS_OKAY(silabs_gecko_wdog)
#define WDT_NODE DT_INST(0, silabs_gecko_wdog)
#elif DT_HAS_COMPAT_STATUS_OKAY(nxp_kinetis_wdog32)
#define WDT_NODE DT_INST(0, nxp_kinetis_wdog32)
#elif DT_HAS_COMPAT_STATUS_OKAY(microchip_xec_watchdog)
#define WDT_NODE DT_INST(0, microchip_xec_watchdog)
#endif

#ifdef WDT_NODE
#define WDT_DEV_NAME DT_LABEL(WDT_NODE)
#else
#error "Unsupported SoC and no watchdog0 alias in zephyr.dts"
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static int wdt_channel_id;
static const struct device *wdt = NULL;
static uint32_t wdt_flags = 0;
static bool wdt_initialized;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void wdt_callback(const struct device *wdt_dev, int channel_id);
static void ct_wdt_feed(void);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void ct_wdt_init(void)
{
	int err;
	struct wdt_timeout_cfg wdt_config;

	LOG_DBG("initializing watchdog");
	wdt = device_get_binding(WDT_DEV_NAME);

	if (!wdt) {
		LOG_ERR("cannot get WDT device");
		return;
	}

	/* Reset SoC when watchdog timer expires. */
	wdt_config.flags = WDT_FLAG_RESET_SOC;

	/* Expire watchdog after 60000 milliseconds. */
	wdt_config.window.min = 0U;
	wdt_config.window.max = 60000U;

	/* Set up watchdog callback. Jump into it when watchdog expired. */
	wdt_config.callback = wdt_callback;

	wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
	if (wdt_channel_id < 0) {
		LOG_ERR("watchdog install error");
		return;
	}

	err = wdt_setup(wdt, 0);
	if (err < 0) {
		LOG_ERR("watchdog setup error");
		return;
	}

	wdt_initialized = true;
}

bool ct_wdt_initialized(void)
{
	return wdt_initialized;
}

void ct_wdt_force(void)
{
	LOG_PANIC();
	LOG_INF("waiting for reset...");
	while (1) {
		k_yield();
	};
}

uint32_t ct_wdt_get_flags(void)
{
	return wdt_flags;
}

void ct_wdt_set_flags(uint32_t flag)
{
	wdt_flags |= flag;
}

void ct_wdt_handler(void)
{
	if (wdt_initialized) {
		if ((wdt_flags & WDOG_FLAGS_ALL) == WDOG_FLAGS_ALL) {
			ct_wdt_feed();
			wdt_flags = 0;
		}
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void wdt_callback(const struct device *wdt_dev, int channel_id)
{
	ARG_UNUSED(wdt_dev);
	ARG_UNUSED(channel_id);

	/* Watchdog timer expired. Handle it here.
	 * Remember that SoC reset will be done soon.
	 */
	LOG_PANIC();
	LOG_WRN("wdt (0x%08x)", ct_wdt_get_flags());
}

static void ct_wdt_feed(void)
{
	if (wdt) {
		wdt_feed(wdt, wdt_channel_id);
	}
}
