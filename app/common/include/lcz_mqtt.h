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
#include <stdarg.h>
#include <sys/slist.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
struct lcz_mqtt_user {
	sys_snode_t node;
	uint16_t ack_id;
	void (*ack_callback)(int status);
};

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
 * @param cb registered callback node, NULL if callback isn't desired.
 * @return int 0 on success, negative errno otherwise
 */
int lcz_mqtt_send_data(bool binary, char *data, uint32_t len, uint8_t *topic,
		       struct lcz_mqtt_user *user);

/**
 * @brief Helper versions of lcz_mqtt_send_data
 */
int lcz_mqtt_send_string(char *data, uint8_t *topic, struct lcz_mqtt_user *user);
int lcz_mqtt_send_binary(char *data, uint32_t len, uint8_t *topic,
			 struct lcz_mqtt_user *user);

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

/**
 * Register callback that occurs when broker has received message
 * with matching id (Qos1 ACK) or a disconnect occurs.
 *
 * An negative status indicates failure.
 *
 * A user/node is required for each simultaneously outstanding message.
 *
 * @param cb Pointer to static callback structure (linked list node)
 */
void lcz_mqtt_register_user(struct lcz_mqtt_user *user);

/**
 * @brief Publish a MQTT message if it is successfully formatted using
 * vsnprintk. Message generation uses calloc.
 *
 * @param cb Callback node, NULL if callback not required
 * @param topic for message
 * @param fmt format string
 * @param ... format string parameters
 * @return int 0 on success, otherwise negative error code
 */
int lcz_mqtt_shadow_format_and_send(struct lcz_mqtt_user *user, char *topic,
				    const char *fmt, ...);

/**
 * @brief Formats a topic string using vsnprintk.
 *
 * @param topic string
 * @param max_size size of topic string
 * @param fmt format string
 * @param ... format string parameters
 * @return int 0 on success, otherwise negative error code
 */
int lcz_mqtt_topic_format(char *topic, size_t max_size, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_MQTT_H__ */
