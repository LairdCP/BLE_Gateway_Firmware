/**
 * @file lcz_lwm2m_client.h
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCZ_LWM2M_CLIENT_H__
#define __LCZ_LWM2M_CLIENT_H__

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
 * @brief Initialize the LWM2M device.
 */
int lwm2m_client_init(void);

/**
 * @brief Set the temperature, pressure, and humidity in the
 * respective IPSO objects.
 *
 * @retval 0 on success, negative errno otherwise.
 */
int lwm2m_set_bl654_sensor_data(float temperature, float humidity,
				float pressure);

/**
 * @brief Generate new PSK
 *
 * @retval 0 on success, negative errno otherwise.
 */
int lwm2m_generate_psk(void);

/**
 * @brief Check if connected to the LwM2M server
 *
 * @retval true if connected, false otherwise
 */
bool lwm2mConnected(void);

/**
 * @brief Connects to the LwM2M server
 *
 * @retval 0 on success, negative errno otherwise.
 */
int lwm2mConnect(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_LWM2M_CLIENT_H__ */
