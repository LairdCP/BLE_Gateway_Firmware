/**
 * @file lcz_lwm2m_battery.c
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lcz_lwm2m_battery, CONFIG_LCZ_LWM2M_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <zephyr/types.h>
#include <net/lwm2m.h>
#include "ucifi_battery.h"

#include "lcz_lwm2m_client.h"
#include "lcz_lwm2m_battery.h"

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
struct battery_voltage_data {
	double volts;
	bool assigned;
};

struct battery_voltage_data
	battery_voltage[CONFIG_LWM2M_UCIFI_BATTERY_INSTANCE_COUNT];

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int lcz_lwm2m_battery_create(struct lwm2m_battery_obj_cfg *cfg)
{
	int r = -EAGAIN;
	char path[CONFIG_LWM2M_PATH_MAX_SIZE];
	struct battery_voltage_data *volt_data_store;
	int i;

	if (lwm2m_client_init() == 0) {
		snprintk(path, sizeof(path),
			 STRINGIFY(UCIFI_OBJECT_BATTERY_ID) "/%u",
			 cfg->instance);
		/* create the battery object (3411/0) */
		r = lwm2m_engine_create_obj_inst(path);
		if (r < 0) {
			goto done;
		}

		/* Create the battery voltage resource (3411/0/3) data storage
		 * since it is an optional resource and doesn't have storage
		 * for the data allocated yet.
		 */
		snprintk(path, sizeof(path),
			 STRINGIFY(UCIFI_OBJECT_BATTERY_ID) "/%u/" STRINGIFY(
				 UCIFI_BATTERY_VOLTAGE_RID),
			 cfg->instance);
		volt_data_store = NULL;
		for (i = 0; i < CONFIG_LWM2M_UCIFI_BATTERY_INSTANCE_COUNT;
		     i++) {
			if (battery_voltage[i].assigned == false) {
				volt_data_store = &battery_voltage[i];
				break;
			}
		}
		if (volt_data_store == NULL) {
			LOG_ERR("All battery voltage instances used");
			r = -EINVAL;
		}
		r = lwm2m_engine_set_res_data(path, &volt_data_store->volts,
					      sizeof(volt_data_store->volts),
					      0);
		if (r < 0) {
			goto done;
		}

		volt_data_store->assigned = true;

		LWM2M_CLIENT_REREGISTER;

		if (cfg->level != 0) {
			r = lcz_lwm2m_battery_level_set(cfg->instance,
							cfg->level);
			if (r < 0) {
				goto done;
			}
		}

		if (cfg->voltage != 0) {
			r = lcz_lwm2m_battery_voltage_set(cfg->instance,
							  &cfg->voltage);
			if (r < 0) {
				goto done;
			}
		}
	}
done:
	return r;
}

int lcz_lwm2m_battery_level_set(uint16_t instance, uint8_t level)
{
	char path[CONFIG_LWM2M_PATH_MAX_SIZE];

	snprintk(path, sizeof(path),
		 STRINGIFY(UCIFI_OBJECT_BATTERY_ID) "/%u/" STRINGIFY(
			 UCIFI_BATTERY_LEVEL_RID),
		 instance);

	return lwm2m_engine_set_u8(path, level);
}

int lcz_lwm2m_battery_voltage_set(uint16_t instance, double *voltage)
{
	char path[CONFIG_LWM2M_PATH_MAX_SIZE];

	snprintk(path, sizeof(path),
		 STRINGIFY(UCIFI_OBJECT_BATTERY_ID) "/%u/" STRINGIFY(
			 UCIFI_BATTERY_VOLTAGE_RID),
		 instance);

	return lwm2m_engine_set_float(path, voltage);
}
