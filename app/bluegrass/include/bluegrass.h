/**
 * @file bluegrass.h
 * @brief Bluegrass is Laird Connectivity's AWS interface.
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLUEGRASS_H__
#define __BLUEGRASS_H__

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
 * @brief Initialize Bluegrass gateway interface.
 *
 * Initialize sensor task if enabled.  The sensor task will process messages
 * from the BT510.
 */
void bluegrass_initialize(void);

/**
 * @brief Accessor function
 *
 * @retval true if system is ready for publishing to AWS/Bluegrass
 */
bool bluegrass_ready_for_publish(void);

#ifdef __cplusplus
}
#endif

#endif /* __BLUEGRASS_H__ */
