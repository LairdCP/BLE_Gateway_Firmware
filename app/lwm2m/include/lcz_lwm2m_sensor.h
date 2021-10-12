/**
 * @file lcz_lwm2m_sensor.h
 * @brief Process advertisements from sensors and update LwM2M objects.
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCZ_LWM2M_SENSOR_H__
#define __LCZ_LWM2M_SENSOR_H__

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
void lcz_lwm2m_sensor_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_LWM2M_SENSOR_H__ */
