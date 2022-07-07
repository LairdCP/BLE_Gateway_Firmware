/**
 * @file memfault_task.h
 * @brief A thread is used to facilitate sending Memfault data on demand and periodically.
 * It is not possible to use the system workq for this task because POSTing the data blocks.
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#ifndef __MEMFAULT_TASK_H__
#define __MEMFAULT_TASK_H__

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************/
/* Global Constants, Macros and Type Definitions                                                  */
/**************************************************************************************************/
#ifdef CONFIG_LCZ_MEMFAULT
#define LCZ_LWM2M_MEMFAULT_POST_DATA lcz_lwm2m_memfault_post_data
#else
#define LCZ_LWM2M_MEMFAULT_POST_DATA(...)
#endif

/**************************************************************************************************/
/* Global Function Prototypes                                                                     */
/**************************************************************************************************/
#ifdef CONFIG_LCZ_MEMFAULT
/**
 * @brief Post any available data to memfault cloud via HTTPS
 *
 * @return 0 on success
 */
int lcz_lwm2m_memfault_post_data(void);
#endif /* CONFIG_LCZ_MEMFAULT*/

#ifdef __cplusplus
}
#endif

#endif /* __MEMFAULT_TASK_H__ */
