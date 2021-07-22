/**
 * @file http_fota_shadow.c
 * @brief
 *
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(http_fota_shadow, CONFIG_HTTP_FOTA_TASK_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>
#include <string.h>
#include <stdio.h>

#include "aws.h"
#include "lcz_qrtc.h"
#include "app_version.h"
#include "string_util.h"
#include "file_system_utilities.h"
#include "http_fota_shadow.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
typedef struct fota_shadow_image {
	char running[CONFIG_FSU_MAX_VERSION_SIZE];
	char desired[CONFIG_FSU_MAX_VERSION_SIZE];
	char host[CONFIG_DOWNLOAD_CLIENT_MAX_HOSTNAME_SIZE];
	char file[CONFIG_DOWNLOAD_CLIENT_MAX_FILENAME_SIZE];
	char downloaded_filename[CONFIG_FSU_MAX_FILE_NAME_SIZE];
	char hash[FSU_HASH_SIZE * 2 + 1];
	uint32_t start;
	uint32_t switchover;
	uint32_t error_count;
	const char *name;
	const char *fs_path;
	bool null_desired;
} fota_shadow_image_t;

typedef struct fota_shadow {
	fota_shadow_image_t app;
#ifdef CONFIG_MODEM_HL7800
	fota_shadow_image_t modem;
#endif
	bool json_update_request;
	bool enabled;
} fota_shadow_t;

#define SHADOW_FOTA_START "{\"state\":{\"reported\":{"
#define SHADOW_FOTA_END "}}}"
#define INT_CONVERSION_MAX_STR_LEN 10

/* "app": { "running": "3.0.99", "desired": "3.0.100", "downloadHost": "",
 * "downloadFile": "", "downloadedFilename": "", "hash": "", "start": 0, "switchover": 0 }
 */
#define SHADOW_FOTA_IMAGE_FMT_MAX_CONVERSION_SIZE                              \
	((CONFIG_FSU_MAX_VERSION_SIZE * 2) +                                   \
	 CONFIG_DOWNLOAD_CLIENT_MAX_HOSTNAME_SIZE +                            \
	 CONFIG_DOWNLOAD_CLIENT_MAX_FILENAME_SIZE + (FSU_HASH_SIZE * 2) +      \
	 CONFIG_FSU_MAX_FILE_NAME_SIZE + (2 * INT_CONVERSION_MAX_STR_LEN))

#define SHADOW_FOTA_IMAGE_FMT_STR                                              \
	"{"                                                                    \
	"\"" SHADOW_FOTA_RUNNING_STR "\":\"%s\","                              \
	"\"" SHADOW_FOTA_DESIRED_STR "\":\"%s\","                              \
	"\"" SHADOW_FOTA_DOWNLOAD_HOST_STR "\":\"%s\","                        \
	"\"" SHADOW_FOTA_DOWNLOAD_FILE_STR "\":\"%s\","                        \
	"\"" SHADOW_FOTA_DOWNLOADED_FILENAME_STR "\":\"%s\","                  \
	"\"" SHADOW_FOTA_HASH_STR "\":\"%s\","                                 \
	"\"" SHADOW_FOTA_START_STR "\":%u,"                                    \
	"\"" SHADOW_FOTA_SWITCHOVER_STR "\":%u,"                               \
	"\"" SHADOW_FOTA_ERROR_STR "\":%u"                                     \
	"}"

#ifdef CONFIG_MODEM_HL7800
#define SHADOW_FOTA_FMT_STR                                                    \
	SHADOW_FOTA_START                                                      \
	"\"" SHADOW_FOTA_APP_STR "\":" SHADOW_FOTA_IMAGE_FMT_STR ","           \
	"\"" SHADOW_FOTA_MODEM_STR                                             \
	"\":" SHADOW_FOTA_IMAGE_FMT_STR SHADOW_FOTA_END
#else
#define SHADOW_FOTA_FMT_STR                                                    \
	SHADOW_FOTA_START                                                      \
	"\"" SHADOW_FOTA_APP_STR "\":" SHADOW_FOTA_IMAGE_FMT_STR SHADOW_FOTA_END
#endif

#define SHADOW_FOTA_FMT_STR_MAX_CONVERSION_SIZE                                \
	(sizeof(SHADOW_FOTA_FMT_STR) +                                         \
	 (2 * SHADOW_FOTA_IMAGE_FMT_MAX_CONVERSION_SIZE) +                     \
	 INT_CONVERSION_MAX_STR_LEN)

/* The delta topic is processed.  However, desired should still be cleared when
 * it has been processed.
 */
#define SHADOW_FOTA_NULL_DESIRED_FMT_STR_CONVERSION_SIZE (64)
#define SHADOW_FOTA_NULL_DESIRED_FMT_STR                                       \
	"{\"state\":{\"desired\":{\"%s\":null}}}"

#ifdef CONFIG_MODEM_HL7800
#define MODEM_IMAGE_PREFIX "HL7800"
#endif

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
K_MUTEX_DEFINE(fota_shadow_mutex);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static fota_shadow_t fota_shadow;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static fota_shadow_image_t *get_image_ptr(enum fota_image_type type);
static void fota_shadow_handler(void);
static void fota_null_desired_image_handler(enum fota_image_type type);
static int fota_null_desired_handler(const char *name);
static bool set_shadow_str(char *dest, size_t dest_size, const char *src,
			   size_t src_len);
static bool set_shadow_uint32(uint32_t *dest, uint32_t value);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void http_fota_shadow_init(void)
{
	strcpy(fota_shadow.app.running, APP_VERSION_STRING);

	fota_shadow.app.name = SHADOW_FOTA_APP_STR;

	fota_shadow.json_update_request = true;
}

#ifdef CONFIG_MODEM_HL7800
void http_fota_modem_shadow_init(const char *modem_fs_path)
{
	fota_shadow.modem.name = SHADOW_FOTA_MODEM_STR;
	fota_shadow.modem.fs_path = modem_fs_path;
}
#endif

void http_fota_enable_shadow_generation(void)
{
	fota_shadow.enabled = true;
	fota_shadow.json_update_request = true;
}

void http_fota_disable_shadow_generation(void)
{
	fota_shadow.enabled = false;
}

bool http_fota_shadow_update_handler(void)
{
	bool update_in_progress = fota_shadow.json_update_request;

	if (!fota_shadow.enabled) {
		return false;
	}

	fota_shadow_handler();
	fota_null_desired_image_handler(APP_IMAGE_TYPE);
#ifdef CONFIG_MODEM_HL7800
	fota_null_desired_image_handler(MODEM_IMAGE_TYPE);
#endif

	return update_in_progress;
}

const char *http_fota_get_image_name(enum fota_image_type type)
{
	switch (type) {
	case APP_IMAGE_TYPE:
		return fota_shadow.app.name;
#ifdef CONFIG_MODEM_HL7800
	case MODEM_IMAGE_TYPE:
		return fota_shadow.modem.name;
#endif
	default:
		return "?name?";
	}
}

void http_fota_set_running_version(enum fota_image_type type, const char *p,
				   size_t length)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}
	/* Strip off prefix */
	size_t offset = 0;
#ifdef CONFIG_MODEM_HL7800
	if (type == MODEM_IMAGE_TYPE) {
		if (strstr(p, MODEM_IMAGE_PREFIX) != NULL) {
			offset = strlen(MODEM_IMAGE_PREFIX);
		}
	}
#endif
	/* This isn't printed because it isn't set from the shadow */
	set_shadow_str(pImg->running, sizeof(pImg->running), p + offset + 1,
		       length - offset);
}

void http_fota_set_desired_version(enum fota_image_type type, const char *p,
				   size_t length)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	if (set_shadow_str(pImg->desired, sizeof(pImg->desired), p, length)) {
		LOG_DBG("%s desired version: %s", log_strdup(pImg->name),
			log_strdup(pImg->desired));
	}
	/* Don't set flag when reading shadow after a reset */
	pImg->null_desired = fota_shadow.enabled;
}

void http_fota_set_download_host(enum fota_image_type type, const char *p,
				 size_t length)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	if (set_shadow_str(pImg->host, sizeof(pImg->host), p, length)) {
		LOG_DBG("%s host name: %s", log_strdup(pImg->name),
			log_strdup(pImg->host));
	}
	/* Don't set flag when reading shadow after a reset */
	pImg->null_desired = fota_shadow.enabled;
}

const char *http_fota_get_download_host(enum fota_image_type type)
{
	switch (type) {
	case APP_IMAGE_TYPE:
		return fota_shadow.app.host;
#ifdef CONFIG_MODEM_HL7800
	case MODEM_IMAGE_TYPE:
		return fota_shadow.modem.host;
#endif
	default:
		return "?name?";
	}
}

void http_fota_set_download_file(enum fota_image_type type, const char *p,
				 size_t length)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	if (set_shadow_str(pImg->file, sizeof(pImg->file), p, length)) {
		LOG_DBG("%s file name: %s", log_strdup(pImg->name),
			log_strdup(pImg->file));
	}
	/* Don't set flag when reading shadow after a reset */
	pImg->null_desired = fota_shadow.enabled;
}

const char *http_fota_get_download_file(enum fota_image_type type)
{
	switch (type) {
	case APP_IMAGE_TYPE:
		return fota_shadow.app.file;
#ifdef CONFIG_MODEM_HL7800
	case MODEM_IMAGE_TYPE:
		return fota_shadow.modem.file;
#endif
	default:
		return "?name?";
	}
}

void http_fota_set_downloaded_filename(enum fota_image_type type, const char *p,
				       size_t length)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	/* This value could be updated when the shadow is read, but in this
	 * application it will only be updated by the fota state machine.
	 */
	if (set_shadow_str(pImg->downloaded_filename,
			   sizeof(pImg->downloaded_filename), p, length)) {
		LOG_DBG("%s downloaded filename: %s", log_strdup(pImg->name),
			log_strdup(pImg->downloaded_filename));
	}
}

const char *http_fota_get_downloaded_filename(enum fota_image_type type)
{
	switch (type) {
	case APP_IMAGE_TYPE:
		return fota_shadow.app.downloaded_filename;
#ifdef CONFIG_MODEM_HL7800
	case MODEM_IMAGE_TYPE:
		return fota_shadow.modem.downloaded_filename;
#endif
	default:
		return "?name?";
	}
}

void http_fota_set_start(enum fota_image_type type, uint32_t value)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	if (set_shadow_uint32(&pImg->start, value)) {
		LOG_DBG("%s start: %u", log_strdup(pImg->name), pImg->start);
	}
	pImg->null_desired = fota_shadow.enabled;
}

void http_fota_set_switchover(enum fota_image_type type, uint32_t value)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	if (set_shadow_uint32(&pImg->switchover, value)) {
		LOG_DBG("%s switchover: %u", log_strdup(pImg->name),
			pImg->switchover);
	}
	pImg->null_desired = fota_shadow.enabled;
}

void http_fota_set_error_count(enum fota_image_type type, uint32_t value)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	if (set_shadow_uint32(&pImg->error_count, value)) {
		LOG_DBG("%s error count: %u", log_strdup(pImg->name),
			pImg->error_count);
	}
	pImg->null_desired = fota_shadow.enabled;
}

void http_fota_increment_error_count(enum fota_image_type type)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	pImg->error_count += 1;
	fota_shadow.json_update_request = true;
	LOG_DBG("%s error count: %u", log_strdup(pImg->name),
		pImg->error_count);
}

bool http_fota_request(enum fota_image_type type)
{
	bool request = false;
	fota_shadow_image_t *p = get_image_ptr(type);
	if (p == NULL) {
		return request;
	}
	k_mutex_lock(&fota_shadow_mutex, K_FOREVER);
	if ((strlen(p->desired) != 0) && (strlen(p->running) != 0) &&
	    (strlen(p->host) != 0) && (strlen(p->file) != 0)) {
		if (strcmp(p->desired, p->running) != 0) {
			if (lcz_qrtc_get_epoch() >= p->start) {
				request = true;
			}
		}
	}
	k_mutex_unlock(&fota_shadow_mutex);
	return request;
}

bool http_fota_ready(enum fota_image_type type)
{
	bool ready = false;
	fota_shadow_image_t *p = get_image_ptr(type);
	if (p == NULL) {
		return false;
	}
	k_mutex_lock(&fota_shadow_mutex, K_FOREVER);
	if ((strlen(p->desired) != 0) && (strlen(p->running) != 0)) {
		if (strcmp(p->desired, p->running) != 0) {
			if (lcz_qrtc_get_epoch() >= p->switchover) {
				ready = true;
			}
		}
	}
	k_mutex_unlock(&fota_shadow_mutex);
	return ready;
}

#ifdef CONFIG_MODEM_HL7800
bool http_fota_modem_install_complete(void)
{
	bool match = false;
	fota_shadow_image_t *p = get_image_ptr(MODEM_IMAGE_TYPE);
	k_mutex_lock(&fota_shadow_mutex, K_FOREVER);
	if ((strlen(p->desired) != 0) && (strlen(p->running) != 0)) {
		if (strcmp(p->desired, p->running) == 0) {
			match = true;
		}
	}
	k_mutex_unlock(&fota_shadow_mutex);
	return match;
}
#endif

bool http_fota_abort(enum fota_image_type type)
{
	bool abort = false;
	fota_shadow_image_t *p = get_image_ptr(type);
	if (p == NULL) {
		return false;
	}
	k_mutex_lock(&fota_shadow_mutex, K_FOREVER);
	if ((strlen(p->desired) != 0) && (strlen(p->running) != 0)) {
		/* Handles the case where the desired image is changed back to
		 * what is already running while a download is in progress.
		 */
		if (strcmp(p->desired, p->running) == 0) {
			abort = true;
		}
	}
	k_mutex_unlock(&fota_shadow_mutex);
	return abort;
}

const char *http_fota_get_hash(enum fota_image_type type)
{
	switch (type) {
	case APP_IMAGE_TYPE:
		return fota_shadow.app.hash;
#ifdef CONFIG_MODEM_HL7800
	case MODEM_IMAGE_TYPE:
		return fota_shadow.modem.hash;
#endif
	default:
		return "?name?";
	}
}

size_t http_fota_convert_hash(enum fota_image_type type, uint8_t *buf,
			      size_t buf_len)
{
	size_t ret = 0;

	if (buf != NULL) {
		if ((type == APP_IMAGE_TYPE) &&
		    (fota_shadow.app.hash != NULL)) {
			ret = hex2bin(fota_shadow.app.hash, FSU_HASH_SIZE * 2,
				      buf, buf_len);
		}
#ifdef CONFIG_MODEM_HL7800
		else if ((type == MODEM_IMAGE_TYPE) &&
			 (fota_shadow.modem.hash != NULL)) {
			ret = hex2bin(fota_shadow.modem.hash, FSU_HASH_SIZE * 2,
				      buf, buf_len);
		}
#endif
	}

	return ret;
}

void http_fota_set_hash(enum fota_image_type type, const char *p, size_t length)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	if (set_shadow_str(pImg->hash, sizeof(pImg->hash), p, length)) {
		LOG_DBG("%s image hash: %s", log_strdup(pImg->name),
			log_strdup(pImg->hash));
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static fota_shadow_image_t *get_image_ptr(enum fota_image_type type)
{
	switch (type) {
	case APP_IMAGE_TYPE:
		return &fota_shadow.app;
#ifdef CONFIG_MODEM_HL7800
	case MODEM_IMAGE_TYPE:
		return &fota_shadow.modem;
#endif
	default:
		return NULL;
	}
}

static bool set_shadow_str(char *dest, size_t dest_size, const char *src,
			   size_t src_len)
{
	bool updated = false;
	k_mutex_lock(&fota_shadow_mutex, K_FOREVER);
	/* If strings are different lengths, then they can't match. */
	if (strncmp(dest, src, MAX(strlen(dest), src_len)) != 0) {
		/* Strings from the jsmn parser aren't null terminated. */
		memset(dest, 0, dest_size);
		strncpy(dest, src, MIN(dest_size - 1, src_len));
		fota_shadow.json_update_request = true;
		updated = true;
	}
	k_mutex_unlock(&fota_shadow_mutex);
	return updated;
}

static bool set_shadow_uint32(uint32_t *dest, uint32_t value)
{
	bool updated = false;
	k_mutex_lock(&fota_shadow_mutex, K_FOREVER);
	if (*dest != value) {
		*dest = value;
		fota_shadow.json_update_request = true;
		updated = true;
	}
	k_mutex_unlock(&fota_shadow_mutex);
	return updated;
}

static void fota_shadow_handler(void)
{
	int rc = -1;
	if (fota_shadow.json_update_request) {
		size_t size = SHADOW_FOTA_FMT_STR_MAX_CONVERSION_SIZE;
		char *msg = k_calloc(size, sizeof(char));
		if (msg == NULL) {
			LOG_ERR("Unable to allocate buffer for shadow");
			return;
		}
		snprintf(msg, size, SHADOW_FOTA_FMT_STR,
			 fota_shadow.app.running, fota_shadow.app.desired,
			 fota_shadow.app.host, fota_shadow.app.file,
			 fota_shadow.app.downloaded_filename,
			 fota_shadow.app.hash, fota_shadow.app.start,
			 fota_shadow.app.switchover, fota_shadow.app.error_count
#ifdef CONFIG_MODEM_HL7800
			 ,
			 fota_shadow.modem.running, fota_shadow.modem.desired,
			 fota_shadow.modem.host, fota_shadow.modem.file,
			 fota_shadow.modem.downloaded_filename,
			 fota_shadow.modem.hash, fota_shadow.modem.start,
			 fota_shadow.modem.switchover,
			 fota_shadow.modem.error_count
#endif
		);

#ifdef CONFIG_BLUEGRASS
		LOG_DBG("Update FOTA shadow");
		rc = awsSendData(msg, GATEWAY_TOPIC);
#endif
		if (rc < 0) {
			LOG_ERR("Could not send FOTA state to AWS");
		} else {
			fota_shadow.json_update_request = false;
		}
		k_free(msg);
	}
}

/* If any value in the image obj is modified its desired shadow is nulled. */
static void fota_null_desired_image_handler(enum fota_image_type type)
{
	fota_shadow_image_t *p = get_image_ptr(type);
	if (p == NULL) {
		return;
	}

	if (p->null_desired) {
		int rc = fota_null_desired_handler(p->name);
		if (rc >= 0) {
			p->null_desired = false;
		}
	}
}

static int fota_null_desired_handler(const char *name)
{
	int rc = -1;
	size_t size = strlen(SHADOW_FOTA_NULL_DESIRED_FMT_STR) +
		      SHADOW_FOTA_NULL_DESIRED_FMT_STR_CONVERSION_SIZE;
	char *msg = k_calloc(size, sizeof(char));
	if (msg == NULL) {
		LOG_ERR("Allocation failure: FOTA null desired");
		return -ENOMEM;
	}
	snprintf(msg, size, SHADOW_FOTA_NULL_DESIRED_FMT_STR, name);

#ifdef CONFIG_BLUEGRASS
	LOG_DBG("Set %s FOTA desired null", name);
	rc = awsSendData(msg, GATEWAY_TOPIC);
#endif

	if (rc < 0) {
		LOG_ERR("Could not set FOTA %s desired to null",
			log_strdup(name));
	}
	k_free(msg);
	return rc;
}
