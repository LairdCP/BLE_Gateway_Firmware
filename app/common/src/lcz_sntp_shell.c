/**
 * @file lcz_sntp_shell.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <shell/shell.h>
#include <init.h>
#include <stdio.h>
#include <stdlib.h>

#include "ethernet_network.h"

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int shell_sntp_update_time_cmd(const struct shell *shell, size_t argc,
				      char **argv)
{
	int rc = 0;

	shell_print(shell, "%s", (sntp_update_time() == true ?
					"SNTP update queued" :
					"Failed to queue SNTP update"));

	return rc;
}

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
SHELL_STATIC_SUBCMD_SET_CREATE(
	sntp_cmds,
	SHELL_CMD(update, NULL, "SNTP time update", shell_sntp_update_time_cmd),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(sntp, &sntp_cmds, "SNTP commands", NULL);
