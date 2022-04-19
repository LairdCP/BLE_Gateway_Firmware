/**
 * @file app_update.c
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(app_update, 4);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stddef.h>
#include <zephyr.h>
#include <storage/flash_map.h>
#include <dfu/mcuboot.h>
#include <dfu/flash_img.h>
#include <sys/reboot.h>

#include "lcz_memfault.h"

#include "laird_utility_macros.h"
#include "file_system_utilities.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
/* @ref mcuboot.c */
#if USE_PARTITION_MANAGER
#define FLASH_AREA_IMAGE_SECONDARY PM_MCUBOOT_SECONDARY_ID
#else
/* FLASH_AREA_ID() values used below are auto-generated by DT */
#ifdef CONFIG_TRUSTED_EXECUTION_NONSECURE
#define FLASH_AREA_IMAGE_SECONDARY FLASH_AREA_ID(image_1_nonsecure)
#else
#define FLASH_AREA_IMAGE_SECONDARY FLASH_AREA_ID(image_1)
#endif /* CONFIG_TRUSTED_EXECUTION_NONSECURE */
#endif /* USE_PARTITION_MANAGER */

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int app_copy(struct flash_img_context *flash_ctx, const char *abs_path,
		    size_t size);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int app_update_initiate(const char *abs_path)
{
	int r = 0;
	struct flash_img_context *flash_ctx = NULL;
    ssize_t size;

	do {
        size = fsu_get_file_size_abs(abs_path);
        if (size <= 0) {
            LOG_ERR("Invalid file size");
            r = -ENOENT;
            break;
        }

		flash_ctx = k_calloc(sizeof(struct flash_img_context),
				     sizeof(uint8_t));
		if (flash_ctx == NULL) {
			LOG_ERR("Unable to allocate flash context");
			r = -ENOMEM;
			break;
		}

		r = boot_erase_img_bank(FLASH_AREA_IMAGE_SECONDARY);
		if (r < 0) {
			LOG_ERR("Unable to erase secondary image bank");
			break;
		} else {
			LOG_DBG("Secondary slot erased");
		}

		r = flash_img_init(flash_ctx);
		if (r < 0) {
			LOG_ERR("Unable to init image id");
			break;
		}

		r = app_copy(flash_ctx, abs_path, size);
		if (r < 0) {
			LOG_ERR("Unable to copy app to secondary slot");
			break;
		} else {
			LOG_DBG("Image copied");
		}

		r = boot_request_upgrade(BOOT_UPGRADE_PERMANENT);
		if (r < 0) {
			LOG_ERR("Unable to initiate boot request");
			break;
		}

		LOG_WRN("Entering mcuboot");
		LCZ_MEMFAULT_REBOOT_TRACK_FIRMWARE_UPDATE();
		/* Allow last print to occur. */
		k_sleep(K_SECONDS(1));
		sys_reboot(SYS_REBOOT_COLD);

	} while (0);

	k_free(flash_ctx);
	return r;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int app_copy(struct flash_img_context *flash_ctx, const char *abs_path,
		    size_t size)
{
	int r;
    ssize_t bytes_read;
	size_t length;
	struct fs_file_t f;
	uint8_t *buffer;
    size_t rem = size;

    r= fs_open(&f, abs_path, FS_O_READ);
	if (r < 0) {
		return r;
	}

	buffer = k_malloc(CONFIG_IMG_BLOCK_BUF_SIZE);
	if (buffer != NULL) {
		while (r == 0 && rem > 0) {
			length = MIN(rem, CONFIG_IMG_BLOCK_BUF_SIZE);
			bytes_read = fs_read(&f, buffer, length);
			if (bytes_read == length) {
				rem -= length;
				r = flash_img_buffered_write(
					flash_ctx, buffer, length, (rem == 0));
				if (r < 0) {
					LOG_ERR("Unable to write to slot (%d) rem: %u size %u",
						r, rem, size);
				}
			} else {
				r = -EIO;
			}
		}
	} else {
		r = -ENOMEM;
	}

	k_free(buffer);
	(void)fs_close(&f);
	return r;
}
