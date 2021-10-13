/**
 * @file lcz_lwm2m_conn_mon.h
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCZ_LWM2M_CONN_MON_H__
#define __LCZ_LWM2M_CONN_MON_H__

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

int lcz_lwm2m_conn_mon_update_values(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_LWM2M_CONN_MON_H__ */
