/**
 * @file coap_fota_shadow.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(coap_fota_shadow, LOG_LEVEL_DBG);

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
#include "coap_fota_shadow.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
typedef struct fota_shadow_image {
	char running[CONFIG_FSU_MAX_VERSION_SIZE];
	char desired[CONFIG_FSU_MAX_VERSION_SIZE];
	char desired_filename[CONFIG_FSU_MAX_FILE_NAME_SIZE];
	char downloaded_filename[CONFIG_FSU_MAX_FILE_NAME_SIZE];
	uint32_t start;
	uint32_t switchover;
	uint32_t error_count;
	const char *name;
	const char *fs_path;
	bool null_desired;
} fota_shadow_image_t;

typedef struct fota_shadow {
	fota_shadow_image_t app;
	fota_shadow_image_t modem;
	char bridge[CONFIG_COAP_FOTA_MAX_PARAMETER_SIZE];
	uint32_t blocksize;
	bool null_host;
	bool null_blocksize;
	bool json_update_request;
	bool enabled;
} fota_shadow_t;

#define SHADOW_FOTA_START "{\"state\":{\"reported\":{"
#define SHADOW_FOTA_END "}}}"
#define INT_CONVERSION_MAX_STR_LEN 10

/* "app": { "desired": "5.0.0", "running": "3.0.99",
 * "downloadedFilename": "", "start": 0, "switchover": 0, "desiredFilename":"" }
 */
#define SHADOW_FOTA_IMAGE_FMT_MAX_CONVERSION_SIZE                              \
	((CONFIG_FSU_MAX_VERSION_SIZE * 2) +                                   \
	 (CONFIG_FSU_MAX_FILE_NAME_SIZE * 2) + 3 * INT_CONVERSION_MAX_STR_LEN)

#define SHADOW_FOTA_IMAGE_FMT_STR                                              \
	"{"                                                                    \
	"\"" SHADOW_FOTA_RUNNING_STR "\":\"%s\","                              \
	"\"" SHADOW_FOTA_DESIRED_STR "\":\"%s\","                              \
	"\"" SHADOW_FOTA_DESIRED_FILENAME_STR "\":\"%s\","                     \
	"\"" SHADOW_FOTA_DOWNLOADED_FILENAME_STR "\":\"%s\","                  \
	"\"" SHADOW_FOTA_START_STR "\":%u,"                                    \
	"\"" SHADOW_FOTA_SWITCHOVER_STR "\":%u,"                               \
	"\"" SHADOW_FOTA_ERROR_STR "\":%u"                                     \
	"}"

#define SHADOW_FOTA_FMT_STR                                                    \
	SHADOW_FOTA_START                                                      \
	"\"" SHADOW_FOTA_APP_STR "\":" SHADOW_FOTA_IMAGE_FMT_STR ","           \
	"\"" SHADOW_FOTA_MODEM_STR "\":" SHADOW_FOTA_IMAGE_FMT_STR ","         \
	"\"" SHADOW_FOTA_BRIDGE_STR "\":\"%s\","                               \
	"\"" SHADOW_FOTA_PRODUCT_STR "\":\"%s\","                              \
	"\"" SHADOW_FOTA_BLOCKSIZE_STR "\":%u" SHADOW_FOTA_END

#define SHADOW_FOTA_FMT_STR_MAX_CONVERSION_SIZE                                \
	(sizeof(SHADOW_FOTA_FMT_STR) +                                         \
	 (2 * SHADOW_FOTA_IMAGE_FMT_MAX_CONVERSION_SIZE) +                     \
	 (2 * CONFIG_COAP_FOTA_MAX_PARAMETER_SIZE) +                           \
	 INT_CONVERSION_MAX_STR_LEN)

/* The delta topic is processed.  However, desired should still be cleared when
 * it has been processed.
 */
#define SHADOW_FOTA_NULL_DESIRED_FMT_STR_CONVERSION_SIZE (64)
#define SHADOW_FOTA_NULL_DESIRED_FMT_STR                                       \
	"{\"state\":{\"desired\":{\"%s\":null}}}"

#define MODEM_IMAGE_PREFIX "HL7800"

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
static void fota_null_desired_host_handler(void);
static void fota_null_desired_blocksize_handler(void);
static int fota_null_desired_handler(const char *name);
static bool set_shadow_str(char *dest, size_t dest_size, const char *src,
			   size_t src_len);
static bool set_shadow_uint32(uint32_t *dest, uint32_t value);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void coap_fota_shadow_init(const char *app_fs_path, const char *modem_fs_path)
{
	strcpy(fota_shadow.bridge, CONFIG_COAP_FOTA_DEFAULT_BRIDGE);

	strcpy(fota_shadow.app.running, APP_VERSION_STRING);

	fota_shadow.app.name = SHADOW_FOTA_APP_STR;
	fota_shadow.app.fs_path = app_fs_path;
	fota_shadow.modem.name = SHADOW_FOTA_MODEM_STR;
	fota_shadow.modem.fs_path = modem_fs_path;

	fota_shadow.blocksize = CONFIG_COAP_FOTA_MAX_BLOCK_SIZE;

	fota_shadow.json_update_request = true;
}

void coap_fota_enable_shadow_generation(void)
{
	fota_shadow.enabled = true;
	fota_shadow.json_update_request = true;
}

void coap_fota_disable_shadow_generation(void)
{
	fota_shadow.enabled = false;
}

void coap_fota_shadow_update_handler(void)
{
#ifdef CONFIG_BLUEGRASS
	if (!awsConnected() || !fota_shadow.enabled) {
		return;
	}
#else
	return;
#endif

	fota_shadow_handler();
	fota_null_desired_image_handler(APP_IMAGE_TYPE);
	fota_null_desired_image_handler(MODEM_IMAGE_TYPE);
	fota_null_desired_host_handler();
	fota_null_desired_blocksize_handler();
}

const char *coap_fota_get_image_name(enum fota_image_type type)
{
	switch (type) {
	case APP_IMAGE_TYPE:
		return fota_shadow.app.name;
	case MODEM_IMAGE_TYPE:
		return fota_shadow.modem.name;
	default:
		return "?name?";
	}
}

void coap_fota_set_running_version(enum fota_image_type type, const char *p,
				   size_t length)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}
	/* Strip off prefix */
	size_t offset = 0;
	if (type == MODEM_IMAGE_TYPE) {
		if (strstr(p, MODEM_IMAGE_PREFIX) != NULL) {
			offset = strlen(MODEM_IMAGE_PREFIX);
		}
	}
	/* This isn't set from the shadow */
	fota_shadow.json_update_request =
		set_shadow_str(pImg->running, sizeof(pImg->running),
			       p + offset + 1, length - offset);
}

void coap_fota_set_desired_version(enum fota_image_type type, const char *p,
				   size_t length)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	if (set_shadow_str(pImg->desired, sizeof(pImg->desired), p, length)) {
		fota_shadow.json_update_request = true;
		LOG_DBG("%s desired image: %s", log_strdup(pImg->name),
			log_strdup(pImg->desired));
	}
	/* Don't set flag when reading shadow after a reset */
	pImg->null_desired = fota_shadow.enabled;
}

void coap_fota_set_desired_filename(enum fota_image_type type, const char *p,
				    size_t length)
{
	fota_shadow_image_t *pImg = get_image_ptr(type);
	if (pImg == NULL) {
		return;
	}

	if (set_shadow_str(pImg->desired_filename,
			   sizeof(pImg->desired_filename), p, length)) {
		fota_shadow.json_update_request = true;
		LOG_DBG("%s desired filename: %s", log_strdup(pImg->name),
			log_strdup(pImg->desired_filename));
	}
	/* Don't set flag when reading shadow after a reset */
	pImg->null_desired = fota_shadow.enabled;
}

void coap_fota_set_downloaded_filename(enum fota_image_type type, const char *p,
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
		fota_shadow.json_update_request = true;
		LOG_DBG("%s downloaded filename: %s", log_strdup(pImg->name),
			log_strdup(pImg->downloaded_filename));
	}
}

void coap_fota_set_start(enum fota_image_type type, uint32_t value)
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

void coap_fota_set_switchover(enum fota_image_type type, uint32_t value)
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

void coap_fota_set_error_count(enum fota_image_type type, uint32_t value)
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

void coap_fota_increment_error_count(enum fota_image_type type)
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

void coap_fota_set_host(const char *p, size_t length)
{
	if (set_shadow_str(fota_shadow.bridge, sizeof(fota_shadow.bridge), p,
			   length)) {
		fota_shadow.json_update_request = true;
		LOG_DBG("fota host name: %s", log_strdup(fota_shadow.bridge));
	}
	/* Don't set flag when reading shadow after a reset */
	fota_shadow.null_host = fota_shadow.enabled;
}

void coap_fota_set_blocksize(uint32_t value)
{
	k_mutex_lock(&fota_shadow_mutex, K_FOREVER);
	if (fota_shadow.blocksize != value) {
		fota_shadow.blocksize =
			MIN(value, CONFIG_COAP_FOTA_MAX_BLOCK_SIZE);
		fota_shadow.json_update_request = true;
		LOG_DBG("blocksize: %u", fota_shadow.blocksize);
	}
	fota_shadow.null_blocksize = fota_shadow.enabled;
	k_mutex_unlock(&fota_shadow_mutex);
}

bool coap_fota_request(enum fota_image_type type)
{
	bool request = false;
	fota_shadow_image_t *p = get_image_ptr(type);
	if (p == NULL) {
		return request;
	}
	k_mutex_lock(&fota_shadow_mutex, K_FOREVER);
	if ((strlen(p->desired) != 0) && (strlen(p->running) != 0) &&
	    (strlen(p->desired_filename) != 0)) {
		if (strcmp(p->desired, p->running) != 0) {
			if (lcz_qrtc_get_epoch() >= p->start) {
				request = true;
			}
		}
	}
	k_mutex_unlock(&fota_shadow_mutex);
	return request;
}

bool coap_fota_ready(enum fota_image_type type)
{
	bool ready = false;
	fota_shadow_image_t *p = get_image_ptr(type);
	if (p == NULL) {
		return false;
	}
	k_mutex_lock(&fota_shadow_mutex, K_FOREVER);
	if ((strlen(p->desired) != 0) && (strlen(p->running) != 0) &&
	    (strlen(p->downloaded_filename) != 0)) {
		if (strcmp(p->desired, p->running) != 0) {
			if (strcmp(p->desired_filename,
				   p->downloaded_filename) == 0) {
				if (lcz_qrtc_get_epoch() >= p->switchover) {
					ready = true;
				}
			}
		}
	}
	k_mutex_unlock(&fota_shadow_mutex);
	return ready;
}

bool coap_fota_modem_install_complete(void)
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

bool coap_fota_abort(enum fota_image_type type)
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
		/* Handle the case in which the desired file name is changed
		 * when a download is already in progress.
		 */
		if (strcmp(p->desired_filename, p->downloaded_filename) != 0) {
			abort = true;
		}
	}
	k_mutex_unlock(&fota_shadow_mutex);
	return abort;
}

void coap_fota_populate_query(enum fota_image_type type,
			      coap_fota_query_t *query)
{
	fota_shadow_image_t *p = get_image_ptr(type);
	if (p == NULL) {
		return;
	}

	memset(query, 0, sizeof(coap_fota_query_t));

	k_mutex_lock(&fota_shadow_mutex, K_FOREVER);
	strcpy(query->domain, fota_shadow.bridge);
	query->port = CONFIG_COAP_FOTA_PORT;
	query->path = CONFIG_COAP_FOTA_PATH;
	query->product = CONFIG_COAP_FOTA_PRODUCT;
	query->image = p->name;
	query->fs_path = p->fs_path;
	strcpy(query->version, p->desired);
	strcpy(query->filename, p->desired_filename);
	query->block_size = fota_shadow.blocksize;
	query->dtls = 1;
	k_mutex_unlock(&fota_shadow_mutex);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static fota_shadow_image_t *get_image_ptr(enum fota_image_type type)
{
	switch (type) {
	case APP_IMAGE_TYPE:
		return &fota_shadow.app;
	case MODEM_IMAGE_TYPE:
		return &fota_shadow.modem;
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
	}
	k_mutex_unlock(&fota_shadow_mutex);
	return updated;
}

static void fota_shadow_handler(void)
{
	const char product[CONFIG_COAP_FOTA_MAX_PARAMETER_SIZE] =
		CONFIG_COAP_FOTA_PRODUCT;
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
			 fota_shadow.app.desired_filename,
			 fota_shadow.app.downloaded_filename,
			 fota_shadow.app.start, fota_shadow.app.switchover,
			 fota_shadow.app.error_count, fota_shadow.modem.running,
			 fota_shadow.modem.desired,
			 fota_shadow.modem.desired_filename,
			 fota_shadow.modem.downloaded_filename,
			 fota_shadow.modem.start, fota_shadow.modem.switchover,
			 fota_shadow.modem.error_count, fota_shadow.bridge,
			 product, fota_shadow.blocksize);

#ifdef CONFIG_BLUEGRASS
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

static void fota_null_desired_host_handler(void)
{
	if (fota_shadow.null_host) {
		int rc = fota_null_desired_handler(SHADOW_FOTA_BRIDGE_STR);
		if (rc >= 0) {
			fota_shadow.null_host = false;
		}
	}
}

static void fota_null_desired_blocksize_handler(void)
{
	if (fota_shadow.null_blocksize) {
		int rc = fota_null_desired_handler(SHADOW_FOTA_BLOCKSIZE_STR);
		if (rc >= 0) {
			fota_shadow.null_blocksize = false;
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
		return -ENOMEM;
	}
	snprintf(msg, size, SHADOW_FOTA_NULL_DESIRED_FMT_STR, name);

#ifdef CONFIG_BLUEGRASS
	rc = awsSendData(msg, GATEWAY_TOPIC);
#endif

	if (rc < 0) {
		LOG_ERR("Could not set FOTA %s desired to null",
			log_strdup(name));
	}
	k_free(msg);
	return rc;
}
