/**
 * @file gateway_common.h
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __GATEWAY_COMMON_H__
#define __GATEWAY_COMMON_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Configure the application
 *
 * @retval 0 on success, else errno
 */
int configure_app(void);

#ifdef __cplusplus
}
#endif

#endif /* __GATEWAY_COMMON_H__ */
