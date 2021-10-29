/**
 * @file hl7800_http_fota.c
 * @brief Download client handler for hl7800 image downloads
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(hl7800_http_fota, CONFIG_HTTP_FOTA_TASK_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <drivers/modem/hl7800.h>
#include <net/download_client.h>
#include <net/fota_download.h>

#include "file_system_utilities.h"
#include "http_fota_task.h"
#include "laird_utility_macros.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifndef CONFIG_FOTA_FS_MOUNT
#define CONFIG_FOTA_FS_MOUNT "/lfs"
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static size_t hl7800_file_offset = 0;
static struct download_client hl7800_dlc;
static int hl7800_socket_retries_left;
static fota_download_callback_t hl7800_fota_callback = NULL;
static uint8_t hl7800_update_expected_hash[FSU_HASH_SIZE];
static uint8_t hl7800_update_file_hash[FSU_HASH_SIZE];
static char hl7800_update_abs_path[FSU_MAX_ABS_PATH_SIZE];

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void hl7800_fota_send_evt(enum fota_download_evt_id id);
static void hl7800_fota_send_error_evt(enum fota_download_error_cause cause);
#ifdef CONFIG_FOTA_DOWNLOAD_PROGRESS_EVT
static void hl7800_fota_send_progress(int progress);
#endif
static int
hl7800_download_client_callback(const struct download_client_evt *event);

/******************************************************************************/
/* Framework Message Dispatcher                                               */
/******************************************************************************/

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int hl7800_download_client_init(fota_download_callback_t client_callback)
{
	int err = 0;

	/* ensure a non-null callback value is sent */
	if (client_callback > 0) {
		hl7800_fota_callback = client_callback;
	} else {
		return -EINVAL;
	}

	/* modem fota initialization */
	err = download_client_init(&hl7800_dlc,
				   hl7800_download_client_callback);
	if (err != 0) {
		LOG_ERR("Could not init HL7800 MODEM FOTA download %d", err);
	}

	return err;
}

int hl7800_download_start(fota_context_t *pCtx, const char *host,
			  const char *file, int sec_tag, const char *apn,
			  size_t fragment_size)
{
	int err = -1;

	struct download_client_cfg config = {
		.sec_tag = sec_tag,
		.apn = apn,
		.frag_size_override = fragment_size,
		.set_tls_hostname = (sec_tag != -1),
	};

	if (host == NULL || file == NULL) {
		return -EINVAL;
	}

	hl7800_socket_retries_left = CONFIG_FOTA_SOCKET_RETRIES;

	err = download_client_connect(&hl7800_dlc, host, &config);
	if (err != 0) {
		return err;
	}

	/* if we are starting from a 0 offset, ensure we are starting fresh */
	if (hl7800_file_offset == 0) {
		if (fsu_delete(CONFIG_FOTA_FS_MOUNT, pCtx->file_path) < 0) {
			LOG_INF("HL7800 Firmware Update File Doesn't Exist");
		}
	}
	err = download_client_start(&hl7800_dlc, file, hl7800_file_offset);
	if (err != 0) {
		download_client_disconnect(&hl7800_dlc);
		return err;
	}

	return 0;
}

int hl7800_initiate_modem_update(fota_context_t *pCtx)
{
	int err = -EINVAL;
	int sha_r = 0;
	ssize_t hash_len = 0;
	ssize_t file_size = 0;

	if (pCtx->type != MODEM_IMAGE_TYPE) {
		return -EINVAL;
	}

	/* proceed only if we can get the file size */
	file_size = fsu_get_file_size(CONFIG_FOTA_FS_MOUNT, pCtx->file_path);
	if (file_size > 0) {
		/* initialize all the buffers to default values */
		memset(hl7800_update_expected_hash, 0, FSU_HASH_SIZE);
		memset(hl7800_update_file_hash, 0, FSU_HASH_SIZE);

		LOG_DBG("Computing hash for %s", log_strdup(pCtx->file_path));
		sha_r = fsu_sha256(hl7800_update_file_hash,
				   CONFIG_FOTA_FS_MOUNT, pCtx->file_path,
				   file_size);

		/* only attempt to compare hash values if we were able to compute a hash on the file */
		if (sha_r == 0) {
			hash_len = http_fota_convert_hash(
				pCtx->type, hl7800_update_expected_hash,
				FSU_HASH_SIZE);
			if (hash_len == FSU_HASH_SIZE) {
				/* only attempt an update if we've correctly downloaded the full image */
				if (!memcmp(hl7800_update_expected_hash,
					    hl7800_update_file_hash,
					    FSU_HASH_SIZE)) {
					LOG_INF("Hash values match. Initiating hl7800 modem update.");
					(void)fsu_build_full_name(
						hl7800_update_abs_path,
						sizeof(hl7800_update_abs_path),
						CONFIG_FOTA_FS_MOUNT,
						pCtx->file_path);
					err = mdm_hl7800_update_fw(
						hl7800_update_abs_path);
				}
			}
		}
	}

	/* regardless of the update status, we must delete the file to start over */
	hl7800_file_offset = 0;
	fsu_delete(CONFIG_FOTA_FS_MOUNT, pCtx->file_path);

	return err;
}
/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int
hl7800_download_client_callback(const struct download_client_evt *event)
{
	static bool first_fragment = true;
	static size_t file_size;
	int err;

	if (event == NULL) {
		return -EINVAL;
	}

	switch (event->id) {
	case DOWNLOAD_CLIENT_EVT_FRAGMENT: {
		if (first_fragment) {
			err = download_client_file_size_get(&hl7800_dlc,
							    &file_size);
			if (err == 0) {
				first_fragment = false;
			} else {
				hl7800_fota_send_error_evt(
					FOTA_DOWNLOAD_ERROR_CAUSE_DOWNLOAD_FAILED);
			}
		}
		err = fsu_append(
			CONFIG_FSU_MOUNT_POINT,
			http_fota_get_fs_name(MODEM_IMAGE_TYPE),
			(void *)event->fragment.buf, event->fragment.len);
		if (err < 0) {
			LOG_ERR("fs write error %d", err);
			(void)download_client_disconnect(&hl7800_dlc);
			hl7800_fota_send_error_evt(
				FOTA_DOWNLOAD_ERROR_CAUSE_DOWNLOAD_FAILED);

			return err;
		} else {
			hl7800_file_offset += event->fragment.len;
		}
#ifdef CONFIG_FOTA_DOWNLOAD_PROGRESS_EVT
		if (file_size == 0) {
			LOG_DBG("invalid file size: %d", file_size);
			hl7800_fota_send_error_evt(
				FOTA_DOWNLOAD_ERROR_CAUSE_DOWNLOAD_FAILED);

			return err;
		}
		hl7800_fota_send_progress((hl7800_file_offset * 100) /
					  file_size);
		LOG_DBG("Progress: %d/%d%%", hl7800_file_offset, file_size);
#endif
		break;
	}

	case DOWNLOAD_CLIENT_EVT_DONE:
		download_client_disconnect(&hl7800_dlc);
		hl7800_fota_send_evt(FOTA_DOWNLOAD_EVT_FINISHED);
		first_fragment = true;
		break;

	case DOWNLOAD_CLIENT_EVT_ERROR: {
		/* In case of socket errors we can return 0 to retry/continue,
		 * or non-zero to stop
		 */
		if ((hl7800_socket_retries_left) &&
		    ((event->error == -ENOTCONN) ||
		     (event->error == -ECONNRESET))) {
			LOG_WRN("Download socket error. %d retries left...",
				hl7800_socket_retries_left);
			hl7800_socket_retries_left--;
			/* Fall through and return 0 below to tell
			 * download_client to retry
			 */
		} else {
			download_client_disconnect(&hl7800_dlc);
			LOG_ERR("Download client error");
			first_fragment = true;
			/* Return non-zero to tell download_client to stop */
			hl7800_fota_send_error_evt(
				FOTA_DOWNLOAD_ERROR_CAUSE_DOWNLOAD_FAILED);
			return event->error;
		}
	}
	default:
		break;
	}

	return 0;
}

static void hl7800_fota_send_evt(enum fota_download_evt_id id)
{
	const struct fota_download_evt evt = { .id = id };
	hl7800_fota_callback(&evt);
}

static void hl7800_fota_send_error_evt(enum fota_download_error_cause cause)
{
	/* If we are ending in a download error,
	 * we must delete the file to start over
	 */
	hl7800_file_offset = 0;
	fsu_delete(CONFIG_FOTA_FS_MOUNT,
		   http_fota_get_fs_name(MODEM_IMAGE_TYPE));
	const struct fota_download_evt evt = { .id = FOTA_DOWNLOAD_EVT_ERROR,
					       .cause = cause };
	hl7800_fota_callback(&evt);
}

#ifdef CONFIG_FOTA_DOWNLOAD_PROGRESS_EVT
static void hl7800_fota_send_progress(int progress)
{
	const struct fota_download_evt evt = { .id = FOTA_DOWNLOAD_EVT_PROGRESS,
					       .progress = progress };
	hl7800_fota_callback(&evt);
}
#endif