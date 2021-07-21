/**
 * @file sntp_qrtc.c
 * @brief SNTP QRTC time syncronisation.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(sntp_qrtc, CONFIG_SNTP_QRTC_LOG_LEVEL);

#define SNTP_QRTC_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define SNTP_QRTC_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define SNTP_QRTC_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define SNTP_QRTC_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <stdio.h>
#include <net/sntp.h>
#include "attr.h"
#include "lcz_qrtc.h"
#include "sntp_qrtc.h"
#if defined(CONFIG_NET_L2_ETHERNET)
#include "ethernet_network.h"
#elif defined(CONFIG_MODEM)
#include "lte.h"
#else
#error "Unsupported build configuration for sntp_qrtc"
#endif

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#if !defined(CONFIG_USER_APPLICATION)
/* Application configuration option disabled, use defaults for SNTP */
#define CONFIG_SNTP_TIMEOUT_MILLISECONDS 300
#define CONFIG_SNTP_SYNCRONISATION_DELAY_SECONDS 5 /* Delay SNTP query for 5 seconds after IP is added */
#define CONFIG_SNTP_RESYNCRONISATION_SECONDS 3600 /* Re-syncronise with SNTP server every 1 hour */
#define CONFIG_SNTP_ERROR_SYNCRONISATION_SECONDS 30 /* Retry SNTP syncronisation every 30 seconds upon failure */
#define CONFIG_SNTP_ERROR_ATTEMPTS 5 /* Try 5 times to syncronise SNTP before delaying */
#define CONFIG_SNTP_THREAD_PRIORITY 5
#define CONFIG_SNTP_THREAD_STACK_SIZE 2048
#endif

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void sntp_recheck_timer_callback(struct k_timer *dummy);
static void sntp_thread(void *unused1, void *unused2, void *unused3);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
K_THREAD_STACK_DEFINE(sntp_stack_area, CONFIG_SNTP_THREAD_STACK_SIZE);
static struct k_timer sntp_timer;
static k_tid_t sntp_tid;
static struct k_thread sntp_thread_data;
static struct k_sem sntp_sem;
#if CONFIG_SNTP_ERROR_ATTEMPTS != 0
uint8_t sntp_fail_count;
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void sntp_qrtc_init(void)
{
	/* Setup timer and thread for SNTP operation */
	k_timer_init(&sntp_timer, sntp_recheck_timer_callback, NULL);

	k_sem_init(&sntp_sem, 0, 1);
	sntp_tid = k_thread_create(&sntp_thread_data, sntp_stack_area,
				   K_THREAD_STACK_SIZEOF(sntp_stack_area),
				   sntp_thread, NULL, NULL, NULL,
				   CONFIG_SNTP_THREAD_PRIORITY, 0, K_NO_WAIT);

#if CONFIG_SNTP_ERROR_ATTEMPTS != 0
	sntp_fail_count = 0;
#endif
}

bool sntp_qrtc_update_time(void)
{
	if (
#if defined(CONFIG_NET_L2_ETHERNET)
	    ethernet_network_connected()
#elif defined(CONFIG_MODEM)
	    lte_ready()
#endif
	   ) {
		k_sem_give(&sntp_sem);
		return true;
	}

	return false;
}

void sntp_qrtc_start_delay(void)
{
	k_timer_start(&sntp_timer,
		      K_SECONDS(CONFIG_SNTP_SYNCRONISATION_DELAY_SECONDS),
		      K_NO_WAIT);
}

void sntp_qrtc_stop(void)
{
	k_timer_stop(&sntp_timer);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void sntp_recheck_timer_callback(struct k_timer *dummy)
{
	ARG_UNUSED(dummy);

	sntp_qrtc_update_time();
}

static void sntp_thread(void *unused1, void *unused2, void *unused3)
{
	struct sntp_time sntp_server_time;
	struct tm *sntp_server_time_tm;
	int rc;
	uint32_t qrtc_epoch;

	while (1) {
		k_sem_take(&sntp_sem, K_FOREVER);

		sntp_qrtc_event(SNTP_QRTC_UPDATING);

		rc = sntp_simple(attr_get_quasi_static(ATTR_ID_sntpServer),
				 CONFIG_SNTP_TIMEOUT_MILLISECONDS,
				 &sntp_server_time);

		if (rc == 0) {
#if CONFIG_SNTP_QRTC_LOG_LEVEL >= 3
			uint32_t old_epoch = lcz_qrtc_get_epoch();
#endif
			qrtc_epoch = lcz_qrtc_set_epoch(sntp_server_time.seconds);

#if CONFIG_SNTP_QRTC_LOG_LEVEL >= 3
			sntp_server_time_tm = gmtime(&sntp_server_time.seconds);

			SNTP_QRTC_LOG_INF("SNTP response %02d/%02d/%04d "
					 "@ %02d:%02d:%02d, "
					 "QRTC Epoch set to %u",
					 sntp_server_time_tm->tm_mday,
					 (sntp_server_time_tm->tm_mon + 1),
					 (sntp_server_time_tm->tm_year + 1900),
					 sntp_server_time_tm->tm_hour,
					 sntp_server_time_tm->tm_min,
					 sntp_server_time_tm->tm_sec,
					 qrtc_epoch);

			if (qrtc_epoch == old_epoch) {
				/* Epochs matched */
				SNTP_QRTC_LOG_INF("System epoch matched SNTP time");
			} else {
				/* Epochs did not match */
				SNTP_QRTC_LOG_INF("System epoch was %d second%s"
						 " %s SNTP time",
						 abs(qrtc_epoch - old_epoch),
						 (abs(qrtc_epoch - old_epoch) == 1 ? "" : "s"),
						 (qrtc_epoch > old_epoch ? "behind" : "ahead of"));
			}
#endif

			sntp_qrtc_event(SNTP_QRTC_UPDATED);


			k_timer_start(&sntp_timer,
				      K_SECONDS(CONFIG_SNTP_RESYNCRONISATION_SECONDS),
				      K_NO_WAIT);

#if CONFIG_SNTP_ERROR_ATTEMPTS != 0
			sntp_fail_count = 0;
#endif
		} else {
			SNTP_QRTC_LOG_WRN("Get time from SNTP server failed!"
					 " (%d)", rc);

#if CONFIG_SNTP_ERROR_ATTEMPTS != 0
			++sntp_fail_count;

			if (sntp_fail_count >= CONFIG_SNTP_ERROR_ATTEMPTS) {
				/* Too many failures for now, delay for a
				 * longer time to allow network connectivity
				 * issues to resolve
				 */
				sntp_fail_count = 0;

				SNTP_QRTC_LOG_WRN("Too many consecutive SNTP "
						 "failures, delaying SNTP "
						 "syncronisation for %d "
						 "seconds",
						 CONFIG_SNTP_RESYNCRONISATION_SECONDS);

				k_timer_start(&sntp_timer,
					      K_SECONDS(CONFIG_SNTP_RESYNCRONISATION_SECONDS),
					      K_NO_WAIT);
			} else {
				SNTP_QRTC_LOG_WRN("SNTP consecutive fail count "
						 "%d, next SNTP syncronisation"
						 " in %d seconds",
						 sntp_fail_count,
						 CONFIG_SNTP_ERROR_SYNCRONISATION_SECONDS);
#endif

				k_timer_start(&sntp_timer,
					      K_SECONDS(CONFIG_SNTP_ERROR_SYNCRONISATION_SECONDS),
					      K_NO_WAIT);
#if CONFIG_SNTP_ERROR_ATTEMPTS != 0
			}
#endif

			sntp_qrtc_event(SNTP_QRTC_UPDATE_FAILED);
		}
	}
}

__weak void sntp_qrtc_event(enum sntp_qrtc_event event)
{
	ARG_UNUSED(event);
}
