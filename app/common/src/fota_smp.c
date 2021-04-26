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
#ifdef CONFIG_MODEM_HL7800
#include <drivers/modem/hl7800.h>
#endif

#include "file_system_utilities.h"
#include "attr.h"
#include "fota_smp.h"

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static bool modem_fota_request;
static bool modem_fota_busy;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void fota_set_status(uint8_t status);
static uint8_t fota_get_status(void);
static uint8_t fota_get_cmd(void);
static void fota_set_size(ssize_t size);
static void fota_modem_start(void);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void fota_smp_cmd_handler(void)
{
	uint8_t command = fota_get_cmd();

	if (fota_get_status() == FOTA_STATUS_BUSY) {
		LOG_ERR("FOTA busy - command not accepted");
		return;
	}

	fota_set_status(FOTA_STATUS_BUSY);

	switch (command) {
	case FOTA_CONTROL_POINT_NOP:
		fota_set_status(FOTA_STATUS_SUCCESS);
		break;
	case FOTA_CONTROL_POINT_MODEM_START:
		modem_fota_request = true;
		modem_fota_busy = true;
		break;
	default:
		fota_set_status(FOTA_STATUS_ERROR);
		break;
	}
}

void fota_smp_start_handler(void)
{
	if (modem_fota_request) {
		modem_fota_request = false;
		fota_modem_start();
	}
}

void fota_smp_state_handler(uint8_t state)
{
#if defined(CONFIG_MODEM_HL7800) && defined(CONFIG_MODEM_HL7800_FW_UPDATE)
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
			ATTR_ID_modemDesiredLogLevel, LOG_LEVEL_DBG));
	}
#endif
}

bool fota_smp_modem_busy(void)
{
	return modem_fota_busy;
}

void fota_smp_set_count(uint32_t count)
{
	attr_set_uint32(ATTR_ID_fotaCount, count);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void fota_modem_start(void)
{
	int status = FOTA_STATUS_SUCCESS;
	char *abs_path = attr_get_quasi_static(ATTR_ID_fotaFileName);
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

#if CONFIG_MODEM_HL7800_FW_UPDATE
		status = mdm_hl7800_update_fw(abs_path);
#else
		status = FOTA_STATUS_ERROR;
#endif
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

static void fota_set_status(uint8_t status)
{
	attr_set_uint32(ATTR_ID_fotaStatus, status);
}

static uint8_t fota_get_status(void)
{
	return attr_get_uint32(ATTR_ID_fotaStatus, 0);
}

static uint8_t fota_get_cmd(void)
{
	return attr_get_uint32(ATTR_ID_fotaControlPoint, 0);
}

static void fota_set_size(ssize_t size)
{
	attr_set_uint32(ATTR_ID_fotaSize, size);
}
