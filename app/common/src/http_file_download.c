/**
 * @file http_file_download.c
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(http_file_download, CONFIG_HTTP_FILE_DOWNLOAD_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <init.h>
#include <net/download_client.h>

#include "http_file_download.h"

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct {
	struct k_sem idle;
	struct k_sem done;
	int status;
	int remaining_retries;
	struct download_client dlc;
	struct hfd_context *context;
} hfd;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void hfd_done(int status);

static int hfd_client_callback(const struct download_client_evt *event);

static int fragment_handler(const struct download_client_evt *event);

/******************************************************************************/
/* Init                                                                       */
/******************************************************************************/
static int hfd_init(const struct device *device)
{
	int r = 0;

	r = download_client_init(&hfd.dlc, hfd_client_callback);
	if (r != 0) {
		LOG_ERR("Could not init HTTP file download %d", r);
	}

	k_sem_init(&hfd.done, 0, 1);
	k_sem_init(&hfd.idle, 1, 1);

	return r;
}

SYS_INIT(hfd_init, APPLICATION, 99);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int http_file_download(struct hfd_context *context)
{
	if (context == NULL || strlen(context->host) == 0 ||
	    strlen(context->host_file_name) == 0 ||
	    strlen(context->abs_file_name) == 0) {
		return -EINVAL;
	}

	if (k_sem_take(&hfd.idle, K_NO_WAIT) != 0) {
		return -EBUSY;
	}

	struct download_client_cfg config = {
		.sec_tag = context->sec_tag,
		.frag_size_override = 0,
		.set_tls_hostname = (context->sec_tag != -1),
	};

	k_sem_reset(&hfd.done);

	hfd.context = context;
	hfd.remaining_retries = CONFIG_HFD_SOCKET_RETRIES;
	hfd.status = 0;

	do {
		hfd.status = download_client_connect(&hfd.dlc, hfd.context->host,
					    &config);
		if (hfd.status != 0) {
			LOG_ERR("Download client unable to connect: %d", hfd.status);
			break;
		}

		if (hfd.context->offset == 0) {
			(void)fsu_delete_abs(context->abs_file_name);
			hfd.context->host_file_size = 0;
		}

		hfd.status = download_client_start(&hfd.dlc, hfd.context->host_file_name,
					  hfd.context->offset);
		if (hfd.status != 0) {
			download_client_disconnect(&hfd.dlc);
			break;
		}

		k_sem_take(&hfd.done, K_FOREVER);

	} while (0);

	k_sem_give(&hfd.idle);

	return hfd.status;
}

int http_file_download_valid_hash(struct hfd_context *context)
{
	int r = -EINVAL;
	int sha_r = 0;
	ssize_t file_size;

	do {
		file_size = fsu_get_file_size_abs(context->abs_file_name);
		if (file_size <= 0) {
			LOG_WRN("Unable to get file size");
			break;
		}

		memset(context->file_hash, 0, sizeof(context->file_hash));

		LOG_DBG("Computing hash for %s", context->abs_file_name);
		sha_r = fsu_sha256_abs(context->file_hash,
				       context->abs_file_name, file_size);
		if (sha_r != 0) {
			LOG_ERR("Unable to compute hash");
			break;
		}

		if (memcmp(context->expected_hash, context->file_hash,
			   FSU_HASH_SIZE) != 0) {
			LOG_ERR("Expected has doesn't match file hash");
			break;
		}

		r = file_size;

	} while (0);

	return r;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void hfd_done(int status)
{
	LOG_DBG("HTTP File Download status: %d", status);

	hfd.status = status;
	k_sem_give(&hfd.done);
}

static int hfd_client_callback(const struct download_client_evt *event)
{
	int r = 0;

	if (event == NULL) {
		return -EINVAL;
	}

	switch (event->id) {
	case DOWNLOAD_CLIENT_EVT_FRAGMENT:
		r = fragment_handler(event);
		break;

	case DOWNLOAD_CLIENT_EVT_DONE:
		download_client_disconnect(&hfd.dlc);
		hfd_done(0);
		break;

	case DOWNLOAD_CLIENT_EVT_ERROR:
		/* In case of socket errors we can return 0 to retry/continue,
		 * or non-zero to stop
		 */
		if ((hfd.remaining_retries) &&
		    ((event->error == -ENOTCONN) ||
		     (event->error == -ECONNRESET))) {
			LOG_WRN("Download socket error. %d retries left",
				hfd.remaining_retries);
			hfd.remaining_retries += 1;
			r = 0;
		} else {
			LOG_ERR("Download client error");
			download_client_disconnect(&hfd.dlc);
			hfd_done(event->error);
			r = event->error;
		}
		break;

	default:
		LOG_ERR("Unhandled callback");
		hfd_done(-ENOTSUP);
		break;
	}

	return r;
}

static int fragment_handler(const struct download_client_evt *event)
{
	int r = -EPERM;

	do {
		if (hfd.context->offset == 0) {
			r = download_client_file_size_get(
				&hfd.dlc, &hfd.context->host_file_size);
			if (r != 0) {
				LOG_ERR("Unable to get file size");
				hfd_done(r);
				break;
			}
		}

		if (hfd.context->host_file_size == 0) {
			LOG_ERR("invalid file size: %d",
				hfd.context->host_file_size);
			r = -EINVAL;
			hfd_done(r);
			break;
		}

		r = fsu_append_abs(hfd.context->abs_file_name,
				   (void *)event->fragment.buf,
				   event->fragment.len);
		if (r < 0 || r != event->fragment.len) {
			LOG_ERR("fs append error %d", r);
			download_client_disconnect(&hfd.dlc);
			hfd_done(r);
			break;
		} else {
			r = 0;
			hfd.context->offset += event->fragment.len;
		}

	} while (0);

	return r;
}