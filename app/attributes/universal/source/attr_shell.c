/**
 * @file attr_shell.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <shell/shell.h>
#include <init.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "attr.h"
#include "FrameworkIncludes.h"
#include "file_system_utilities.h"
#include "lcz_qrtc.h"

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int ats_set_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_set_string_cmd(const struct shell *shell, size_t argc,
			      char **argv);
static int ats_query_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_dump_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_show_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_type_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_quiet_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_qrtc_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_load_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_get_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_factory_reset_cmd(const struct shell *shell, size_t argc,
				 char **argv);
static int ats_delete_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_notify_cmd(const struct shell *shell, size_t argc, char **argv);
static int ats_disable_notify_cmd(const struct shell *shell, size_t argc,
				  char **argv);

static int attr_shell_init(const struct device *device);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_attr,
	SHELL_CMD(set, NULL, "set attribute <number or name> <value>",
		  ats_set_cmd),
	SHELL_CMD(set_string, NULL,
		  "set string attribute <number or name> <string>",
		  ats_set_string_cmd),
	SHELL_CMD(query, NULL,
		  "query attribute <number or name>\n"
		  "Prepare NOT called",
		  ats_query_cmd),
	SHELL_CMD(get, NULL,
		  "get attribute <number or name>\n"
		  "If a prepare to read function exists it will be "
		  "called to update parameter value",
		  ats_get_cmd),
	SHELL_CMD(dump, NULL, "<0 = rw, 1 = w, 2 = ro> <abs_path>\n",
		  ats_dump_cmd),
	SHELL_CMD(show, NULL, "Display all parameters", ats_show_cmd),
	SHELL_CMD(
		type, NULL,
		"Display an attribute file\n"
		"<abs file name> <if param present then hexdump (default is string)>",
		ats_type_cmd),
	SHELL_CMD(quiet, NULL,
		  "Disable printing for a parameter\n"
		  "<id> <0 = verbose, 1 = quiet>",
		  ats_quiet_cmd),
	SHELL_CMD(notify, NULL,
		  "Enable/Disable BLE notifications\n"
		  "<id> <0 = disable, 1 = enable>",
		  ats_notify_cmd),
	SHELL_CMD(disable_notify, NULL, "Disable all BLE notifications",
		  ats_disable_notify_cmd),
	SHELL_CMD(
		qrtc, NULL,
		"Set the Quasi-RTC <value>\n"
		"Default is time in seconds from Jan 1, 1970 (UTC).\n"
		"Value must be larger than upTime (ms) and LCZ_QRTC_MINIMUM_EPOCH",
		ats_qrtc_cmd),
	SHELL_CMD(load, NULL, "Load attributes from a file <abs file name>",
		  ats_load_cmd),
	SHELL_CMD(fr, NULL, "Factory Reset", ats_factory_reset_cmd),
	SHELL_CMD(del, NULL, "Delete attribute file", ats_delete_cmd),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(attr, &sub_attr, "Attribute/Parameter Utilities", NULL);

SYS_INIT(attr_shell_init, APPLICATION, 99);

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/

/* Strings can have numbers, but most likely it won't be the first char. */
static bool is_string(const char *str)
{
	if (isdigit((int)str[0])) {
		return false;
	}
	return true;
}

static attr_id_t get_id(const char *str)
{
	attr_id_t id = 0;

	if (is_string(str)) {
		id = attr_get_id(str);
	} else {
		id = (attr_id_t)strtoul(str, NULL, 0);
	}
	return id;
}

static int ats_set_cmd(const struct shell *shell, size_t argc, char **argv)
{
	attr_id_t id = 0;
	int r = -EPERM;
	enum attr_type type;
	static union {
		long unsigned int x;
		long int y;
		long long unsigned int xx;
		long long int yy;
		bool b;
		float f;
		uint8_t bin[ATTR_MAX_BIN_SIZE];
	} param;
	size_t binlen;

	if ((argc == 3) && (argv[1] != NULL) && (argv[2] != NULL)) {
		id = get_id(argv[1]);
		/* The attribute validators try to make sense of what is given to them. */
		if (attr_valid_id(id)) {
			type = attr_get_type(id);

			switch (type) {
			case ATTR_TYPE_FLOAT:
				param.f = strtof(argv[2], NULL);
				r = attr_set(id, ATTR_TYPE_ANY, &param.f,
					     sizeof(param.f));
				break;

			case ATTR_TYPE_BOOL:
			case ATTR_TYPE_U8:
			case ATTR_TYPE_U16:
			case ATTR_TYPE_U32:
				param.x = strtoul(argv[2], NULL, 0);
				r = attr_set(id, ATTR_TYPE_ANY, &param.x,
					     sizeof(param.x));
				break;

			case ATTR_TYPE_U64:
				param.xx = strtoull(argv[2], NULL, 0);
				r = attr_set(id, ATTR_TYPE_ANY, &param.xx,
					     sizeof(param.xx));
				break;

			case ATTR_TYPE_S8:
			case ATTR_TYPE_S16:
			case ATTR_TYPE_S32:
				param.y = strtol(argv[2], NULL, 0);
				r = attr_set(id, ATTR_TYPE_ANY, &param.y,
					     sizeof(param.y));
				break;

			case ATTR_TYPE_S64:
				param.yy = strtoull(argv[2], NULL, 0);
				r = attr_set(id, ATTR_TYPE_ANY, &param.yy,
					     sizeof(param.yy));
				break;

			case ATTR_TYPE_STRING:
				r = attr_set(id, ATTR_TYPE_ANY, argv[2],
					     strlen(argv[2]));
				break;

			case ATTR_TYPE_BYTE_ARRAY:
				memset(param.bin, 0, sizeof(param.bin));
				binlen = hex2bin(argv[2], strlen(argv[2]),
						 param.bin, sizeof(param.bin));
				r = attr_set(id, ATTR_TYPE_ANY, param.bin,
					     binlen);
				break;

			default:
				shell_error(shell, "Unhandled type");
				break;
			}

			if (r < 0) {
				shell_error(shell, "Set failed");
			}

		} else {
			shell_error(shell, "Invalid id");
		}
	} else {
		shell_error(shell, "Unexpected parameters");
		return -EINVAL;
	}
	return 0;
}

/* is_string doesn't handle file names that beging with a number */
static int ats_set_string_cmd(const struct shell *shell, size_t argc,
			      char **argv)
{
	attr_id_t id = 0;
	int r = -EPERM;

	if ((argc == 3) && (argv[1] != NULL) && (argv[2] != NULL)) {
		id = get_id(argv[1]);
		r = attr_set(id, ATTR_TYPE_STRING, argv[2], strlen(argv[2]));
	} else {
		shell_error(shell, "Unexpected parameters");
		return -EINVAL;
	}
	return 0;
}

static int ats_query_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int r = -EPERM;

	if ((argc == 2) && (argv[1] != NULL)) {
		r = attr_show(get_id(argv[1]));
		shell_print(shell, "query status: %d", r);
	} else {
		shell_error(shell, "Unexpected parameters");
		return -EINVAL;
	}
	return 0;
}

static int ats_get_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int r = -EPERM;
	attr_id_t id = 0;
	uint8_t dummy[ATTR_MAX_STR_SIZE];

	if ((argc == 2) && (argv[1] != NULL)) {
		id = get_id(argv[1]);
		/* If the value changed, then prepare will cause a duplicate show. */
		attr_show(id);
		/* Discard data (assumes show is enabled). */
		r = attr_get(id, dummy, sizeof(dummy));
		/* Negative status indicates value isn't readable from SMP. */
		shell_print(shell, "get status: %d", r);
	} else {
		shell_error(shell, "Unexpected parameters");
		return -EINVAL;
	}
	return 0;
}

static int ats_dump_cmd(const struct shell *shell, size_t argc, char **argv)
{
	char *fstr = NULL;
	char *fname = attr_get_quasi_static(ATTR_ID_dumpPath);
	int r = -EPERM;
	int type;

	if ((argc >= 2) && (argv[1] != NULL)) {
		type = MAX((int)strtol(argv[1], NULL, 0), 0);
		r = attr_prepare_then_dump(&fstr, type);
		if (r >= 0) {
			shell_print(shell, "Dump status: %d type: %d", r, type);
			if (argc == 3) {
				if (argv[2] != NULL) {
					fname = argv[2];
				} else {
					shell_print(
						shell,
						"Using default file name: %s",
						fname);
				}
			}
			r = fsu_write_abs(fname, fstr, strlen(fstr));
		}

		if (r < 0) {
			shell_error(shell, "Dump error %d", r);
		} else {
			shell_print(shell, "%s", fstr);
		}

		k_free(fstr);

	} else {
		shell_error(shell, "Unexpected parameters");
		return -EINVAL;
	}
	return 0;
}

static int ats_show_cmd(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	attr_show_all();
	return 0;
}

static int ats_type_cmd(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t *buf;
	ssize_t size;

	if ((argc >= 2) && (argv[1] != NULL)) {
		size = fsu_get_file_size_abs(argv[1]);
		if (size > 0) {
			buf = k_calloc(size + 1, sizeof(uint8_t));
			if (buf != NULL) {
				fsu_read_abs(argv[1], buf, size);
				if (argc > 2) {
					shell_hexdump(shell, buf, size);
				} else {
					shell_print(shell, "%s", buf);
				}
				k_free(buf);
			}
		} else {
			shell_error(shell, "File not found");
		}
	} else {
		shell_error(shell, "Unexpected parameters");
		return -EINVAL;
	}
	return 0;
}

static int ats_quiet_cmd(const struct shell *shell, size_t argc, char **argv)
{
	attr_id_t id = 0;
	int quiet = 0;
	int r = -EPERM;

	if ((argc == 3) && (argv[1] != NULL) && (argv[2] != NULL)) {
		id = get_id(argv[1]);
		quiet = MAX((int)strtol(argv[2], NULL, 0), 0);
		r = attr_set_quiet(id, quiet);
		if (r < 0) {
			shell_error(shell, "Unable to set quiet");
		}
	} else {
		shell_error(shell, "Unexpected parameters");
		return -EINVAL;
	}
	return 0;
}

static int ats_notify_cmd(const struct shell *shell, size_t argc, char **argv)
{
	attr_id_t id = 0;
	int notify = 0;
	int r = -EPERM;

	if ((argc == 3) && (argv[1] != NULL) && (argv[2] != NULL)) {
		id = get_id(argv[1]);
		notify = MAX((int)strtol(argv[2], NULL, 0), 0);
		r = attr_set_notify(id, notify);
		if (r < 0) {
			shell_error(shell, "Unable to set notify");
		}
	} else {
		shell_error(shell, "Unexpected parameters");
		return -EINVAL;
	}
	return 0;
}

static int ats_disable_notify_cmd(const struct shell *shell, size_t argc,
				  char **argv)
{
	int r = -EPERM;

	r = attr_disable_notify();
	if (r < 0) {
		shell_error(shell, "Unable to disable notifications");
	}
	return 0;
}

static int ats_qrtc_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int r = -EPERM;
	uint32_t qrtc;
	uint32_t result;

	if ((argc == 2) && (argv[1] != NULL)) {
		qrtc = MAX((int)strtol(argv[1], NULL, 0), 0);
		result = lcz_qrtc_set_epoch(qrtc);
		r = attr_set_uint32(ATTR_ID_qrtcLastSet, qrtc);
		if (qrtc != result || r < 0) {
			shell_error(shell, "Unable to set qrtc");
		}
	} else {
		shell_error(shell, "Unexpected parameters");
		return -EINVAL;
	}
	return 0;
}

static int ats_load_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int r = -EPERM;
	if ((argc == 2) && (argv[1] != NULL)) {
		r = attr_load(argv[1]);
		if (r < 0) {
			shell_error(shell, "attr_ Load error");
		}
	} else {
		shell_error(shell, "Unexpected parameters");
		return -EINVAL;
	}
	return 0;
}

static int ats_factory_reset_cmd(const struct shell *shell, size_t argc,
				 char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Requesting factory reset");
	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_CONTROL_TASK, FWK_ID_CONTROL_TASK,
				      FMC_FACTORY_RESET);

	return 0;
}

static int ats_delete_cmd(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Delete attribute file status: %d", attr_delete());

	return 0;
}

static int attr_shell_init(const struct device *device)
{
	ARG_UNUSED(device);

	return 0;
}
