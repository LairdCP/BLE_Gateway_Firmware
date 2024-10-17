/**
 * @file lcz_certs.h
 * @brief Loads cloud certificates from file system into buffers allocated
 * from the system heap.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCZ_CERTS_H__
#define __LCZ_CERTS_H__

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
 * @brief If they haven't been loaded, then load certificates from the file 
 * system using names in the attribute system.
 *
 * Uses slots LCZ_CERTS_CA_CERT_TAG and [LCZ_CERTS_DEVICE_CERT_TAG].
 *
 * @retval negative error code, 0 on success
 */
int lcz_certs_load(void);

/**
 * @brief Delete certs from mbedTLS.
 *
 * @retval negative error code, 0 on success
 */
int lcz_certs_unload(void);

/**
 * @brief Accessor
 *
 * @retval true if certs have been loaded, false otherwise.
 */
bool lcz_certs_loaded(void);

/**
 * @brief Helper function that calls unload and then load.
 */
int lcz_certs_reload(void);

#ifdef __cplusplus
}
#endif

#endif /* __CERT_LOADER_H__ */
