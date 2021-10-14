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
#include <net/lwm2m.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
/* clang-format off */
#define	LWM2M_INSTANCE_BOARD        0
#define	LWM2M_INSTANCE_BL654_SENSOR 1
#define	LWM2M_INSTANCE_TEST         2
#define	LWM2M_INSTANCE_SENSOR_START 4
/* clang-format on */

/* clang-format off */
#define LWM2M_TEMPERATURE_UNITS "C"
#define LWM2M_TEMPERATURE_MIN   -40.0
#define LWM2M_TEMPERATURE_MAX   85.0

#define LWM2M_HUMIDITY_UNITS "%"
#define LWM2M_HUMIDITY_MIN   0.0
#define LWM2M_HUMIDITY_MAX   100.0

#define LWM2M_PRESSURE_UNITS "Pa"
#define LWM2M_PRESSURE_MIN   300.0
#define LWM2M_PRESSURE_MAX   1100000.0
/* clang-format on */

struct lwm2m_sensor_obj_cfg {
	uint16_t type;
	uint16_t instance;
	bool skip_secondary; /* units, min, and max only valid for some sensors */
	char *units;
	double min;
	double max;
};

/* /lfs/65535.65535.65535.65535.bin */
#define LWM2M_CFG_FILE_NAME_MAX_SIZE                                           \
	(sizeof(CONFIG_FSU_MOUNT_POINT) + CONFIG_LWM2M_PATH_MAX_SIZE + 1)

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
 * @brief Create sensor object.  Object type must be enabled in Zephyr.
 *
 * @param cfg @ref lwm2m_sensor_obj_cfg
 * @return int zero on success, otherwise negative error code
 */
int lwm2m_create_sensor_obj(struct lwm2m_sensor_obj_cfg *cfg);

/**
 * @brief Set the board temperature
 *
 * @param temperature
 * @return int zero on success, otherwise negative error code
 */
int lwm2m_set_board_temperature(float temperature);

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
bool lwm2m_connected(void);

/**
 * @brief Connects to the LwM2M server
 *
 * @retval 0 on success, negative errno otherwise.
 */
int lwm2m_connect(void);

/**
 * @brief Disconnects from the LwM2M server
 *
 * @retval 0 on success, negative errno otherwise.
 */
int lwm2m_disconnect(void);

/**
 * @brief Load configuration/state from non-volatile memory.
 *
 * @param type ID of object
 * @param instance ID of object
 * @param resource ID of object
 * @param data_len size of data
 * @return int zero on success, negative error code
 */
int lwm2m_load(uint16_t type, uint16_t instance, uint16_t resource,
	       uint16_t data_len);
/**
 * @brief Save configuration/state to non-volatile memory
 *
 * @param type of object
 * @param instance of object
 * @param resource ID of object
 * @param data pointer to data
 * @param data_len size of data
 * @return int greater than zero on success (size of write),
 * negative error code
 */
int lwm2m_save(uint16_t type, uint16_t instance, uint16_t resource,
	       uint8_t *data, uint16_t data_len);

/**
 * @brief Delete a resource instance
 *
 * @param type object type id
 * @param instance object instance
 * @param resource id
 * @param resource_inst most often 0
 * @return int negative error code, 0 on success
 */
int lwm2m_delete_resource_inst(uint16_t type, uint16_t instance,
			       uint16_t resource, uint16_t resource_inst);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_LWM2M_CLIENT_H__ */
