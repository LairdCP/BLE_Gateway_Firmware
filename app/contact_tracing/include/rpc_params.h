/**
 * @file rpc_params.h
 * @brief Process subscription data from AWS as commands (remote procedure calls).
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __RPC_PARAMS_H__
#define __RPC_PARAMS_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
/* clang-format off */
#define CONFIG_RPC_PARAMS_METHOD_MAX_SIZE     32
#define CONFIG_RPC_PARAMS_BUF_MAX_SIZE        1024
#define CONFIG_RPC_PARAMS_FILE_NAME_MAX_SIZE  8
#define CONFIG_RPC_PARAMS_WHENCE_MAX_SIZE     8
#define CONFIG_RPC_PARAMS_CMD_MAX_SIZE        768
/* clang-format on */

/* log_get params structure */
typedef struct rpc_params_log_get_s {
	char filename[CONFIG_RPC_PARAMS_FILE_NAME_MAX_SIZE];
	char whence[CONFIG_RPC_PARAMS_WHENCE_MAX_SIZE];
	uint32_t offset;
	uint32_t length;
} rpc_params_log_get_t;

/* exec params structure */
typedef struct rpc_params_exec_s {
	char cmd[CONFIG_RPC_PARAMS_CMD_MAX_SIZE];
} rpc_params_exec_t;

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Process $aws/things/deviceId-X/shadow/update/accepted contents to
 * find an RPC method being invoked.
 *
 * @note This is meant to be called from the shadow parser that
 * has already called jsmn_start and determined topic is for the gateway.
 *
 * @note This function assumes that the AWS task acknowledges the publish so
 * that it isn't repeatedly sent to the gateway.
 *
 * @param get_accepted_topic true when topic contains /accepted
 *
 */
void rpc_params_gateway_parser(bool get_accepted_topic);

/**
 * @brief Get the last rpc method sent via device shadow (if any)
 *
 */
char *rpc_params_get_method(void);

/**
 * @brief Get a pointer to the last parsed rpc params structure.
 * Contents map to an rpc_params_* structure determined by
 * the return value of rpc_params_get_method().
 *
 */
void *rpc_params_get(void);

/**
 * @brief Clear the last gateway rpc method buffer
 */
void rpc_params_clear_method(void);

#ifdef __cplusplus
}
#endif

#endif /* __RPC_PARAMS_H__ */
