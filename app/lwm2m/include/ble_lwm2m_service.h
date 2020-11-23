/**
 * @file ble_lwm2m_service.h
 * @brief This service contains configuration parameters for the LwM2M client.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLE_L2M2M_SERVICE_H___
#define __BLE_L2M2M_SERVICE_H___

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
 * @brief Initialize the LwM2M Bluetooth Service.
 */
void ble_lwm2m_service_init();

/**
 * @brief Accessor function
 * @note This is not NULL terminated.
 */
uint8_t *ble_lwm2m_get_client_psk(void);

/**
 * @brief Accessor function
 */
char *ble_lwm2m_get_client_id(void);

/**
 * @brief Accessor function
 */
char *ble_lwm2m_get_peer_url(void);

#ifdef __cplusplus
}
#endif

#endif /* __BLE_L2M2M_SERVICE_H___ */
