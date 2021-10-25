/**
 * @file main.c
 * @brief Application main entry point
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(main);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>

#include "app_version.h"
#include "control_task.h"

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
const char *get_app_type(void)
{
#if defined CONFIG_LWM2M
	return "LwM2M";
#elif defined CONFIG_CONTACT_TRACING
	return "Contact Tracing";
#else
	return "AWS";
#endif
}

const char *get_app_type_short(void)
{
#if defined CONFIG_LWM2M
	return "LwM2M";
#elif defined CONFIG_CONTACT_TRACING
	return "CT";
#else
	return "AWS";
#endif
}

void main(void)
{
	printk("\n" CONFIG_BOARD " - %s v%s (%s)\n", get_app_type(),
	       APP_VERSION_STRING,
#if defined(BUILD_VERSION_LOCAL)
	       STRINGIFY(BUILD_VERSION_LOCAL)
#else
	       "Unknown application version"
#endif
	);

	control_task_initialize();
	control_task_thread();

	LOG_ERR("Exiting main thread");
	return;
}
