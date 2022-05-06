/**
 * @file lcz_mqtt.c
 * @brief Wrapped MQTT APIs
 *
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2020-2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lcz_mqtt, CONFIG_LCZ_MQTT_LOG_LEVEL);

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
#include "shadow_parser.h"
#include "errno_str.h"

#include "lcz_mqtt.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define CONNECTION_ATTEMPT_DELAY_MS 500
#define SOCKET_POLL_WAIT_TIME_MSECS 250

#define LCZ_MQTT_MQTT_ID_MAX_SIZE 128

#define LCZ_MQTT_RX_THREAD_STACK_SIZE 2048
#define LCZ_MQTT_RX_THREAD_PRIORITY K_PRIO_COOP(15)

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
K_THREAD_STACK_DEFINE(rx_thread_stack, LCZ_MQTT_RX_THREAD_STACK_SIZE);
static struct k_thread rx_thread;

static struct k_sem connected_sem;
static struct k_sem disconnected_sem;

static uint8_t mqtt_rx_buffer[CONFIG_LCZ_MQTT_RX_BUFFER_SIZE];
static uint8_t mqtt_tx_buffer[CONFIG_LCZ_MQTT_TX_BUFFER_SIZE];
static struct mqtt_client mqtt_client_ctx;
static struct sockaddr_storage mqtt_broker;

static struct pollfd fds[1];
static int nfds;

static struct addrinfo *saddr;

static sec_tag_t m_sec_tags[] = {
	CONFIG_LCZ_CERTS_CA_CERT_TAG,
#if CONFIG_LCZ_CERTS_DEVICE_CERT_TAG != 0
	CONFIG_LCZ_CERTS_DEVICE_CERT_TAG,
#endif
};

static struct k_work_delayable publish_watchdog;
static struct k_work_delayable keep_alive;

static uint8_t subscription_buffer[CONFIG_LCZ_MQTT_SHADOW_IN_MAX_SIZE];
static uint8_t subscription_topic[CONFIG_LCZ_MQTT_SHADOW_TOPIC_MAX_SIZE];

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
} lcz_mqtt;

#ifdef ATTR_ID_mqtt_client_user_name
static struct mqtt_utf8 user_name;
#endif

#ifdef ATTR_ID_mqtt_client_password
static struct mqtt_utf8 password;
#endif

static sys_slist_t callback_list;

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
		   uint32_t len, uint8_t *topic, bool binary,
		   struct lcz_mqtt_user *user);
static void client_init(struct mqtt_client *client);
static int try_to_connect(struct mqtt_client *client);
static void lcz_mqtt_rx_thread(void *arg1, void *arg2, void *arg3);
static uint16_t rand16_nonzero_get(void);
static void publish_watchdog_work_handler(struct k_work *work);
static void keep_alive_work_handler(struct k_work *work);
static void connection_failure_handler(int status);
static void log_json(const char *prefix, size_t size, const char *buffer);
static void restart_publish_watchdog(void);

static void update_ack_id(const struct lcz_mqtt_user *user, uint16_t id);
static void issue_disconnect_callbacks(void);
static void issue_ack_callback(int result, uint16_t id);

/******************************************************************************/
/* Task Init                                                                  */
/******************************************************************************/
static int lcz_mqtt_init(const struct device *device)
{
	ARG_UNUSED(device);

	k_sem_init(&connected_sem, 0, 1);
	k_sem_init(&disconnected_sem, 0, 1);

	k_thread_name_set(
		k_thread_create(&rx_thread, rx_thread_stack,
				K_THREAD_STACK_SIZEOF(rx_thread_stack),
				lcz_mqtt_rx_thread, NULL, NULL, NULL,
				LCZ_MQTT_RX_THREAD_PRIORITY, 0, K_NO_WAIT),
		"lcz_mqtt");

	k_work_init_delayable(&publish_watchdog, publish_watchdog_work_handler);
	k_work_init_delayable(&keep_alive, keep_alive_work_handler);

	return 0;
}

SYS_INIT(lcz_mqtt_init, APPLICATION, 99);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int lcz_mqtt_get_server_addr(void)
{
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	return dns_resolve_server_addr(attr_get_quasi_static(ATTR_ID_endpoint),
				       attr_get_quasi_static(ATTR_ID_port),
				       &hints, &saddr);
}

int lcz_mqtt_connect()
{
	int rc = 0;

	if (!lcz_mqtt.connected) {
		lcz_mqtt.disconnect = false;
		rc = try_to_connect(&mqtt_client_ctx);
		connection_failure_handler(rc);
	}
	return rc;
}

int lcz_mqtt_disconnect(void)
{
	if (lcz_mqtt.connected) {
		lcz_mqtt.disconnect = true;
		LOG_DBG("Waiting to close MQTT connection");
		k_sem_take(&disconnected_sem, K_FOREVER);
		LOG_DBG("MQTT connection closed");
	}

	return 0;
}

int lcz_mqtt_send_string(char *data, uint8_t *topic, struct lcz_mqtt_user *user)
{
	return lcz_mqtt_send_data(false, data, 0, topic, user);
}

int lcz_mqtt_send_binary(char *data, uint32_t len, uint8_t *topic,
			 struct lcz_mqtt_user *user)
{
	return lcz_mqtt_send_data(true, data, len, topic, user);
}

int lcz_mqtt_send_data(bool binary, char *data, uint32_t len, uint8_t *topic,
		       struct lcz_mqtt_user *user)
{
	int rc = -ENOTCONN;
	uint32_t length;

	if (!lcz_mqtt.connected) {
		return rc;
	}

	if (binary) {
		length = len;
	} else {
		length = strlen(data);
	}

	lcz_mqtt.stats.sends += 1;
	lcz_mqtt.stats.tx_payload_bytes += length;

	rc = publish(&mqtt_client_ctx, MQTT_QOS_1_AT_LEAST_ONCE, data, length,
		     topic, binary, user);

	if (rc == 0) {
		lcz_mqtt.stats.success += 1;
		lcz_mqtt.stats.consecutive_fails = 0;
		restart_publish_watchdog();
	} else {
		lcz_mqtt.stats.failure += 1;
		lcz_mqtt.stats.consecutive_fails += 1;
		LOG_ERR("MQTT publish err %u (%d)", lcz_mqtt.stats.failure, rc);
	}

	return rc;
}

bool lcz_mqtt_connected(void)
{
	return lcz_mqtt.connected;
}

bool lcz_mqtt_published(void)
{
	return (lcz_mqtt.stats.success > 0);
}

int lcz_mqtt_subscribe(uint8_t *topic, uint8_t subscribe)
{
	struct mqtt_topic mt;
	int rc = -EPERM;
	const char *const str = subscribe ? "Subscribed" : "Unsubscribed";
	struct mqtt_subscription_list list = {
		.list = &mt, .list_count = 1, .message_id = rand16_nonzero_get()
	};

	if (!lcz_mqtt.connected) {
		return rc;
	}

	mt.topic.utf8 = topic;
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
struct mqtt_client *lcz_mqtt_get_mqtt_client(void)
{
	return &mqtt_client_ctx;
}

void lcz_mqtt_register_user(struct lcz_mqtt_user *user)
{
	sys_slist_append(&callback_list, &user->node);
}

int lcz_mqtt_shadow_format_and_send(struct lcz_mqtt_user *user, char *topic,
				    const char *fmt, ...)
{
	va_list ap;
	char *msg = NULL;
	int actual_size;
	int req_size;
	int r;

	va_start(ap, fmt);
	do {
		/* determine size of message */
		req_size = vsnprintk(msg, 0, fmt, ap);
		if (req_size < 0) {
			r = -EINVAL;
			break;
		}
		/* add one for null character */
		req_size += 1;

		msg = k_calloc(req_size, sizeof(char));
		if (msg == NULL) {
			LOG_ERR("Unable to allocate message");
			r = -ENOMEM;
			break;
		}

		/* build actual message and try to send it */
		actual_size = vsnprintk(msg, req_size, fmt, ap);

		if (actual_size > 0 && actual_size < req_size) {
			r = lcz_mqtt_send_string(msg, topic, user);
		} else {
			LOG_ERR("Unable to format and send MQTT message");
			r = -EINVAL;
		}

	} while (0);

	va_end(ap);
	k_free(msg);
	return r;
}

int lcz_mqtt_topic_format(char *topic, size_t max_size, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);

	r = vsnprintk(topic, max_size, fmt, ap);
	if (r < 0) {
		LOG_ERR("Error encoding topic string");
	} else if (r >= max_size) {
		LOG_ERR("Topic string too small %d (desired) >= %d (max)", r,
			max_size);
		r = -EINVAL;
	} else {
		r = 0;
	}

	va_end(ap);
	return r;
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

		lcz_mqtt.connected = true;
		k_sem_give(&connected_sem);
		LOG_INF("MQTT client connected!");
		k_work_schedule(&keep_alive, K_NO_WAIT);
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);
		lcz_mqtt.connected = false;
		lcz_mqtt.disconnect = true;
		k_work_cancel_delayable(&keep_alive);
		lcz_mqtt.stats.disconnects += 1;
		break;

	case MQTT_EVT_PUBACK:
		issue_ack_callback(evt->result, evt->param.puback.message_id);

		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		/* Unless a single caller/semaphore is used the delta isn't foolproof. */
		lcz_mqtt.stats.acks += 1;
		lcz_mqtt.stats.delta = k_uptime_delta(&lcz_mqtt.stats.time);
		lcz_mqtt.stats.delta_max =
			MAX(lcz_mqtt.stats.delta_max, lcz_mqtt.stats.delta);

		LOG_DBG("PUBACK packet id: %u delta: %d",
			evt->param.puback.message_id,
			(int32_t)lcz_mqtt.stats.delta);

		break;

	case MQTT_EVT_PUBLISH:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBLISH error %d", evt->result);
			break;
		}
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
	uint32_t topic_length = evt->param.publish.message.topic.topic.size;

	/* Terminate topic and shadow so that they can be printed and used
	 * as strings by other modules.
	 */

	size_t size = length + 1;
	if (size > CONFIG_LCZ_MQTT_SHADOW_IN_MAX_SIZE) {
		LOG_ERR("Shadow buffer too small");
		return 0;
	}

	if (topic_length + 1 > CONFIG_LCZ_MQTT_SHADOW_IN_MAX_SIZE) {
		LOG_ERR("Shadow topic too small");
		return 0;
	} else {
		memcpy(subscription_topic, topic, topic_length);
		subscription_topic[topic_length] = 0;
	}

	LOG_INF("MQTT RXd ID: %d payload len: %d", id, length);
	if (IS_ENABLED(CONFIG_LCZ_MQTT_LOG_MQTT_SUBSCRIPTION_TOPIC)) {
		log_json("Subscription topic", topic_length,
			 subscription_topic);
	}

	lcz_mqtt.stats.rx_payload_bytes += length;

	rc = mqtt_read_publish_payload(client, subscription_buffer, length);
	if (rc == length) {
		subscription_buffer[length] = 0; /* null terminate */

		if (IS_ENABLED(CONFIG_LCZ_MQTT_LOG_MQTT_SUBSCRIPTION)) {
			log_json("Subscription data", rc, subscription_buffer);
		}

		shadow_parser(subscription_topic, subscription_buffer);

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
		   uint32_t len, uint8_t *topic, bool binary,
		   struct lcz_mqtt_user *user)
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

	if (IS_ENABLED(CONFIG_LCZ_MQTT_LOG_MQTT_PUBLISH_TOPIC)) {
		log_json("Publish Topic", param.message.topic.topic.size,
			 param.message.topic.topic.utf8);
	}

	if (!binary && IS_ENABLED(CONFIG_LCZ_MQTT_LOG_MQTT_PUBLISH)) {
		log_json("Publish string", len, data);
	}

	if (mqtt_client_ctx.tx_buf_size < len) {
		LOG_WRN("len: %u", len);
	}

	lcz_mqtt.stats.time = k_uptime_get();

	update_ack_id(user, param.message_id);

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
	client->client_id.utf8 = lcz_mqtt_get_mqtt_client_id();
	client->client_id.size = strlen(client->client_id.utf8);

	/* User name and password are used by Losant */
#ifdef ATTR_ID_mqtt_client_user_name
	user_name.utf8 = attr_get_quasi_static(ATTR_ID_mqtt_client_user_name);
	user_name.size = strlen(user_name.utf8);
	client->user_name = &user_name;
#else
	client->user_name = NULL;
#endif

#ifdef ATTR_ID_mqtt_client_password
	password.utf8 = attr_get_quasi_static(ATTR_ID_mqtt_client_password);
	password.size = strlen(password.utf8);
	client->password = &password;
#else
	client->password = NULL;
#endif

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

	while (i++ < CONFIG_LCZ_MQTT_CONNECT_TRIES && !lcz_mqtt.connected) {
		client_init(client);

		LOG_INF("Attempting to connect %s to MQTT Broker...",
			(char *)client->client_id.utf8);

		rc = mqtt_connect(client);
		if (rc != 0) {
			LOG_ERR("mqtt_connect (%d)", rc);
			k_sleep(K_MSEC(CONNECTION_ATTEMPT_DELAY_MS));
			continue;
		}

		prepare_fds(client);

		wait(CONNECTION_ATTEMPT_DELAY_MS);
		mqtt_input(client);

		if (!lcz_mqtt.connected) {
			mqtt_abort(client);
		}
	}

	if (lcz_mqtt.connected) {
		return 0;
	}

	return -EINVAL;
}

static void lcz_mqtt_rx_thread(void *arg1, void *arg2, void *arg3)
{
	while (true) {
		if (lcz_mqtt.connected) {
			/* Wait for socket RX data */
			wait(SOCKET_POLL_WAIT_TIME_MSECS);
			/* process MQTT RX data */
			mqtt_input(&mqtt_client_ctx);
			/* Disconnect (request) flag is set from the disconnect callback
			 * and from a user request.
			 */
			if (lcz_mqtt.disconnect) {
				LOG_DBG("Closing MQTT connection");
				mqtt_disconnect(&mqtt_client_ctx);
				clear_fds();
				lcz_mqtt.disconnect = false;
				lcz_mqtt.connected = false;
				k_sem_give(&disconnected_sem);
				lcz_mqtt_disconnect_callback();
				issue_disconnect_callbacks();
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

	LOG_WRN("Unable to publish MQTT in the last hour");

	if (lcz_mqtt_ignore_publish_watchdog()) {
		restart_publish_watchdog();
	} else {
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
	int time_left;
	k_timeout_t delay;
	int rc;

	/* The accuracy of mqtt_live may not be good enough to support
	 * the broker's maximum keep alive timeout.
	 *
	 * For example, the absolute maximum of Losant is 20 minutes +/- 10 seconds.
	 * If the keep alive is set to 1200, then disconnections will occur.
	 * If the keep alive is set to 1170 (19.5 minutes), then the Losant timeout
	 * will be 20 minutes and disconnects will not occur.
	 * If the keep alive is set to 120 (2 minutes), then the actual timeout is
	 * 3 minutes.
	 */
	if (lcz_mqtt.connected) {
		rc = mqtt_live(&mqtt_client_ctx);
		if (rc != 0 && rc != -EAGAIN) {
			LOG_ERR("mqtt_live error: %s", ERRNO_STR(rc));
		} else if (IS_ENABLED(CONFIG_LCZ_MQTT_KEEPALIVE_VERBOSE)) {
			LOG_INF("mqtt_live status: %s",
				(rc == -EAGAIN) ? "try again" : ERRNO_STR(rc));
		}

		if (CONFIG_MQTT_KEEPALIVE != 0) {
			time_left = mqtt_keepalive_time_left(&mqtt_client_ctx);
			delay = K_MSEC(time_left);
			k_work_schedule(&keep_alive, delay);
			if (IS_ENABLED(CONFIG_LCZ_MQTT_KEEPALIVE_VERBOSE)) {
				LOG_INF("Scheduled next keep alive: %d ms",
					time_left);
			}
		}
	} else if (IS_ENABLED(CONFIG_LCZ_MQTT_KEEPALIVE_VERBOSE)) {
		LOG_INF("MQTT Not Connected - Keep alive not sent");
	}
}

static void connection_failure_handler(int status)
{
	if (status != 0) {
		lcz_mqtt.stats.consecutive_connection_failures += 1;
		if ((lcz_mqtt.stats.consecutive_connection_failures >
		     CONFIG_LCZ_MQTT_MAX_CONSECUTIVE_CONNECTION_FAILURES) &&
		    (CONFIG_LCZ_MQTT_MAX_CONSECUTIVE_CONNECTION_FAILURES !=
		     0)) {
			LOG_WRN("Maximum consecutive connection failures exceeded");
			lcz_software_reset_after_assert(0);
		}
	} else {
		lcz_mqtt.stats.consecutive_connection_failures = 0;
	}
}

static void restart_publish_watchdog(void)
{
	if (CONFIG_LCZ_MQTT_PUBLISH_WATCHDOG_SECONDS != 0) {
		k_work_reschedule(
			&publish_watchdog,
			K_SECONDS(CONFIG_LCZ_MQTT_PUBLISH_WATCHDOG_SECONDS));
	}
}

static void update_ack_id(const struct lcz_mqtt_user *user, uint16_t id)
{
	struct lcz_mqtt_user *iterator;

	if (user == NULL) {
		return;
	}

	SYS_SLIST_FOR_EACH_CONTAINER (&callback_list, iterator, node) {
		if (user == iterator) {
			iterator->ack_id = id;
			if (user->ack_callback == NULL) {
				LOG_WRN("Ack Callback is null");
			}
			return;
		}
	}

	LOG_ERR("User not found");
}

/* When disconnected, set ID to zero and notify users of failure to send. */
static void issue_disconnect_callbacks(void)
{
	struct lcz_mqtt_user *iterator;

	SYS_SLIST_FOR_EACH_CONTAINER (&callback_list, iterator, node) {
		iterator->ack_id = 0;
		if (iterator->ack_callback != NULL) {
			iterator->ack_callback(-ENOTCONN);
		}
	}
}

static void issue_ack_callback(int result, uint16_t id)
{
	struct lcz_mqtt_user *iterator;

	SYS_SLIST_FOR_EACH_CONTAINER (&callback_list, iterator, node) {
		if (iterator->ack_id == id) {
			if (id == 0) {
				LOG_ERR("Invalid ID in user callback");
			}
			if (iterator->ack_callback != NULL) {
				if (result == 0) {
					iterator->ack_callback(id);
				} else {
					iterator->ack_callback(result);
				}
			}
			iterator->ack_id = 0;
			return;
		}
	}
}

/******************************************************************************/
/* Override in application                                                    */
/******************************************************************************/
__weak void lcz_mqtt_disconnect_callback(void)
{
	return;
}

__weak bool lcz_mqtt_ignore_publish_watchdog(void)
{
	return false;
}

__weak const uint8_t *lcz_mqtt_get_mqtt_client_id(void)
{
	return attr_get_quasi_static(ATTR_ID_client_id);
}