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

#ifdef __cplusplus
}
#endif

#endif /* __AWS_H__ */
