/**
 * @file fota_smp.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(fota_smp, CONFIG_LOG_LEVEL_FOTA_SMP);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#ifdef CONFIG_MODEM_HL7800_FW_UPDATE
#include <drivers/modem/hl7800.h>
#endif

#ifdef CONFIG_ESS_SENSOR
#include "ess_sensor.h"
#endif

#include "file_system_utilities.h"
#include "attr.h"
#include "lcz_bt_scan.h"
#include "gateway_fsm.h"

#include "fota_smp.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define DEFAULT_PREPARE_TIMEOUT 3600 /* 1 hour */

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
#ifdef CONFIG_MODEM_HL7800_FW_UPDATE
static bool modem_fota_request;
static bool modem_fota_busy;
#endif

static bool fota_smp_initialized;

static int scan_user_id = -1;

static struct k_work_delayable prepare_timeout;
static bool ble_prepared;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void fota_set_status(uint8_t status);
static uint8_t fota_get_status(void);
static uint8_t fota_get_cmd(void);

#ifdef CONFIG_MODEM_HL7800_FW_UPDATE
static void fota_set_size(ssize_t size);

static void fota_modem_start(void);
#endif

static void unused_adv_handler(const bt_addr_le_t *addr, int8_t rssi,
			       uint8_t type, struct net_buf_simple *ad);

static void prepare_timeout_handler(struct k_work *work);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void fota_smp_cmd_handler(void)
{
	int r = 0;
	uint8_t command = fota_get_cmd();

	if (!fota_smp_initialized) {
		if (!lcz_bt_scan_register(&scan_user_id, unused_adv_handler)) {
			LOG_ERR("Unable to register scan user for FOTA SMP module");
		}

		k_work_init_delayable(&prepare_timeout,
				      prepare_timeout_handler);
		fota_smp_initialized = true;
	}

	if (fota_get_status() == FOTA_STATUS_BUSY) {
		LOG_ERR("FOTA busy - command not accepted");
		return;
	}

	fota_set_status(FOTA_STATUS_BUSY);

	switch (command) {
	case FOTA_CONTROL_POINT_NOP:
		fota_set_status(FOTA_STATUS_SUCCESS);
		break;

#ifdef CONFIG_MODEM_HL7800_FW_UPDATE
	case FOTA_CONTROL_POINT_MODEM_START:
		modem_fota_request = true;
		modem_fota_busy = true;
		/* Modem isn't updated if cloud is connected. */
		gateway_fsm_request_cloud_disconnect();
		break;
#endif

	case FOTA_CONTROL_POINT_BLE_PREPARE:
		ble_prepared = true;
		lcz_bt_scan_stop(scan_user_id);

#ifdef CONFIG_ESS_SENSOR
		r = ess_sensor_disconnect();
#endif
		fota_set_status(r == 0 ? FOTA_STATUS_SUCCESS :
					       FOTA_STATUS_ERROR);

		k_work_reschedule(
			&prepare_timeout,
			K_SECONDS(attr_get_uint32(ATTR_ID_ble_prepare_timeout,
						  DEFAULT_PREPARE_TIMEOUT)));
		break;

	case FOTA_CONTROL_POINT_BLE_ABORT:
		ble_prepared = false;
		lcz_bt_scan_resume(scan_user_id);
		fota_set_status(FOTA_STATUS_SUCCESS);
		k_work_cancel_delayable(&prepare_timeout);
		break;

	default:
		fota_set_status(FOTA_STATUS_ERROR);
		break;
	}
}

#ifdef CONFIG_MODEM_HL7800_FW_UPDATE
void fota_smp_start_handler(void)
{
	if (modem_fota_request) {
		modem_fota_request = false;
		fota_modem_start();
	}
}

void fota_smp_state_handler(uint8_t state)
{
	bool restore_log_level = false;

	/* Qualify state update based on command and status. */
	if ((fota_get_cmd() == FOTA_CONTROL_POINT_MODEM_START) &&
	    (fota_get_status() == FOTA_STATUS_BUSY)) {
		switch (state) {
		case HL7800_FOTA_IDLE:
			fota_set_status(FOTA_STATUS_SUCCESS);
			modem_fota_busy = false;
			break;
		case HL7800_FOTA_COMPLETE:
			fota_set_status(FOTA_STATUS_SUCCESS);
			restore_log_level = true;
			modem_fota_busy = false;
			if (IS_ENABLED(CONFIG_FOTA_SMP_DELETE_ON_COMPLETE)) {
				fsu_delete_abs(attr_get_quasi_static(
					ATTR_ID_fota_file_name));
			}
			break;
		case HL7800_FOTA_FILE_ERROR:
			LOG_ERR("FOTA File Error");
			fota_set_status(FOTA_STATUS_ERROR);
			restore_log_level = true;
			modem_fota_busy = false;
			break;
		default:
			/* Keep indicating busy. */
			break;
		}
	}

	if (restore_log_level) {
		mdm_hl7800_log_filter_set(attr_get_uint32(
			ATTR_ID_modem_desired_log_level, LOG_LEVEL_DBG));
	}
}

bool fota_smp_modem_busy(void)
{
	return modem_fota_busy;
}
#endif

void fota_smp_set_count(uint32_t count)
{
	attr_set_uint32(ATTR_ID_fota_count, count);
}

bool fota_smp_ble_prepared(void)
{
	return ble_prepared;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
#ifdef CONFIG_MODEM_HL7800_FW_UPDATE
static void fota_modem_start(void)
{
	int status = FOTA_STATUS_SUCCESS;
	char *abs_path = attr_get_quasi_static(ATTR_ID_fota_file_name);
	ssize_t size = fsu_get_file_size_abs(abs_path);

	fota_smp_set_count(0);

	if (size <= 0) {
		status = FOTA_STATUS_ERROR;
		fota_set_status(FOTA_STATUS_ERROR);
		fota_set_size(0);
		LOG_ERR("Modem FOTA file not found");
		return;
	} else {
		fota_set_size(size);
	}

	/* Pass control to modem task. */
	if (status == FOTA_STATUS_SUCCESS) {
		LOG_INF("Requesting modem firmware update with file %s",
			log_strdup(abs_path));

		status = mdm_hl7800_update_fw(abs_path);

		if (status != 0) {
			fota_set_status(FOTA_STATUS_ERROR);
			LOG_ERR("Modem FOTA failed to start");
		} else {
			/* State changes are printed at info level */
			mdm_hl7800_log_filter_set(LOG_LEVEL_INF);
			modem_fota_busy = true;
		}
	}
}

static void fota_set_size(ssize_t size)
{
	attr_set_uint32(ATTR_ID_fota_size, size);
}
#endif

static void fota_set_status(uint8_t status)
{
	attr_set_uint32(ATTR_ID_fota_status, status);
}

static uint8_t fota_get_status(void)
{
	return attr_get_uint32(ATTR_ID_fota_status, FOTA_STATUS_BUSY);
}

static uint8_t fota_get_cmd(void)
{
	return attr_get_uint32(ATTR_ID_fota_control_point,
			       FOTA_CONTROL_POINT_NOP);
}

/* Ads aren't processed by this task, but scanning can be stopped before FOTA. */
static void unused_adv_handler(const bt_addr_le_t *addr, int8_t rssi,
			       uint8_t type, struct net_buf_simple *ad)
{
	ARG_UNUSED(addr);
	ARG_UNUSED(rssi);
	ARG_UNUSED(type);
	ARG_UNUSED(ad);

	return;
}

static void prepare_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_WRN("BLE Prepare timeout");
	if (ble_prepared) {
		attr_set_uint32(ATTR_ID_fota_control_point,
				FOTA_CONTROL_POINT_BLE_ABORT);
	}
}
