/**
 * @file aws.c
 * @brief AWS (MQTT) APIs
 *
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2020-2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(aws, CONFIG_AWS_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>
#include <random/rand32.h>

#include "attr.h"
#include "fota_smp.h"
#include "lcz_mqtt.h"

#include "aws.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifndef CONFIG_MQTT_LIB_TLS
#error "AWS must use MQTT with TLS"
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static uint8_t mqtt_random_id[AWS_MQTT_ID_MAX_SIZE];

static struct topics {
	uint8_t update[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t update_delta[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t get[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t get_accepted[CONFIG_AWS_TOPIC_MAX_SIZE];
};

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int aws_send_data(char *data, uint8_t *topic)
{
	/* If the topic is NULL, then publish to the gateway (Pinnacle-100) topic.
	 * Otherwise, publish to the provided topic.
	 */
	if (topic == NULL) {
		return lcz_mqtt_send_data(false, data, 0, topics.update);
	} else {
		return lcz_mqtt_send_data(false, data, 0, topic);
	}
}

int aws_send_bin_data(char *data, uint32_t len, uint8_t *topic)
{
	if (topic == NULL) {
		/* don't publish binary data to the default topic (device shadow) */
		return -EOPNOTSUPP;
	} else {
		return lcz_mqtt_send_data(true, data, len, topic);
	}
}

void aws_generate_gateway_topics(const char *id)
{
	snprintk(topics.update, sizeof(topics.update),
		 "$aws/things/deviceId-%s/shadow/update", id);

	snprintk(topics.update_delta, sizeof(topics.update_delta),
		 "$aws/things/deviceId-%s/shadow/update/delta", id);

	snprintk(topics.get_accepted, sizeof(topics.get_accepted),
		 "$aws/things/deviceId-%s/shadow/get/accepted", id);

	snprintk(topics.get, sizeof(topics.get),
		 "$aws/things/deviceId-%s/shadow/get", id);
}

/* On power-up, get the shadow by sending a message to the /get topic */
int aws_subscribe_to_get_accepted(void)
{
	int rc = aws_subscribe(topics.get_accepted, true);
	if (rc != 0) {
		LOG_ERR("Unable to subscribe to /../get/accepted");
	}
	return rc;
}

int aws_get_shadow(void)
{
	char msg[] = "{\"message\":\"Hello, from Laird Connectivity\"}";
	int rc = send_data(false, msg, 0, topics.get);
	if (rc != 0) {
		LOG_ERR("Unable to get shadow");
	}
	return rc;
}

int aws_unsubscribe_from_get_accepted(void)
{
	return aws_subscribe(topics.get_accepted, false);
}

char *aws_get_gateway_update_delta_topic()
{
	return topics.update;
}

int aws_subscribe(uint8_t *topic, uint8_t subscribe)
{
	return lcz_mqtt_subscribe(
		((topic == NULL) ? topics.update_delta : topic), subscribe);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
bool lcz_mqtt_ignore_publish_watchdog(void)
{
	return fota_smp_ble_prepared();
}

/* Add randomness to client id to make each connection unique.*/
const uint8_t *lcz_mqtt_get_mqtt_client_id(void)
{
	size_t id_len;

	strncpy(mqtt_random_id, attr_get_quasi_static(ATTR_ID_client_id),
		AWS_MQTT_ID_MAX_SIZE);

	id_len = strlen(mqtt_random_id);

	snprintf(mqtt_random_id + id_len, AWS_MQTT_ID_MAX_SIZE - id_len,
		 "_%08x", sys_rand32_get());

	return mqtt_random_id;
}
