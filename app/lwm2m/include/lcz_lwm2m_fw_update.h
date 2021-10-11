/**
 * @file lcz_lwm2m_fw_update.h
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCZ_LWM2M_FW_UPDATE_H__
#define __LCZ_LWM2M_FW_UPDATE_H__

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
extern void client_acknowledge(void);

int lcz_lwm2m_fw_update_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_LWM2M_FW_UPDATE_H__ */
