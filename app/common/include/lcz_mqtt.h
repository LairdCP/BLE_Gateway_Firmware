/**
 * @file lcz_mqtt.h
 * @brief Wrapped MQTT API
 *
 * Copyright (c) 2020-2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCZ_MQTT_H__
#define __LCZ_MQTT_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <net/mqtt.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief Resolve the server address using DNS
 *
 * @return int 0 on success, else negative errno
 */
int lcz_mqtt_get_server_addr(void);

/**
 * @brief Connect to broker
 *
 * @note If CONFIG_LCZ_MQTT_MAX_CONSECUTIVE_CONNECTION_FAILURES is non-zero,
 * then consecutive connection failures will cause a reset.
 *
 * @return int 0 on success, else negative errno
 */
int lcz_mqtt_connect(void);

/**
 * @brief Disconnect from broker
 *
 * @return int 0 on success, else negative errno
 */
int lcz_mqtt_disconnect(void);

/**
 * @brief Send data using MQTT
 *
 * @param binary true if data is binary, false otherwise
 * @param data to send
 * @param len length of data, 0 allowed in non-binary mode
 * @param topic to send data to
 * @return int 0 on success, negative errno otherwise
 */
int lcz_mqtt_send_data(bool binary, char *data, uint32_t len, uint8_t *topic);

/**
 * @brief Helper versions of lcz_mqtt_send_data
 */
int lcz_mqtt_send_string(char *data, uint8_t *topic);
int lcz_mqtt_send_binary(char *data, uint32_t len, uint8_t *topic);

/**
 * @brief Subscribe or Unsubscribe to a topic
 *
 * @param topic null terminated topic string
 * @param subscribe true to subscribe, false to unsubscribe
 * @return int 0 on success, else negative errno
 */
int lcz_mqtt_subscribe(uint8_t *topic, uint8_t subscribe);

/**
 * @return true if connected to MQTT broker
 * @return false otherwise
 */
bool lcz_mqtt_connected(void);

/**
 * @return true if at least one publish has been successful
 * @return false otherwise
 */
bool lcz_mqtt_published(void);

/**
 * @brief Weak callback that can be overridden by application
 */
void lcz_mqtt_disconnect_callback(void);

/**
 * @brief Weak callback that can be overridden by application
 * to prevent reset when MQTT publish watchdog has expired.
 *
 * @return true to prevent reset
 * @return false to allow reset
 */
bool lcz_mqtt_ignore_publish_watchdog(void);

/**
 * @brief ID that is used
 * Weak callback that can be overriden by application.
 * Default is ATTR_ID_client_id value.
 *
 * @return const uint8_t* pointer to client ID
 */
const uint8_t *lcz_mqtt_get_mqtt_client_id(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_MQTT_H__ */
