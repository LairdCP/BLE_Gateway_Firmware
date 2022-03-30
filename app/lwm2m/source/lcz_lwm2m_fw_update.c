/**
 * @file lcz_lwm2m_fw_update.c
 * @brief
 *
 * Copyright (c) 2019 Nordic Semiconductor ASA
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lwm2m_fw_update, CONFIG_LCZ_LWM2M_FW_UPDATE_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <sys/atomic.h>
#include <net/lwm2m.h>
#include <dfu/mcuboot.h>
#include <logging/log_ctrl.h>
#include <dfu/dfu_target.h>
#include <dfu/dfu_target_mcuboot.h>
#include <dfu/mcuboot.h>
#include <sys/reboot.h>

#include "lcz_lwm2m_client.h"

#include "lcz_lwm2m_fw_update.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define BYTE_PROGRESS_STEP (1024 * 10)
#define REBOOT_DELAY_SECONDS 10
#define REBOOT_DELAY K_SECONDS(REBOOT_DELAY_SECONDS)

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static uint8_t
	mcuboot_buf[CONFIG_LCZ_LWM2M_FW_UPDATE_MCUBOOT_FLASH_BUF_SIZE] __aligned(
		4);
static uint8_t firmware_data_buf[CONFIG_LWM2M_COAP_BLOCK_SIZE];
static struct k_work_delayable work_reboot;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void work_reboot_callback(struct k_work *item);
static void lwm2m_set_fw_update_state(int state);
static void lwm2m_set_fw_update_result(int result);
static void dfu_target_cb(enum dfu_target_evt_id evt);
static void *lwm2m_fw_prewrite_callback(uint16_t obj_inst_id, uint16_t res_id,
					uint16_t res_inst_id, size_t *data_len);
static int lwm2m_fw_block_received_callback(uint16_t obj_inst_id,
					    uint16_t res_id,
					    uint16_t res_inst_id, uint8_t *data,
					    uint16_t data_len, bool last_block,
					    size_t total_size);
static int lwm2m_fw_update_callback(uint16_t obj_inst_id, uint8_t *args,
				    uint16_t args_len);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/

int lcz_lwm2m_fw_update_init(void)
{
	int ret;
	bool image_ok;

	k_work_init_delayable(&work_reboot, work_reboot_callback);

	/* setup data buffer for block-wise transfer */
	lwm2m_engine_register_pre_write_callback("5/0/0",
						 lwm2m_fw_prewrite_callback);
	lwm2m_firmware_set_write_cb(lwm2m_fw_block_received_callback);
#if defined(CONFIG_LWM2M_FIRMWARE_UPDATE_PULL_SUPPORT)
	lwm2m_firmware_set_update_cb(lwm2m_fw_update_callback);
#endif

	/* Set the required buffer for MCUboot targets */
	ret = dfu_target_mcuboot_set_buf(mcuboot_buf, sizeof(mcuboot_buf));
	if (ret) {
		LOG_ERR("Failed to set MCUboot flash buffer %d", ret);
		return ret;
	}

	image_ok = boot_is_img_confirmed();
	LOG_INF("Image is%s confirmed", image_ok ? "" : " not");
	if (!image_ok) {
		ret = boot_write_img_confirmed();
		if (ret) {
			LOG_ERR("Couldn't confirm this image: %d", ret);
			lwm2m_set_fw_update_state(STATE_IDLE);
			lwm2m_set_fw_update_result(RESULT_UPDATE_FAILED);
			return ret;
		}

		LOG_INF("Marked image as OK");

		lwm2m_set_fw_update_state(STATE_IDLE);
		lwm2m_set_fw_update_result(RESULT_SUCCESS);
	}

	return ret;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/

static void work_reboot_callback(struct k_work *item)
{
	ARG_UNUSED(item);

	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
}

static void lwm2m_set_fw_update_state(int state)
{
	lwm2m_engine_set_u8("5/0/3", (uint8_t)state);
}

static void lwm2m_set_fw_update_result(int result)
{
	lwm2m_engine_set_u8("5/0/5", result);
}

static void dfu_target_cb(enum dfu_target_evt_id evt)
{
	ARG_UNUSED(evt);
}

static void *lwm2m_fw_prewrite_callback(uint16_t obj_inst_id, uint16_t res_id,
					uint16_t res_inst_id, size_t *data_len)
{
	*data_len = sizeof(firmware_data_buf);
	return firmware_data_buf;
}

static int lwm2m_fw_block_received_callback(uint16_t obj_inst_id,
					    uint16_t res_id,
					    uint16_t res_inst_id, uint8_t *data,
					    uint16_t data_len, bool last_block,
					    size_t total_size)
{
	static uint8_t percent_downloaded;
	static uint32_t bytes_downloaded;
	uint8_t curent_percent;
	uint32_t current_bytes;
	size_t offset;
	size_t skip = 0;
	int ret = 0;
	int image_type;

	if (!data_len) {
		LOG_ERR("Data len is zero, nothing to write.");
		return -EINVAL;
	}

	if (bytes_downloaded == 0) {
		client_acknowledge();

		image_type = dfu_target_img_type(data, data_len);

		ret = dfu_target_init(image_type, total_size, dfu_target_cb);
		if (ret < 0) {
			LOG_ERR("Failed to init DFU target, err: %d", ret);
			lwm2m_set_fw_update_state(STATE_IDLE);
			lwm2m_set_fw_update_result(RESULT_UPDATE_FAILED);
			goto cleanup;
		}

		LOG_INF("Firmware download started.");
		lwm2m_set_fw_update_state(STATE_DOWNLOADING);
	}

	ret = dfu_target_offset_get(&offset);
	if (ret < 0) {
		LOG_ERR("Failed to obtain current offset, err: %d", ret);
		lwm2m_set_fw_update_state(STATE_IDLE);
		lwm2m_set_fw_update_result(RESULT_UPDATE_FAILED);
		goto cleanup;
	}

	/* Display a % downloaded or byte progress, if no total size was
	 * provided (this can happen in PULL mode FOTA)
	 */
	if (total_size > 0) {
		curent_percent = bytes_downloaded * 100 / total_size;
		if (curent_percent > percent_downloaded) {
			percent_downloaded = curent_percent;
			LOG_INF("Downloaded %d%%", percent_downloaded);
		}
	} else {
		current_bytes = bytes_downloaded + data_len;
		if (current_bytes / BYTE_PROGRESS_STEP >
		    bytes_downloaded / BYTE_PROGRESS_STEP) {
			LOG_INF("Downloaded %d kB", current_bytes / 1024);
		}
	}

	if (bytes_downloaded < offset) {
		skip = MIN(data_len, offset - bytes_downloaded);

		LOG_INF("Skipping bytes %d-%d, already written.",
			bytes_downloaded, bytes_downloaded + skip);
	}

	bytes_downloaded += data_len;

	if (skip == data_len) {
		/* Nothing to do. */
		return 0;
	}

	ret = dfu_target_write(data + skip, data_len - skip);
	if (ret < 0) {
		LOG_ERR("dfu_target_write error, err %d", ret);
		lwm2m_set_fw_update_state(STATE_IDLE);
		lwm2m_set_fw_update_result(RESULT_UPDATE_FAILED);
		goto cleanup;
	}

	if (!last_block) {
		/* Keep going */
		return 0;
	} else {
		LOG_INF("Firmware downloaded, %d bytes in total",
			bytes_downloaded);
	}

	if (total_size && (bytes_downloaded != total_size)) {
		LOG_ERR("Early last block, downloaded %d, expecting %d",
			bytes_downloaded, total_size);
		ret = -EIO;
		lwm2m_set_fw_update_state(STATE_IDLE);
		lwm2m_set_fw_update_result(RESULT_UPDATE_FAILED);
	}

cleanup:
	if (ret < 0) {
		if (dfu_target_reset() < 0) {
			LOG_ERR("Failed to reset DFU target");
		}
	}

	bytes_downloaded = 0;
	percent_downloaded = 0;

	return ret;
}

static int lwm2m_fw_update_callback(uint16_t obj_inst_id, uint8_t *args,
				    uint16_t args_len)
{
	ARG_UNUSED(args);
	ARG_UNUSED(args_len);

	int rc;

	LOG_INF("Executing firmware update");

	rc = dfu_target_done(true);
	if (rc != 0) {
		LOG_ERR("Failed to upgrade firmware [%d]", rc);
		lwm2m_set_fw_update_state(STATE_IDLE);
		lwm2m_set_fw_update_result(RESULT_UPDATE_FAILED);
		goto done;
	} else {
		lwm2m_set_fw_update_state(STATE_UPDATING);
	}

	(void)lwm2m_disconnect_and_deregister();

	LOG_INF("Rebooting device in %d seconds", REBOOT_DELAY_SECONDS);
	k_work_schedule(&work_reboot, REBOOT_DELAY);
done:
	return rc;
}
