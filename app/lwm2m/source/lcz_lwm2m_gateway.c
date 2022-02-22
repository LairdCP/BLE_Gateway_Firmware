/**
 * @file lcz_lwm2m_gateway.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lwm2m_gateway, CONFIG_LCZ_LWM2M_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <zephyr/types.h>
#include <net/lwm2m.h>
#include "lwm2m_resource_ids.h"
#include "lwm2m_obj_gateway.h"

#include "lcz_lwm2m_client.h"
#include "lcz_lwm2m_gateway.h"

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int lcz_lwm2m_gateway_create(struct lwm2m_gateway_obj_cfg *cfg)
{
	int r = -EAGAIN;
	char path[CONFIG_LWM2M_PATH_MAX_SIZE];

	if (lwm2m_client_init() == 0) {
		snprintk(path, sizeof(path),
			 STRINGIFY(LWM2M_OBJECT_GATEWAY_ID) "/%u",
			 cfg->instance);
		r = lwm2m_engine_create_obj_inst(path);
		if (r < 0) {
			return r;
		}

		LWM2M_CLIENT_REREGISTER;

		if (cfg->id != NULL) {
			snprintk(
				path, sizeof(path),
				STRINGIFY(LWM2M_OBJECT_GATEWAY_ID) "/%u/" STRINGIFY(
					LWM2M_GATEWAY_DEVICE_RID),
				cfg->instance);
			lwm2m_engine_set_string(path, cfg->id);
		}

		/* Bug 20220 - Prefix needs to be checked for uniqueness */
		if (cfg->prefix != NULL) {
			snprintk(
				path, sizeof(path),
				STRINGIFY(LWM2M_OBJECT_GATEWAY_ID) "/%u/" STRINGIFY(
					LWM2M_GATEWAY_PREFIX_RID),
				cfg->instance);
			lwm2m_engine_set_string(path, cfg->prefix);
		}

		if (cfg->iot_device_objects != NULL) {
			snprintk(
				path, sizeof(path),
				STRINGIFY(LWM2M_OBJECT_GATEWAY_ID) "/%u/" STRINGIFY(
					LWM2M_GATEWAY_IOT_DEVICE_OBJECTS_RID),
				cfg->instance);
			lwm2m_engine_set_string(path, cfg->iot_device_objects);
		}
	}

	return r;
}

int lcz_lwm2m_gateway_id_set(uint16_t instance, char *id)
{
	char path[CONFIG_LWM2M_PATH_MAX_SIZE];

	snprintk(path, sizeof(path),
		 STRINGIFY(LWM2M_OBJECT_GATEWAY_ID) "/%u/" STRINGIFY(
			 LWM2M_GATEWAY_DEVICE_RID),
		 instance);

	return lwm2m_engine_set_string(path, id);
}
