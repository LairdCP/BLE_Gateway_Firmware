/**
 * @file aws.h
 * @brief Amazon Web Services API
 *
 * Copyright (c) 2020-2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __AWS_H__
#define __AWS_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <net/mqtt.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define GATEWAY_TOPIC NULL

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief Resolve the server address using DNS
 *
 * @return int 0 on success, else negative errno
 */
int aws_get_server_addr(void);

/**
 * @brief Connect to AWS
 *
 * @note If CONFIG_AWS_MAX_CONSECUTIVE_CONNECTION_FAILURES is non-zero, then
 * consecutive connection failures will cause a reset.
 *
 * @return int 0 on success, else negative errno
 */
int aws_connect(void);

/**
 * @brief Disconnect from AWS
 *
 * @return int 0 on success, else negative errno
 */
int aws_disconnect(void);

/**
 * @brief Send MQTT data
 *
 * @param data null terminated string
 * @param topic null terminated topic string
 * @return int 0 on success, else negative errno
 */
int aws_send_data(char *data, uint8_t *topic);

/**
 * @brief Send Binary MQTT data
 *
 * @param data binary data
 * @param len length of data
 * @param topic null terminated topic to publish to
 * @return int 0 on success, else negative errno
 */
int aws_send_bin_data(char *data, uint32_t len, uint8_t *topic);

/**
 * @brief Subscribe or Unsubscribe to a topic
 *
 * @param topic null terminated topic string
 * @param subscribe true to subscribe, false to unsubscribe
 * @return int 0 on success, else negative errno
 */
int aws_subscribe(uint8_t *topic, uint8_t subscribe);

/**
 * @brief Publish to the get topic so that the shadow can be
 * received.
 *
 * @return int 0 on success, else negative errno
 */
int aws_get_shadow(void);

/**
 * @brief Helper function to subscribe to /get/accepted.
 *
 * @return int 0 on success, else negative errno
 */
int aws_subscribe_to_get_accepted(void);

/**
 * @brief Helper function to unsubscribe from /get/accepted.
 *
 * @return int 0 on success, else negative errno
 */
int aws_unsubscribe_from_get_accepted(void);

/**
 * @brief Generate topics of the form
 * $aws/things/deviceId-%s/shadow/ + {update, update/delta, get, get/accepted}
 *
 * @param id device id string
 */
void aws_generate_gateway_topics(const char *id);

/**
 * @brief Get pointer to update delta topic
 *
 * @return char* topic string
 */
char *aws_get_gateway_update_delta_topic(void);

/**
 * @return true if AWS is connected
 * @return false otherwise
 */
bool aws_connected(void);

/**
 * @return true if at least one publish has been successful
 * @return false otherwise
 */
bool aws_published(void);

/**
 * @brief Weak callback that can be overridden by application
 */
void aws_disconnect_callback(void);

#ifdef __cplusplus
}
#endif

#endif /* __AWS_H__ */
