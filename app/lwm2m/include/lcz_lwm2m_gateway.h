/**
 * @file lcz_lwm2m_gateway.h
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCZ_LWM2M_GATEWAY_H__
#define __LCZ_LWM2M_GATEWAY_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
struct lwm2m_gateway_obj_cfg {
	uint16_t instance;
	char *id;
	char *prefix;
	char *iot_device_objects;
	int8_t rssi;
};

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Create gateway object.  Object type must be enabled in Zephyr.
 *
 * @param cfg @ref lwm2m_gateway_obj_cfg
 * @return int zero on success, otherwise negative error code
 */
int lcz_lwm2m_gateway_create(struct lwm2m_gateway_obj_cfg *cfg);

/**
 * @brief Set ID string in gateway object
 *
 * @param instance each sensor has a unique instance
 * @param id
 * @return int zero on success, otherwise negative error code
 */
int lcz_lwm2m_gateway_id_set(uint16_t instance, char *id);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_LWM2M_GATEWAY_H__ */
