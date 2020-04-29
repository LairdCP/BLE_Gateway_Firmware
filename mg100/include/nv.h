/**
 * @file nv.h
 * @brief Non-volatile storage
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __NV_H__
#define __NV_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define NV_FLASH_DEVICE DT_FLASH_DEV_NAME
#define NV_FLASH_OFFSET DT_FLASH_AREA_STORAGE_OFFSET

#define NUM_FLASH_SECTORS 4

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
int nvInit(void);
int nvReadCommissioned(bool *commissioned);
int nvStoreCommissioned(bool commissioned);
int nvStoreDevCert(u8_t *cert, u16_t size);
int nvStoreDevKey(u8_t *key, u16_t size);
int nvReadDevCert(u8_t *cert, u16_t size);
int nvReadDevKey(u8_t *key, u16_t size);
int nvDeleteDevCert(void);
int nvDeleteDevKey(void);
int nvStoreAwsEndpoint(u8_t *ep, u16_t size);
int nvReadAwsEndpoint(u8_t *ep, u16_t size);
int nvDeleteAwsEndpoint(void);
int nvStoreAwsClientId(u8_t *id, u16_t size);
int nvReadAwsClientId(u8_t *id, u16_t size);
int nvDeleteAwsClientId(void);
int nvStoreAwsRootCa(u8_t *cert, u16_t size);
int nvReadAwsRootCa(u8_t *cert, u16_t size);
int nvDeleteAwsRootCa(void);

#ifdef __cplusplus
}
#endif

#endif /* __NV_H__ */
