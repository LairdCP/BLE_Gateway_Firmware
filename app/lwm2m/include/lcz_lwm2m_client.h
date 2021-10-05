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
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/

/* temporary */
/* clang-format off */
#define IPSO_OJBECT_CURRENT_SENSOR_ID       3317
#define IPSO_OBJECT_FILLING_LEVEL_SENSOR_ID 3435
/* clang-format on */

enum lwm2m_instance {
	LWM2M_INSTANCE_BOARD = 0,
	LWM2M_INSTANCE_BL654_SENSOR = 1,
	LWM2M_INSTANCE_SENSOR_START = 4
};

/* clang-format off */
#define LWM2M_TEMPERATURE_UNITS "C"
#define LWM2M_TEMPERATURE_MIN   -40.0
#define LWM2M_TEMPERATURE_MAX   85.0

#define LWM2M_BT610_TEMPERATURE_UNITS "C"
#define LWM2M_BT610_TEMPERATURE_MIN   -40.0
#define LWM2M_BT610_TEMPERATURE_MAX   125.0

#define LWM2M_HUMIDITY_UNITS "%"
#define LWM2M_HUMIDITY_MIN   0.0
#define LWM2M_HUMIDITY_MAX   100.0

#define LWM2M_PRESSURE_UNITS "Pa"
#define LWM2M_PRESSURE_MIN   300.0
#define LWM2M_PRESSURE_MAX   1100000.0

#define LWM2M_BT610_CURRENT_UNITS "mA"
#define LWM2M_BT610_CURRENT_MIN   0.0
#define LWM2M_BT610_CURRENT_MAX   1000000.0

#define LWM2M_BT610_PRESSURE_UNITS "PSI"
#define LWM2M_BT610_PRESSURE_MIN   0
#define LWM2M_BT610_PRESSURE_MAX   1000.0

/* Ultrasonic may have min/max distance for which it is accurate */
#define LWM2M_BT610_ULTRASONIC_UNITS "mm"
#define LWM2M_BT610_ULTRASONIC_MIN   0
#define LWM2M_BT610_ULTRASONIC_MAX   10000.0
/* clang-format on */

struct lwm2m_sensor_obj_cfg {
	uint16_t type;
	uint16_t instance;
	char *units;
	float min;
	float max;
};

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
 * @brief Set sensor object data
 *
 * @param type object type.  For example, 3303 for temperature
 * @param instance each sensor has a unique instance
 * @param value sensor data (temperature, current, ...)
 * @return int zero on success, otherwise negative error code
 */
int lwm2m_set_sensor_data(uint16_t type, uint16_t instance, float value);

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
int lwm2m_set_temperature(float temperature);

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

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_LWM2M_CLIENT_H__ */
