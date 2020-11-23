/**
 * @file aws_config.c
 * @brief Provides functionality for configuring AWS connection via UART
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(aws_config, LOG_LEVEL_DBG);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stdlib.h>
#include <shell/shell.h>

#include "nv.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define INVALID_PARAM_MSG "Invalid parameter"
#define SET_MSG "set [%s]"
#define SET_ERR_MSG "Could not set option [%d]"
#define READ_ERR_MSG "Could not read option [%d]"
#define VALUE_MSG "value [%s]"
#define BOOL_TO_STR(b) (b ? "true" : "false")

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int cmd_aws_enable(const struct shell *shell, size_t argc, char **argv);
static int cmd_aws_endpoint(const struct shell *shell, size_t argc,
			    char **argv);
static int cmd_aws_id(const struct shell *shell, size_t argc, char **argv);

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int cmd_aws_enable(const struct shell *shell, size_t argc, char **argv)
{
	int rc = 0;
	bool enable;

	if (argc == 2) {
		/* set the value */
		enable = strtol(argv[1], NULL, 10);
		rc = nvStoreAwsEnableCustom(enable);
		if (rc >= 0) {
			shell_print(shell, VALUE_MSG, BOOL_TO_STR(enable));
		} else {
			shell_error(shell, SET_ERR_MSG, rc);
		}
		if (enable) {
			rc = nvStoreCommissioned(true);
			if (rc < 0) {
				shell_error(shell,
					    "error setting commissioned [%d]",
					    rc);
			}
		}
	} else if (argc == 1) {
		/* read the value */
		rc = nvReadAwsEnableCustom(&enable);
		if (rc > 0) {
			shell_print(shell, VALUE_MSG, BOOL_TO_STR(enable));
		} else {
			shell_error(shell, READ_ERR_MSG, rc);
		}
	} else {
		shell_error(shell, INVALID_PARAM_MSG);
		rc = -EINVAL;
	}

	return rc;
}

static int cmd_aws_endpoint(const struct shell *shell, size_t argc, char **argv)
{
	int rc = 0;
	size_t val_len;
	char ep[CONFIG_APP_AWS_ENDPOINT_MAX_LENGTH];

	if (argc == 2) {
		/* set the value */
		val_len = strlen(argv[1]);
		if (val_len > CONFIG_APP_AWS_ENDPOINT_MAX_LENGTH) {
			rc = -EINVAL;
			shell_error(shell, "endpoint too long [%d]", val_len);
			goto done;
		}

		rc = nvStoreAwsEndpoint(argv[1], val_len);
		if (rc >= 0) {
			shell_print(shell, SET_MSG, argv[1]);
		} else {
			shell_error(shell, SET_ERR_MSG, rc);
		}
	} else if (argc == 1) {
		/* read the value */
		rc = nvReadAwsEndpoint(ep, CONFIG_APP_AWS_ENDPOINT_MAX_LENGTH);
		if (rc > 0) {
			shell_print(shell, VALUE_MSG, ep);
		} else {
			shell_error(shell, READ_ERR_MSG, rc);
		}
	} else {
		shell_error(shell, INVALID_PARAM_MSG);
		rc = -EINVAL;
	}
done:
	return rc;
}

static int cmd_aws_id(const struct shell *shell, size_t argc, char **argv)
{
	int rc = 0;
	size_t val_len;
	char id[CONFIG_APP_AWS_CLIENT_ID_MAX_LENGTH];

	if (argc == 2) {
		/* set the value */
		val_len = strlen(argv[1]);
		if (val_len > CONFIG_APP_AWS_CLIENT_ID_MAX_LENGTH) {
			rc = -EINVAL;
			shell_error(shell, "id too long [%d]", val_len + 1);
			goto done;
		}

		rc = nvStoreAwsClientId(argv[1], val_len);
		if (rc >= 0) {
			shell_print(shell, SET_MSG, argv[1]);
		} else {
			shell_error(shell, SET_ERR_MSG, rc);
		}
	} else if (argc == 1) {
		/* read the value */
		rc = nvReadAwsClientId(id, CONFIG_APP_AWS_CLIENT_ID_MAX_LENGTH);
		if (rc > 0) {
			shell_print(shell, VALUE_MSG, id);
		} else {
			shell_error(shell, READ_ERR_MSG, rc);
		}
	} else {
		shell_error(shell, INVALID_PARAM_MSG);
		rc = -EINVAL;
	}
done:
	return rc;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	aws_cmds,
	SHELL_CMD(enable, NULL, "Enable custom AWS connection", cmd_aws_enable),
	SHELL_CMD(endpoint, NULL, "AWS hostname enpoint", cmd_aws_endpoint),
	SHELL_CMD(id, NULL, "AWS client ID", cmd_aws_id),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);
SHELL_CMD_REGISTER(aws, &aws_cmds, "AWS config", NULL);
