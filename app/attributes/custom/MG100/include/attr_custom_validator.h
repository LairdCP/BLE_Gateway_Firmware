/**
 * @file attr_custom_validator.h
 * @brief Validators custom to a particular project.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __ATTR_CUSTOM_VALIDATOR_H__
#define __ATTR_CUSTOM_VALIDATOR_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <zephyr/types.h>
#include <stddef.h>

#include "attr_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @param do_write true if attribute should be changed, false if pv should
 * be validated but not written.
 *
 * @note Power must be set to broadcast (and subsequently updated in radio).
 *
 * @retval Validators return negative error code, 0 on success
 */
int av_tx_power(const ate_t *const entry, void *pv, size_t vlen, bool do_write);

#ifdef __cplusplus
}
#endif

#endif /* __ATTR_CUSTOM_VALIDATOR_H__ */
