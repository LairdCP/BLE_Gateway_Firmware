/**
 * @file ct_app.h
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __CT_APP_H__
#define __CT_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Initialize contact tracing application.
 *
 * @retval negative error code
 */
int ct_app_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __CT_APP_H__ */
