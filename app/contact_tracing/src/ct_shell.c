/**
 * @file ct_shell.c
 * @brief Contact Tracing Shell
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <shell/shell.h>

#include "lcz_bt_scan.h"
#include "ct_ble.h"
#include "lte.h"
#include "ble_aws_service.h"
#include "lcz_qrtc.h"
#include "nv.h"

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int get_time_cmd(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t time = lcz_qrtc_get_epoch();
	shell_print(shell, "currTime: %d (0x%x)", time, time);
	return 0;
}

static int print_status_cmd(const struct shell *shell, size_t argc, char **argv)
{
	bool isScanning = lcz_bt_scan_active();
	uint32_t numScanStarts = lcz_bt_scan_get_num_starts();
	uint32_t numScanStops = lcz_bt_scan_get_num_stops();
	uint32_t numScanResults = ct_ble_get_num_scan_results();
	uint32_t numScanCtResults = ct_ble_get_num_ct_scan_results();
	bool isPublishing = ct_ble_is_publishing_log();
	bool logTransferActiveFlag = ct_ble_get_log_transfer_active_flag();
	bool connectedToSensor = ct_ble_is_connected_to_sensor();
	bool connectedToCentral = ct_ble_is_connected_to_central();
	uint32_t numConns = ct_ble_get_num_connections();
	uint32_t numDl = ct_ble_get_num_ct_dl_starts();
	uint32_t numDlComplete = ct_ble_get_num_download_completes();

	shell_print(shell,
		    "Scanning: %d, starts: %d, stops: %d, ads: %d, ct-ads: %d",
		    isScanning, numScanStarts, numScanStops, numScanResults,
		    numScanCtResults);
	shell_print(shell, "AWS Publishing: %d", isPublishing);
	shell_print(shell, "Log transfer flag %d", logTransferActiveFlag);
	shell_print(shell, "Connected to ct sensor: %d, %d, %d, %d",
		    connectedToSensor, numConns, numDl, numDlComplete);
	shell_print(shell, "Connected to central: %d", connectedToCentral);

	return 0;
}

static int shell_set_aws_topic_prefix(const struct shell *shell, size_t argc,
				      char **argv)
{
	ARG_UNUSED(argc);

	int rc = nvStoreAwsTopicPrefix(argv[1], strlen(argv[1]));
	aws_svc_set_topic_prefix(argv[1]);
	return rc;
}

#if 0 /* Bug 17828 - move to parameters */
static int check_app_status_cmd(const struct shell *shell, size_t argc,
				char **argv)
{
	if (argc > 1) {
		checkAppStatusEnabled = (uint8_t)strtol(argv[1], NULL, 0);
		shell_print(shell,,"checkAppStatusEnabled set to %d",
			checkAppStatusEnabled);
	}

	return 0;
}

#endif

/******************************************************************************/
/* Shell                                                                      */
/******************************************************************************/
SHELL_STATIC_SUBCMD_SET_CREATE(
	ct_cmds, SHELL_CMD(gettime, NULL, "Get current time", get_time_cmd),
	SHELL_CMD_ARG(set_topic, NULL, "Set topic prefix",
		      shell_set_aws_topic_prefix, 2, 0),
	SHELL_CMD(status, NULL, "Print operating status info",
		  print_status_cmd),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(ct, &ct_cmds, "Contact tracing commands", NULL);
