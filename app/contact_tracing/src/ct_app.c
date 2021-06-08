/**
 * @file ct_app.c
 * @brief Contact tracing application
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(ct_app, CONFIG_CT_APP_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stdio.h>
#include <shell/shell.h>

#ifdef CONFIG_SHELL_BACKEND_SERIAL
#include <shell/shell_uart.h>
#endif

#ifdef CONFIG_SHELL_BACKEND_DUMMY
#warning "log strdup buffers are not always freed when using dummy shell"
#endif

#include "lcz_software_reset.h"
#include "lcz_qrtc.h"
#include "lte.h"
#include "aws.h"
#include "bluegrass.h"
#include "sdcard_log.h"
#include "ct_ble.h"
#include "ct_app.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifdef CONFIG_SD_CARD_LOG
#define SD_LOG_PUBLISH_BUF_SIZE 2048
#define SD_LOG_PUBLISH_MAX_CHUNK_LEN 1984
#else
/* Buffer space is required to provide empty response. */
#define SD_LOG_PUBLISH_BUF_SIZE 128
#endif

#define CLEAR_RPC_MSG "{\"state\":{\"desired\":{\"rpc\":null}}}"

#define RPC_REBOOT_DELAY_MS 10000

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static uint32_t lastLogPublish = 0;
static bool awsRebootCommandReceived = false;
static bool awsSendLogCommandReceived = false;
static bool awsExecCommandReceived = false;

static uint8_t sd_log_publish_buf[SD_LOG_PUBLISH_BUF_SIZE];
static log_get_state_t log_get_state;

static struct k_delayed_work ct_app_work;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void ct_app_work_handler(struct k_work *work);
static void ct_publisher(uint32_t now);

static void handle_sd_card_log_get(void);
static void aws_handle_command(char *cmd);

static void process_log_get_cmd(void);
static void process_log_dir_command(void);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int ct_app_init(void)
{
	ct_ble_initialize();

	k_delayed_work_init(&ct_app_work, ct_app_work_handler);
	k_delayed_work_submit(&ct_app_work,
			      K_SECONDS(CONFIG_CT_APP_TICK_RATE_SECONDS));

	return 0;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void ct_app_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	ct_publisher(lcz_qrtc_get_epoch());

	k_delayed_work_submit(&ct_app_work,
			      K_SECONDS(CONFIG_CT_APP_TICK_RATE_SECONDS));
}

static int publish_clear_command(void)
{
	/* Clear the gateway command buffer. */
	rpc_params_clear_method();

	/* Publish a JSON message to clear the command from the device shadow. */
	return awsSendData((char *)CLEAR_RPC_MSG, GATEWAY_TOPIC);
}

static void ct_publisher(uint32_t now)
{
	int r;

	if (ct_ble_is_publishing_log() == false) {
		char *cmd = rpc_params_get_method();
		if (cmd[0]) {
			LOG_DBG("received rpc: '%s'", log_strdup(cmd));
			aws_handle_command(cmd);
			publish_clear_command();
			LOG_DBG("cleared rpc");

			if (awsRebootCommandReceived) {
				lcz_software_reset(RPC_REBOOT_DELAY_MS);
			}
		}

		/* If there is log data to upload and 5 seconds have passed since
		 * last chunk, send next chunk.
         */
		if (log_get_state.rpc_params.filename[0] &&
		    awsSendLogCommandReceived &&
		    now > (lastLogPublish +
			   CONFIG_CT_APP_SD_CARD_LOG_PUBLISH_RATE_SECONDS)) {
			handle_sd_card_log_get();
			lastLogPublish = now;
		}
	}

	/* Periodic check to make sure stashed entries don't stay forever
     * (which will prevent advertisement processing).
     */
	ct_ble_check_stashed_log_entries();

	/* if an exec was received, run the command */
	if (awsExecCommandReceived) {
#ifdef CONFIG_SHELL_BACKEND_SERIAL
		r = shell_execute_cmd(
			shell_backend_uart_get_ptr(),
			((rpc_params_exec_t *)rpc_params_get())->cmd);
#else
		r = shell_execute_cmd(
			NULL, ((rpc_params_exec_t *)rpc_params_get())->cmd);
#endif
		awsExecCommandReceived = false;
		LOG_DBG("Shell (RPC exec) status: %d", r);
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void handle_sd_card_log_get(void)
{
	char *sd_log_pbuf;

	sd_log_pbuf = sd_log_publish_buf;
	memset(sd_log_publish_buf, 0, sizeof(sd_log_publish_buf));

	if (log_get_state.bytes_remaining) {
		if (log_get_state.bytes_remaining ==
		    log_get_state.rpc_params.length) {
			sprintf(sd_log_pbuf, "%s %u bytes @ %u from %s\r",
				log_get_state.rpc_params.filename,
				log_get_state.rpc_params.length,
				log_get_state.rpc_params.offset,
				log_get_state.rpc_params.whence);
			sd_log_pbuf += strlen(sd_log_pbuf);
		}

#ifdef CONFIG_SD_CARD_LOG
		sdCardLogGet(sd_log_pbuf, &log_get_state,
			     SD_LOG_PUBLISH_MAX_CHUNK_LEN);
#else
		log_get_state.bytes_ready = 0;
#endif

		/* if bytes are ready in sd_log_pbuf, send them */
		if (log_get_state.bytes_ready) {
			if (log_get_state.bytes_remaining == 0) {
				strncat(sd_log_publish_buf, "\r<eof>",
					sizeof(sd_log_publish_buf) - 1);
			}
			awsSendData(sd_log_publish_buf, ct_ble_get_log_topic());
		} else {
			/* abort if no bytes are ready, likely file not found
			 * or other fs error */
			log_get_state.bytes_remaining = 0;
		}

		if (log_get_state.bytes_remaining == 0) {
			awsSendLogCommandReceived = false;
			/* clear the filename in prep for next command */
			log_get_state.rpc_params.filename[0] = '\0';
		}
	}
}

static void aws_handle_command(char *cmd)
{
	if (!cmd) {
		return;
	}

	if (strstr(cmd, "log_get") != NULL) {
		process_log_get_cmd();
	} else if (strstr(cmd, "log_dir") != NULL) {
		LOG_DBG("processing log_dir command");
		process_log_dir_command();
	} else if (strstr(cmd, "reboot") != NULL) {
		LOG_DBG("processing reboot command");
		awsRebootCommandReceived = true;
	} else if (strstr(cmd, "exec") != NULL) {
		LOG_DBG("processing exec command");
		awsExecCommandReceived = true;
	}
}

static void process_log_get_cmd(void)
{
#ifdef CONFIG_BOARD_MG100
	if (log_get_state.bytes_remaining > 0) {
		LOG_DBG("ignoring log_get command, send in progress");
	} else {
		/* parse the log_get rpc params */
		rpc_params_log_get_t *params = rpc_params_get();
		if (strlen(params->filename) > 0 && params->length > 0) {
			LOG_DBG("log_get(%s, %s, %u, %u)",
				log_strdup(params->filename),
				log_strdup(params->whence), params->offset,
				params->length);
			memcpy(&log_get_state.rpc_params, params,
			       sizeof(rpc_params_log_get_t));
			log_get_state.bytes_remaining = params->length;
			log_get_state.cur_seek = 0;
			awsSendLogCommandReceived = true;
		}
	}
#else
	LOG_WRN("ignoring log_get command, SD card not present");
#endif
}

static void process_log_dir_command(void)
{
#ifdef CONFIG_SD_CARD_LOG
	char *topic = ct_ble_get_log_topic();

	if (0 == sdCardLogLsDirToString("/", sd_log_publish_buf,
					SD_LOG_PUBLISH_MAX_CHUNK_LEN)) {
		LOG_DBG("\t\tpublishing log dir to %s", log_strdup(topic));
		awsSendData(sd_log_publish_buf, topic);
	}
#else
	LOG_WRN("ignoring log_get command, SD card not present");
#endif
}