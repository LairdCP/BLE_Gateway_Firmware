/**
 * @file http_fota_shadow.h
 * @brief The shadow provides information to AWS and allows
 * firmware versions to be selected.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __HTTP_FOTA_SHADOW_H__
#define __HTTP_FOTA_SHADOW_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <data/json.h>
#include <stddef.h>

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
#define SHADOW_FOTA_DOWNLOAD_HOST_STR        "downloadHost"
#define SHADOW_FOTA_DOWNLOAD_FILE_STR        "downloadFile"
#define SHADOW_FOTA_FS_NAME_STR              "downloadedFilename"
#define SHADOW_FOTA_HASH_STR                 "hash"
#define SHADOW_FOTA_START_STR                "start"
#define SHADOW_FOTA_SWITCHOVER_STR           "switchover"
#define SHADOW_FOTA_ERROR_STR                "errorCount"
/* clang-format on */

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Sets running app from version string. Sets application image name.
 */
void http_fota_shadow_init(void);

/**
 * @brief Sets running app from version string. Sets modem image name.
 *
 * @param modem_fs_path is the file system path for modem images.
 */
#ifdef CONFIG_MODEM_HL7800
void http_fota_modem_shadow_init(const char *modem_fs_path);
#endif

/**
 * @brief Enable shadow generation
 *
 * @note Shadow generation should not be enabled until after get
 * accepted has been processed because previously set values are read
 * from the shadow.
 */
void http_fota_enable_shadow_generation(void);

/**
 * @brief Disable shadow generation
 */
void http_fota_disable_shadow_generation(void);

/**
 * @brief Publish shadow information for FOTA to AWS/Bluegrass.
 *
 * @note Assumes AWS messages can be sent at any time.
 *
 * @retval true if there is an update in progress, false otherwise
 */
bool http_fota_shadow_update_handler(void);

/**
 * @brief Set the version that is currently running.
 */
void http_fota_set_running_version(enum fota_image_type type, const char *p,
				   size_t length);

/**
 * @brief Set image version should be downloaded and run.
 */
void http_fota_set_desired_version(enum fota_image_type type, const char *p,
				   size_t length);

/**
 * @brief Set host where image is downloaded from.
 */
void http_fota_set_download_host(enum fota_image_type type, const char *p,
				 size_t length);

/**
 * @brief Accessor function.
 *
 * @retval download host filename
 */
const char *http_fota_get_download_host(enum fota_image_type type);

/**
 * @brief Set file name of image to download from the host.
 */
void http_fota_set_download_file(enum fota_image_type type, const char *p,
				 size_t length);

/**
 * @brief Accessor function.
 *
 * @retval download cloud file
 */
const char *http_fota_get_download_file(enum fota_image_type type);

/**
 * @brief Set file system name
 */
void http_fota_set_fs_name(enum fota_image_type type, const char *p,
			   size_t length);

/**
 * @brief Accessor function.
 *
 * @retval file system name
 */
const char *http_fota_get_fs_name(enum fota_image_type type);

/**
 * @brief Set the time that the image should start being downloaded at.
 */
void http_fota_set_start(enum fota_image_type type, uint32_t value);

/**
 * @brief Set the time that an firmware update shall occur.
 */
void http_fota_set_switchover(enum fota_image_type type, uint32_t value);

/**
 * @brief Set the error count
 */
void http_fota_set_error_count(enum fota_image_type type, uint32_t value);

/**
 * @brief Increment the error count
 */
void http_fota_increment_error_count(enum fota_image_type type);

/**
 * @brief Accessor function.
 *
 * @retval name of image
 */
const char *http_fota_get_image_name(enum fota_image_type type);

/**
 * @brief Helper function.
 *
 * @retval true if desired image != running image AND
 * current time >= start time.
 */
bool http_fota_request(enum fota_image_type type);

/**
 * @brief Helper function.
 *
 * @retval true if an image is ready to be updated.
 */
bool http_fota_ready(enum fota_image_type type);

/**
 * @brief This is only valid in the WAITING_FOR_SWITCHOVER state
 *
 * @retval true if the requested image has changed
 */
bool http_fota_abort(enum fota_image_type type);

/**
 * @brief Used by the FOTA state machine to determine when the
 * modem image has finished installing.
 *
 * @retval true when the desired and running version match, false otherwise
 */
bool http_fota_modem_install_complete(void);

/**
 * @brief Accessor function to retrieve the hash string
 *
 * @param type the type of image being downloaded
 *
 * @retval pointer to the hash string
 */
const char *http_fota_get_hash(enum fota_image_type type);
/**
 * @brief Helper function to translate the hex string representing the
 *  sha256 hash value of an image.
 *
 * @param type the type of image being downloaded
 * @param buf is the pointer to the buffer to store the conversion result
 * @param buf_len is the length of the buffer
 *
 * @retval non-zero when the string was succesfully converted.
 */
size_t http_fota_convert_hash(enum fota_image_type type, uint8_t *buf,
			      size_t buf_len);

/**
 * @brief Set the hash value being used for the image integrity check.
 *
 * @param type the type of image being downloaded
 * @param p pointer to the data being set
 * @param length the length of the data to be set
 */
void http_fota_set_hash(enum fota_image_type type, const char *p,
			size_t length);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_FOTA_SHADOW_H__ */
