/**
 * @file rand_range.h
 * @brief Generate random number between two numbers (inclusive).
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __RAND_RANGE_H__
#define __RAND_RANGE_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <random/rand32.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define RAND_RANGE(min, max) ((sys_rand32_get() % (max - min + 1)) + min)

#ifdef __cplusplus
}
#endif

#endif /* __RAND_RANGE_H__ */
