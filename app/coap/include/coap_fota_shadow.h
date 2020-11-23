/**
 * @file coap_fota_shadow.h
 * @brief The shadow provides information to Bluegrass and allows
 * firmware versions to be selected.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __COAP_FOTA_SHADOW_H__
#define __COAP_FOTA_SHADOW_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <data/json.h>
#include <stddef.h>

#include "coap_fota_query.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
enum fota_image_type { APP_IMAGE_TYPE = 0, MODEM_IMAGE_TYPE };

/* clang-format off */
#define SHADOW_FOTA_APP_STR                  "app"
#define SHADOW_FOTA_MODEM_STR                "hl7800"
#define SHADOW_FOTA_RUNNING_STR              "running"
#define SHADOW_FOTA_DESIRED_STR              "desired"
#define SHADOW_FOTA_DESIRED_FILENAME_STR     "desiredFilename"
#define SHADOW_FOTA_DOWNLOADED_FILENAME_STR  "downloadedFilename"
#define SHADOW_FOTA_START_STR                "start"
#define SHADOW_FOTA_SWITCHOVER_STR           "switchover"
#define SHADOW_FOTA_BRIDGE_STR               "fwBridge"
#define SHADOW_FOTA_PRODUCT_STR              "fwProduct"
#define SHADOW_FOTA_BLOCKSIZE_STR            "fwBlockSize"
#define SHADOW_FOTA_ERROR_STR                "errorCount"
/* clang-format on */

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Sets running app from version string. Sets image names.
 *
 * @param abs_fs_path is the file system path for application images
 * @param modem_fs_path is the file system path for modem images.
 */
void coap_fota_shadow_init(const char *app_fs_path, const char *modem_fs_path);

/**
 * @brief Enable shadow generation
 *
 * @note Shadow generation should not be enabled until after get
 * accepted has been processed because previously set values are read
 * from the shadow.
 */
void coap_fota_enable_shadow_generation(void);

/**
 * @brief Disable shadow generation
 */
void coap_fota_disable_shadow_generation(void);

/**
 * @brief Publish shadow information for FOTA to AWS/Bluegrass.
 *
 * @note Assumes AWS messages can be sent at any time.
 *
 */
void coap_fota_shadow_update_handler(void);

/**
 * @brief Set the version that is currently running.
 */
void coap_fota_set_running_version(enum fota_image_type type, const char *p,
				   size_t length);

/**
 * @brief Set image version should be downloaded and run.
 */
void coap_fota_set_desired_version(enum fota_image_type type, const char *p,
				   size_t length);

/**
 * @brief Set image filename that should be downloaded and run.
 */
void coap_fota_set_desired_filename(enum fota_image_type type, const char *p,
				    size_t length);

/**
 * @brief Set image filename that was downloaded.
 */
void coap_fota_set_downloaded_filename(enum fota_image_type type, const char *p,
				       size_t length);

/**
 * @brief Set the time that the image should start being downloaded at.
 */
void coap_fota_set_start(enum fota_image_type type, uint32_t value);

/**
 * @brief Set the time that an firmware update shall occur.
 */
void coap_fota_set_switchover(enum fota_image_type type, uint32_t value);

/**
 * @brief Set the host name used for downloading firmware images.
 */
void coap_fota_set_host(const char *p, size_t length);

/**
 * @brief Set the CoAP block-wise transfer size.
 */
void coap_fota_set_blocksize(uint32_t value);

/**
 * @brief Set the error count
 */
void coap_fota_set_error_count(enum fota_image_type type, uint32_t value);

/**
 * @brief Increment the error count
 */
void coap_fota_increment_error_count(enum fota_image_type type);

/**
 * @brief Accessor function.
 *
 * @retval name of image
 */
const char *coap_fota_get_image_name(enum fota_image_type type);

/**
 * @brief Helper function.
 *
 * @retval true if desired image != running image AND
 * current time >= start time.
 */
bool coap_fota_request(enum fota_image_type type);

/**
 * @brief Helper function.
 *
 * @retval true if an image is ready to be updated.
 */
bool coap_fota_ready(enum fota_image_type type);

/**
 * @brief This is only valid in the WAITING_FOR_SWITCHOVER state
 *
 * @retval true if the requested image has changed
 */
bool coap_fota_abort(enum fota_image_type type);

/**
 * @brief Populate query with shadow information.
 */
void coap_fota_populate_query(enum fota_image_type type,
			      coap_fota_query_t *query);

/**
 * @brief Used by the FOTA state machine to determine when the
 * modem image has finished installing.
 *
 * @retval true when the desired and running version match, false otherwise
 */
bool coap_fota_modem_install_complete(void);

#ifdef __cplusplus
}
#endif

#endif /* __COAP_FOTA_SHADOW_H__ */
