/**
 * @file aws.c
 * @brief AWS APIs
 *
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2020-2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(aws, CONFIG_AWS_LOG_LEVEL);

#define LOG_ACK(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <mbedtls/ssl.h>
#include <net/socket.h>
#include <stdio.h>
#include <kernel.h>
#include <random/rand32.h>
#include <init.h>
#include <sys/printk.h>

#include "lcz_dns.h"
#include "lcz_software_reset.h"
#include "attr.h"
#include "fota_smp.h"

#include "aws.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifndef CONFIG_MQTT_LIB_TLS
#error "AWS must use MQTT with TLS"
#endif

#define APP_SLEEP_MSECS 500
#define SOCKET_POLL_WAIT_TIME_MSECS 250

#define AWS_MQTT_ID_MAX_SIZE 128

#define AWS_RX_THREAD_STACK_SIZE 2048
#define AWS_RX_THREAD_PRIORITY K_PRIO_COOP(15)

struct topics {
	uint8_t update[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t update_delta[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t get[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t get_accepted[CONFIG_AWS_TOPIC_MAX_SIZE];
};

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
K_THREAD_STACK_DEFINE(rx_thread_stack, AWS_RX_THREAD_STACK_SIZE);
static struct k_thread rx_thread;

static struct k_sem connected_sem;
static struct k_sem disconnected_sem;

static uint8_t mqtt_rx_buffer[CONFIG_AWS_RX_BUFFER_SIZE];
static uint8_t mqtt_tx_buffer[CONFIG_AWS_TX_BUFFER_SIZE];
static char mqtt_random_id[AWS_MQTT_ID_MAX_SIZE];
static struct mqtt_client mqtt_client_ctx;
static struct sockaddr_storage mqtt_broker;

static struct pollfd fds[1];
static int nfds;

static struct addrinfo *saddr;

static sec_tag_t m_sec_tags[] = {
	CONFIG_APP_CA_CERT_TAG,
	CONFIG_APP_DEVICE_CERT_TAG,
};

static struct topics topics;

static struct k_work_delayable publish_watchdog;
static struct k_work_delayable keep_alive;

static uint8_t subscription_buffer[CONFIG_AWS_SHADOW_IN_MAX_SIZE];

static struct {
	bool connected;
	bool disconnect;
	struct {
		uint32_t consecutive_connection_failures;
		uint32_t disconnects;
		uint32_t sends;
		uint32_t acks;
		uint32_t success;
		uint32_t failure;
		uint32_t consecutive_fails;
		int64_t time;
		int64_t delta;
		int64_t delta_max;
		uint32_t tx_payload_bytes;
		uint32_t rx_payload_bytes;
	} stats;
} aws;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void prepare_fds(struct mqtt_client *client);
static void clear_fds(void);
static void wait(int timeout);
static void mqtt_evt_handler(struct mqtt_client *const client,
			     const struct mqtt_evt *evt);
static int subscription_handler(struct mqtt_client *const client,
				const struct mqtt_evt *evt);
static void subscription_flush(struct mqtt_client *const client, size_t length);
static int publish(struct mqtt_client *client, enum mqtt_qos qos, char *data,
		   uint32_t len, uint8_t *topic, bool binary);
static void client_init(struct mqtt_client *client);
static int try_to_connect(struct mqtt_client *client);
static void aws_rx_thread(void *arg1, void *arg2, void *arg3);
static uint16_t rand16_nonzero_get(void);
static void publish_watchdog_work_handler(struct k_work *work);
static void keep_alive_work_handler(struct k_work *work);
static int send_data(bool binary, char *data, uint32_t len, uint8_t *topic);
static void generate_client_id(void);
static void connection_failure_handler(int status);
static void log_json(const char *prefix, size_t size, const char *buffer);

/******************************************************************************/
/* Task Init                                                                  */
/******************************************************************************/
static int aws_init(const struct device *device)
{
	ARG_UNUSED(device);

	k_sem_init(&connected_sem, 0, 1);
	k_sem_init(&disconnected_sem, 0, 1);

	k_thread_name_set(
		k_thread_create(&rx_thread, rx_thread_stack,
				K_THREAD_STACK_SIZEOF(rx_thread_stack),
				aws_rx_thread, NULL, NULL, NULL,
				AWS_RX_THREAD_PRIORITY, 0, K_NO_WAIT),
		"aws");

	k_work_init_delayable(&publish_watchdog, publish_watchdog_work_handler);
	k_work_init_delayable(&keep_alive, keep_alive_work_handler);

	return 0;
}

SYS_INIT(aws_init, APPLICATION, 99);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int aws_get_server_addr(void)
{
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	return dns_resolve_server_addr(attr_get_quasi_static(ATTR_ID_endpoint),
				       attr_get_quasi_static(ATTR_ID_port),
				       &hints, &saddr);
}

int aws_connect()
{
	int rc = 0;

	if (!aws.connected) {
		aws.disconnect = false;
		rc = try_to_connect(&mqtt_client_ctx);
		if (rc != 0) {
			LOG_ERR("AWS connect err (%d)", rc);
		}
		connection_failure_handler(rc);
	}
	return rc;
}

int aws_disconnect(void)
{
	if (aws.connected) {
		aws.disconnect = true;
		LOG_DBG("Waiting to close MQTT connection");
		k_sem_take(&disconnected_sem, K_FOREVER);
		LOG_DBG("MQTT connection closed");
	}

	return 0;
}

int aws_send_data(char *data, uint8_t *topic)
{
	/* If the topic is NULL, then publish to the gateway (Pinnacle-100) topic.
	 * Otherwise, publish to the provided topic.
	 */
	if (topic == NULL) {
		return send_data(false, data, 0, topics.update);
	} else {
		return send_data(false, data, 0, topic);
	}
}

int aws_send_bin_data(char *data, uint32_t len, uint8_t *topic)
{
	if (topic == NULL) {
		/* don't publish binary data to the default topic (device shadow) */
		return -EOPNOTSUPP;
	} else {
		return send_data(true, data, len, topic);
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

bool aws_connected(void)
{
	return aws.connected;
}

bool aws_published(void)
{
	return (aws.stats.success > 0);
}

int aws_subscribe(uint8_t *topic, uint8_t subscribe)
{
	struct mqtt_topic mt;
	int rc = -EPERM;
	const char *const str = subscribe ? "Subscribed" : "Unsubscribed";
	struct mqtt_subscription_list list = {
		.list = &mt, .list_count = 1, .message_id = rand16_nonzero_get()
	};

	if (!aws.connected) {
		return rc;
	}

	if (topic == NULL) {
		mt.topic.utf8 = topics.update_delta;
	} else {
		mt.topic.utf8 = topic;
	}
	mt.topic.size = strlen(mt.topic.utf8);
	mt.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	__ASSERT(mt.topic.size != 0, "Invalid topic");
	rc = subscribe ? mqtt_subscribe(&mqtt_client_ctx, &list) :
			       mqtt_unsubscribe(&mqtt_client_ctx, &list);
	if (rc != 0) {
		LOG_ERR("%s status %d to %s", str, rc,
			    log_strdup(mt.topic.utf8));
	} else {
		LOG_DBG("%s to %s", str, log_strdup(mt.topic.utf8));
	}
	return rc;
}


/**
 * @note This is used to abstract MEMFAULT data forwarding and shouldn't be
 * used for anything else.
 */
struct mqtt_client *aws_get_mqtt_client(void)
{
	return &mqtt_client_ctx;
}

char *aws_get_gateway_update_delta_topic()
{
	return topics.update;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void prepare_fds(struct mqtt_client *client)
{
	fds[0].fd = client->transport.tls.sock;
	fds[0].events = ZSOCK_POLLIN;
	nfds = 1;
}

static void clear_fds(void)
{
	fds[0].fd = 0;
	nfds = 0;
}

static void wait(int timeout)
{
	if (nfds > 0) {
		if (poll(fds, nfds, timeout) < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}
}

static void mqtt_evt_handler(struct mqtt_client *const client,
			     const struct mqtt_evt *evt)
{
	int rc;
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}

		aws.connected = true;
		k_sem_give(&connected_sem);
		LOG_INF("MQTT client connected!");
		k_work_schedule(&keep_alive,
				K_SECONDS(CONFIG_MQTT_KEEPALIVE / 2));
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);
		aws.connected = false;
		aws.disconnect = true;
		k_work_cancel_delayable(&keep_alive);
		aws.stats.disconnects += 1;
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		/* Unless a single caller/semaphore is used the delta isn't foolproof. */
		aws.stats.acks += 1;
		aws.stats.delta = k_uptime_delta(&aws.stats.time);
		aws.stats.delta_max = MAX(aws.stats.delta_max, aws.stats.delta);

		LOG_ACK("PUBACK packet id: %u delta: %d",
			    evt->param.puback.message_id,
			    (int32_t)aws.stats.delta);

		break;

	case MQTT_EVT_PUBLISH:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBLISH error %d", evt->result);
			break;
		}

		LOG_INF(
			"MQTT RXd ID: %d\r\n \tTopic: %s len: %d",
			evt->param.publish.message_id,
			log_strdup(evt->param.publish.message.topic.topic.utf8),
			evt->param.publish.message.payload.len);

		rc = subscription_handler(client, evt);
		if ((rc >= 0) &&
		    (rc < evt->param.publish.message.payload.len)) {
			subscription_flush(
				client,
				evt->param.publish.message.payload.len - rc);
		}
		break;
	default:
		break;
	}
}

/* Log system v2 cannot be used because buffer may be too large. */
void log_json(const char *prefix, size_t size, const char *buffer)
{
	if (buffer[size] == 0) {
		printk("%s size: %u data: %s\r\n", prefix, size, buffer);
	} else {
		LOG_ERR("Logging JSON as a string requires NULL terminator");
	}
}

/* The timestamps can make the shadow size larger than 4K.
 * A static buffer is used for processing.
 */
static int subscription_handler(struct mqtt_client *const client,
				const struct mqtt_evt *evt)
{
	int rc = 0;

	uint16_t id = evt->param.publish.message_id;
	uint32_t length = evt->param.publish.message.payload.len;
	uint8_t qos = evt->param.publish.message.topic.qos;
	const uint8_t *topic = evt->param.publish.message.topic.topic.utf8;

	/* Leave room for null to allow easy printing */
	size_t size = length + 1;
	if (size > CONFIG_AWS_SHADOW_IN_MAX_SIZE) {
		return 0;
	}

	aws.stats.rx_payload_bytes += length;

	rc = mqtt_read_publish_payload(client, subscription_buffer, length);
	if (rc == length) {
		subscription_buffer[length] = 0; /* null terminate */

		if (IS_ENABLED(CONFIG_AWS_LOG_MQTT_RX_DATA)) {
			log_json("MQTT Read data", rc, subscription_buffer);
		}

		aws_subscription_handler_callback(topic, subscription_buffer);

		if (qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			struct mqtt_puback_param param = { .message_id = id };
			(void)mqtt_publish_qos1_ack(client, &param);
		} else if (qos == MQTT_QOS_2_EXACTLY_ONCE) {
			LOG_ERR("QOS 2 not supported");
		}
	}

	return rc;
}

static void subscription_flush(struct mqtt_client *const client, size_t length)
{
	LOG_ERR("Subscription Flush %u", length);
	char junk;
	size_t i;
	for (i = 0; i < length; i++) {
		if (mqtt_read_publish_payload(client, &junk, 1) != 1) {
			break;
		}
	}
}

static int publish(struct mqtt_client *client, enum mqtt_qos qos, char *data,
		   uint32_t len, uint8_t *topic, bool binary)
{
	struct mqtt_publish_param param;

	memset(&param, 0, sizeof(struct mqtt_publish_param));
	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = topic;
	param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
	param.message.payload.data = data;
	param.message.payload.len = len;
	param.message_id = rand16_nonzero_get();
	param.dup_flag = 0U;
	param.retain_flag = 0U;

	if (IS_ENABLED(CONFIG_AWS_LOG_MQTT_TOPIC)) {
		log_json("Publish Topic", param.message.topic.topic.size,
			 param.message.topic.topic.utf8);
	}

	if (!binary && IS_ENABLED(CONFIG_AWS_LOG_MQTT_PUBLISH)) {
		log_json("Publish string", len, data);
	}

	/* Does the tx buf size matter? */
	if (mqtt_client_ctx.tx_buf_size < len) {
		LOG_WRN("len: %u", len);
	}

	aws.stats.time = k_uptime_get();

	return mqtt_publish(client, &param);
}

static void broker_init(void)
{
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&mqtt_broker;

	broker4->sin_family = saddr->ai_family;
	broker4->sin_port =
		htons(strtol(attr_get_quasi_static(ATTR_ID_port), NULL, 0));
	net_ipaddr_copy(&broker4->sin_addr, &net_sin(saddr->ai_addr)->sin_addr);
}

static void client_init(struct mqtt_client *client)
{
	mqtt_client_init(client);

	broker_init();

	/* MQTT client configuration */
	client->broker = &mqtt_broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = mqtt_random_id;
	client->client_id.size = strlen(mqtt_random_id);
	client->password = NULL;
	client->user_name = NULL;

	/* MQTT buffers configuration */
	client->rx_buf = mqtt_rx_buffer;
	client->rx_buf_size = sizeof(mqtt_rx_buffer);
	client->tx_buf = mqtt_tx_buffer;
	client->tx_buf_size = sizeof(mqtt_tx_buffer);

	/* MQTT transport configuration */
	client->transport.type = MQTT_TRANSPORT_SECURE;
	struct mqtt_sec_config *tls_config = &client->transport.tls.config;
	tls_config->peer_verify =
		attr_get_signed32(ATTR_ID_peer_verify, MBEDTLS_SSL_VERIFY_NONE);
	tls_config->cipher_list = NULL;
	tls_config->sec_tag_list = m_sec_tags;
	tls_config->sec_tag_count = ARRAY_SIZE(m_sec_tags);
	tls_config->hostname = attr_get_quasi_static(ATTR_ID_endpoint);
}

/* In this routine we block until the connected variable is 1 */
static int try_to_connect(struct mqtt_client *client)
{
	int rc, i = 0;

	generate_client_id();

	LOG_INF("Attempting to connect %s to AWS...",
		log_strdup(mqtt_random_id));

	while (i++ < CONFIG_AWS_CONNECT_TRIES && !aws.connected) {
		client_init(client);

		rc = mqtt_connect(client);
		if (rc != 0) {
			LOG_ERR("mqtt_connect (%d)", rc);
			k_sleep(K_MSEC(APP_SLEEP_MSECS));
			continue;
		}

		prepare_fds(client);

		wait(APP_SLEEP_MSECS);
		mqtt_input(client);

		if (!aws.connected) {
			mqtt_abort(client);
		}
	}

	if (aws.connected) {
		return 0;
	}

	return -EINVAL;
}

static void aws_rx_thread(void *arg1, void *arg2, void *arg3)
{
	while (true) {
		if (aws.connected) {
			/* Wait for socket RX data */
			wait(SOCKET_POLL_WAIT_TIME_MSECS);
			/* process MQTT RX data */
			mqtt_input(&mqtt_client_ctx);
			/* Disconnect (request) flag is set from the disconnect callback
			 * and from a user request.
			 */
			if (aws.disconnect) {
				LOG_DBG("Closing MQTT connection");
				mqtt_disconnect(&mqtt_client_ctx);
				clear_fds();
				aws.disconnect = false;
				aws.connected = false;
				k_sem_give(&disconnected_sem);
				aws_disconnect_callback();
			}
		} else {
			LOG_DBG("Waiting for MQTT connection...");
			/* Wait for connection */
			k_sem_reset(&connected_sem);
			k_sem_take(&connected_sem, K_FOREVER);
		}
	}
}

/* Message ID of zero is reserved as invalid. */
static uint16_t rand16_nonzero_get(void)
{
	uint16_t r = 0;
	do {
		r = (uint16_t)sys_rand32_get();
	} while (r == 0);
	return r;
}

static void publish_watchdog_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_WRN("Unable to publish (AWS) in the last hour");

	if (!fota_smp_ble_prepared()) {
#ifdef CONFIG_MODEM_HL7800
		if (attr_get_signed32(ATTR_ID_modem_functionality, 0) !=
		    MODEM_FUNCTIONALITY_AIRPLANE) {
			lcz_software_reset_after_assert(0);
		}
#else
		lcz_software_reset_after_assert(0);
#endif
	}
}

static void keep_alive_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	int rc;

	if (aws.connected) {
		rc = mqtt_live(&mqtt_client_ctx);
		if (rc != 0 && rc != -EAGAIN) {
			LOG_ERR("mqtt_live (%d)", rc);
		}

		k_work_schedule(&keep_alive, K_SECONDS(CONFIG_MQTT_KEEPALIVE));
	}
}

static int send_data(bool binary, char *data, uint32_t len, uint8_t *topic)
{
	int rc = -ENOTCONN;
	uint32_t length;

	if (!aws.connected) {
		return rc;
	}

	if (binary) {
		length = len;
	} else {
		length = strlen(data);
	}

	aws.stats.sends += 1;
	aws.stats.tx_payload_bytes += length;

	rc = publish(&mqtt_client_ctx, MQTT_QOS_1_AT_LEAST_ONCE, data, length, topic,
		     binary);

	if (rc == 0) {
		aws.stats.success += 1;
		aws.stats.consecutive_fails = 0;
		if (CONFIG_AWS_PUBLISH_WATCHDOG_SECONDS != 0) {
			k_work_reschedule(
				&publish_watchdog,
				K_SECONDS(CONFIG_AWS_PUBLISH_WATCHDOG_SECONDS));
		}
	} else {
		aws.stats.failure += 1;
		aws.stats.consecutive_fails += 1;
		LOG_ERR("MQTT publish err %u (%d)", aws.stats.failure, rc);
	}

	return rc;
}

/* Add randomness to client id to make each connection unique.*/
static void generate_client_id(void)
{
	size_t id_len;

	strncpy(mqtt_random_id, attr_get_quasi_static(ATTR_ID_client_id),
		AWS_MQTT_ID_MAX_SIZE);

	id_len = strlen(mqtt_random_id);

	snprintf(mqtt_random_id + id_len, AWS_MQTT_ID_MAX_SIZE - id_len,
		 "_%08x", sys_rand32_get());
}

static void connection_failure_handler(int status)
{
	if (status != 0) {
		aws.stats.consecutive_connection_failures += 1;
		if ((aws.stats.consecutive_connection_failures >
		     CONFIG_AWS_MAX_CONSECUTIVE_CONNECTION_FAILURES) &&
		    (CONFIG_AWS_MAX_CONSECUTIVE_CONNECTION_FAILURES != 0)) {
			LOG_WRN("Maximum consecutive connection failures exceeded");
			lcz_software_reset_after_assert(0);
		}
	} else {
		aws.stats.consecutive_connection_failures = 0;
	}
}

/******************************************************************************/
/* Override in application                                                    */
/******************************************************************************/
__weak void aws_disconnect_callback(void)
{
	return;
}

__weak void aws_subscription_handler_callback(const char *topic, const char *json)
{
	return;
}
