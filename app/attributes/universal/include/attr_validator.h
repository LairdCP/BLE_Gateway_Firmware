/**
 * @file attr_validator.h
 * @brief Validators common to attribute system
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __ATTR_VALIDATOR_H__
#define __ATTR_VALIDATOR_H__

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
 * @brief Attribute validators are run anytime value is set.
 *
 * @param const ate_t *const entry pointer to table entry
 * @param pv pointer to value
 * @param vlen value length
 * @param do_write true if attribute should be changed, false if pv should
 * be validated but not written.
 *
 * @note Control point (cp) validators don't require the value being set
 * to be different than the current value.
 *
 * @return int negative error code, 0 on success
 */
int av_string(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_array(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_uint64(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_uint32(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_uint16(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_uint8(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_bool(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_int64(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_int32(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_int16(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_int8(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_float(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_cp32(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_cp16(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_cp8(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_cpi32(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_cpi16(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_cpi8(const ate_t *const entry, void *pv, size_t vlen, bool do_write);
int av_cpb(const ate_t *const entry, void *pv, size_t vlen, bool do_write);

#ifdef __cplusplus
}
#endif

#endif /* __ATTR_VALIDATOR_H__ */
