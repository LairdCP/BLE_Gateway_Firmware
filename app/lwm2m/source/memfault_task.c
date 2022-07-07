/**
 * @file memfault_task.c
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(memfault_task, CONFIG_LCZ_LWM2M_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr.h>
#include <drivers/modem/hl7800.h>
#include <memfault_ncs.h>
#if defined(CONFIG_ATTR)
#include "attr.h"
#endif
#include "lcz_memfault.h"
#include "memfault_task.h"

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
static struct k_timer report_data_timer;
static uint8_t chunk_buf[CONFIG_LCZ_LWM2M_MEMFAULT_CHUNK_BUF_SIZE];

/**************************************************************************************************/
/* Global Data Definitions                                                                        */
/**************************************************************************************************/
extern const k_tid_t memfault;

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static void report_data_timer_expired(struct k_timer *timer_id);

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
static void report_data_timer_expired(struct k_timer *timer_id)
{
	(void)lcz_lwm2m_memfault_post_data();
}

static void memfault_thread(void *arg1, void *arg2, void *arg3)
{
	const char *dev_id;
	bool post_data;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

#ifdef CONFIG_MODEM_HL7800
	dev_id = mdm_hl7800_get_imei();
#else
	dev_id = attr_get_quasi_static(ATTR_ID_bluetooth_address);
#endif

	memfault_ncs_device_id_set(dev_id, strlen(dev_id));

	LCZ_MEMFAULT_HTTP_INIT();
	k_timer_init(&report_data_timer, report_data_timer_expired, NULL);

	k_timer_start(&report_data_timer,
		      K_SECONDS(CONFIG_LCZ_LWM2M_MEMFAULT_REPORT_PERIOD_SECONDS),
		      K_SECONDS(CONFIG_LCZ_LWM2M_MEMFAULT_REPORT_PERIOD_SECONDS));

	while (true) {
		k_thread_suspend(memfault);
#ifdef CONFIG_MODEM_HL7800
#ifdef CONFIG_ATTR
		if (attr_get_uint32(ATTR_ID_lte_rat, 0) == MDM_RAT_CAT_NB1) {
			post_data = false;
		} else
#endif
		{
			post_data = true;
		}
#else
		post_data = true;
#endif
		if (post_data) {
			LOG_INF("Posting Memfault data...");
			(void)LCZ_MEMFAULT_POST_DATA_V2(chunk_buf, sizeof(chunk_buf));
			LOG_INF("Memfault data sent!");
		}

		/* Reset timer each time data is sent */
		k_timer_start(&report_data_timer,
			      K_SECONDS(CONFIG_LCZ_LWM2M_MEMFAULT_REPORT_PERIOD_SECONDS),
			      K_SECONDS(CONFIG_LCZ_LWM2M_MEMFAULT_REPORT_PERIOD_SECONDS));
	}
}

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
int lcz_lwm2m_memfault_post_data(void)
{
	k_thread_resume(memfault);
	return 0;
}

K_THREAD_DEFINE(memfault, CONFIG_LCZ_LWM2M_MEMFAULT_THREAD_STACK_SIZE, memfault_thread, NULL,
		NULL, NULL, K_PRIO_PREEMPT(CONFIG_LCZ_LWM2M_MEMFAULT_THREAD_PRIORITY), 0, 0);
