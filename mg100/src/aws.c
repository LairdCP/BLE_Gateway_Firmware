/**
 * @file aws.c
 * @brief AWS APIs
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(aws);

#define AWS_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define AWS_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define AWS_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define AWS_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <mbedtls/ssl.h>
#include <net/socket.h>
#include <net/mqtt.h>
#include <stdio.h>
#include <kernel.h>

#include "dns.h"
#include "print_json.h"
#include "aws.h"
#include "lairdconnect_battery.h"
#include "ble_motion_service.h"
#include "sdcard_log.h"

#if CONFIG_BLUEGRASS
#include "sensor_gateway_parser.h"
#endif

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifndef CONFIG_MQTT_LIB_TLS
#error "AWS must use MQTT with TLS"
#endif

#define APP_CA_CERT_TAG CA_TAG
#define APP_DEVICE_CERT_TAG DEVICE_CERT_TAG

#define CONVERSION_MAX_STR_LEN 10

struct topics {
	uint8_t update[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t update_delta[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t get[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t get_accepted[CONFIG_AWS_TOPIC_MAX_SIZE];
};

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
K_THREAD_STACK_DEFINE(rxThreadStack, AWS_RX_THREAD_STACK_SIZE);
static struct k_thread rxThread;

K_SEM_DEFINE(connected_sem, 0, 1);
K_SEM_DEFINE(send_ack_sem, 0, 1);

/* Buffers for MQTT client. */
static uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];
#if CONFIG_BLUEGRASS
static uint8_t subscription_buffer[CONFIG_SHADOW_IN_MAX_SIZE];
#endif

/* mqtt client id */
static char *mqtt_client_id;
static char mqtt_random_id[AWS_MQTT_ID_MAX_SIZE];

/* The mqtt client struct */
static struct mqtt_client client_ctx;

/* MQTT Broker details. */
static struct sockaddr_storage broker;

static struct pollfd fds[1];
static int nfds;

static bool connected = false;

static struct addrinfo *saddr;

static sec_tag_t m_sec_tags[] = { APP_CA_CERT_TAG, APP_DEVICE_CERT_TAG };

static struct shadow_reported_struct shadow_persistent_data;

static char *server_endpoint;
static char *root_ca;

static struct topics topics;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int tls_init(const uint8_t *cert, const uint8_t *key);
static void prepare_fds(struct mqtt_client *client);
static void clear_fds(void);
static void wait(int timeout);
static void mqtt_evt_handler(struct mqtt_client *const client,
			     const struct mqtt_evt *evt);
static int subscription_handler(struct mqtt_client *const client,
				const struct mqtt_evt *evt);
static void subscription_flush(struct mqtt_client *const client, size_t length);
static int publish_string(struct mqtt_client *client, enum mqtt_qos qos,
			  char *data, uint8_t *topic);
static void client_init(struct mqtt_client *client);
static int try_to_connect(struct mqtt_client *client);
static void awsRxThread(void *arg1, void *arg2, void *arg3);
static uint16_t rand16_nonzero_get(void);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
char *awsGetGatewayUpdateDeltaTopic()
{
	return (topics.update);
}

int awsInit(void)
{
	/* init shadow data */
	shadow_persistent_data.state.reported.firmware_version = "";
	shadow_persistent_data.state.reported.os_version = "";
	shadow_persistent_data.state.reported.radio_version = "";
	shadow_persistent_data.state.reported.IMEI = "";
	shadow_persistent_data.state.reported.ICCID = "";
#ifdef CONFIG_SCAN_FOR_BT510_CODED
	shadow_persistent_data.state.reported.codedPhySupported = true;
#else
	shadow_persistent_data.state.reported.codedPhySupported = false;
#endif
	server_endpoint = AWS_DEFAULT_ENDPOINT;
	mqtt_client_id = DEFAULT_MQTT_CLIENTID;

	k_thread_name_set(k_thread_create(&rxThread, rxThreadStack,
					  K_THREAD_STACK_SIZEOF(rxThreadStack),
					  awsRxThread, NULL, NULL, NULL,
					  AWS_RX_THREAD_PRIORITY, 0, K_NO_WAIT),
			  "aws");

	return 0;
}

int awsSetCredentials(const uint8_t *cert, const uint8_t *key)
{
	return tls_init(cert, key);
}

void awsSetEndpoint(const char *ep)
{
	server_endpoint = (char *)ep;
}

void awsSetClientId(const char *id)
{
	mqtt_client_id = (char *)id;
}

void awsSetRootCa(const char *cred)
{
	root_ca = (char *)cred;
}

int awsGetServerAddr(void)
{
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	return dns_resolve_server_addr(server_endpoint, SERVER_PORT_STR, &hints,
				       &saddr);
}

int awsConnect()
{
	/* add randomness to client id */
	strncpy(mqtt_random_id, mqtt_client_id, AWS_MQTT_ID_MAX_SIZE);
	size_t id_len = strlen(mqtt_random_id);
	snprintf(mqtt_random_id + id_len, AWS_MQTT_ID_MAX_SIZE - id_len,
		 "_%08x", sys_rand32_get());

	AWS_LOG_INF("Attempting to connect %s to AWS...",
		    log_strdup(mqtt_random_id));
	int rc = try_to_connect(&client_ctx);
	if (rc != 0) {
		AWS_LOG_ERR("AWS connect err (%d)", rc);
	}
	return rc;
}

void awsDisconnect(void)
{
	mqtt_disconnect(&client_ctx);
}

int awsKeepAlive(void)
{
	int rc;

	rc = mqtt_live(&client_ctx);
	if (rc != 0) {
		AWS_LOG_ERR("mqtt_live (%d)", rc);
		goto done;
	}

done:
	return rc;
}

int awsSetShadowKernelVersion(const char *version)
{
	shadow_persistent_data.state.reported.os_version = version;

	return 0;
}

int awsSetShadowIMEI(const char *imei)
{
	shadow_persistent_data.state.reported.IMEI = imei;

	return 0;
}

int awsSetShadowRadioFirmwareVersion(const char *version)
{
	shadow_persistent_data.state.reported.radio_version = version;

	return 0;
}

int awsSetShadowAppFirmwareVersion(const char *version)
{
	shadow_persistent_data.state.reported.firmware_version = version;

	return 0;
}

int awsSetShadowICCID(const char *iccid)
{
	shadow_persistent_data.state.reported.ICCID = iccid;

	return 0;
}

int awsSetShadowRadioSerialNumber(const char *sn)
{
	shadow_persistent_data.state.reported.radio_sn = sn;

	return 0;
}

static int sendData(char *data, uint8_t *topic)
{
	int rc;

	/* Wait until acknowledgement so that we don't close AWS
	 * connection too soon after sending data.
	 */
	k_sem_reset(&send_ack_sem);

	rc = publish_string(&client_ctx, MQTT_QOS_1_AT_LEAST_ONCE, data, topic);
	if (rc != 0) {
		AWS_LOG_ERR("MQTT publish err (%d)", rc);
		goto done;
	}

	if (k_sem_take(&send_ack_sem, PUBLISH_TIMEOUT_TICKS) != 0) {
		AWS_LOG_ERR("Publish timeout");
		return -EAGAIN;
	}
done:
	return rc;
}

int awsSendData(char *data, uint8_t *topic)
{
	/* If the topic is NULL, then publish to the gateway (Pinnacle-100) topic.
	 * Otherwise, publish to a sensor topic. */
	if (topic == NULL) {
		return sendData(data, topics.update);
	} else {
		return sendData(data, topic);
	}
}

void awsGenerateGatewayTopics(const char *imei)
{
	snprintk(topics.update, sizeof(topics.update),
		 "$aws/things/deviceId-%s/shadow/update", imei);

	snprintk(topics.update_delta, sizeof(topics.update_delta),
		 "$aws/things/deviceId-%s/shadow/update/delta", imei);

	snprintk(topics.get_accepted, sizeof(topics.get_accepted),
		 "$aws/things/deviceId-%s/shadow/get/accepted", imei);

	snprintk(topics.get, sizeof(topics.get),
		 "$aws/things/deviceId-%s/shadow/get", imei);
}

/* On power-up, get the shadow by sending a message to the /get topic */
int awsGetAcceptedSubscribe(void)
{
	int rc = awsSubscribe(topics.get_accepted, true);
	if (rc != 0) {
		AWS_LOG_ERR("Unable to subscribe to /../get/accepted");
	}
	return rc;
}

int awsGetShadow(void)
{
	char msg[] = "{\"message\":\"Hello, from Laird Connectivity\"}";
	int rc = sendData(msg, topics.get);
	if (rc != 0) {
		AWS_LOG_ERR("Unable to get shadow");
	}
	return rc;
}

int awsGetAcceptedUnsub(void)
{
	return awsSubscribe(topics.get_accepted, false);
}

int awsPublishShadowPersistentData()
{
	int rc = -ENOMEM;
	ssize_t buf_len;

	buf_len = json_calc_encoded_len(shadow_descr, ARRAY_SIZE(shadow_descr),
					&shadow_persistent_data);
	if (buf_len < 0) {
		return buf_len;
	}

	/* account for null char */
	buf_len += 1;

	char *msg = k_calloc(buf_len, sizeof(char));
	if (msg == NULL) {
		AWS_LOG_ERR("k_calloc failed");
		return rc;
	}

	rc = json_obj_encode_buf(shadow_descr, ARRAY_SIZE(shadow_descr),
				 &shadow_persistent_data, msg, buf_len);
	if (rc < 0) {
		AWS_LOG_ERR("JSON encode failed");
		goto done;
	}

#if CLEAR_SHADOW_ON_STARTUP
	/* Clear the shadow and start fresh */
	rc = awsSendData(SHADOW_STATE_NULL, GATEWAY_TOPIC);
	if (rc < 0) {
		AWS_LOG_ERR("Clear shadow failed");
		goto done;
	}
#endif

	rc = awsSendData(msg, GATEWAY_TOPIC);
	if (rc < 0) {
		AWS_LOG_ERR("Update persistent shadow data failed");
		goto done;
	}

done:
	k_free(msg);
	return rc;
}

/* BL654 Sensor with BME280 */
int awsPublishBl654SensorData(float temperature, float humidity, float pressure)
{
	char msg[strlen(SHADOW_REPORTED_START) + strlen(SHADOW_TEMPERATURE) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_HUMIDITY) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_PRESSURE) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_REPORTED_END)];

	snprintf(msg, sizeof(msg), "%s%s%.2f,%s%.2f,%s%.1f%s",
		 SHADOW_REPORTED_START, SHADOW_TEMPERATURE, temperature,
		 SHADOW_HUMIDITY, humidity, SHADOW_PRESSURE, pressure,
		 SHADOW_REPORTED_END);

	return awsSendData(msg, GATEWAY_TOPIC);
}

int awsPublishPinnacleData(int radioRssi, int radioSinr,
			   struct battery_data *battery,
			   struct motion_status *motion,
			   struct sdcard_status *sdcard)
{
	char msg[strlen(SHADOW_REPORTED_START) + strlen(SHADOW_RADIO_RSSI) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_TEMP) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_BATT_LEVEL) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_BATT_VOLT) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_PWR_STATE) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_BATT_0) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_BATT_1) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_BATT_2) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_BATT_3) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_BATT_4) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_BATT_GOOD) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_BATT_BAD) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_BATT_LOW) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_ODR) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_SCALE) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_ACT_THS) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_MOVEMENT) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_MAX_LOG_SIZE) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_CURR_LOG_SIZE) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_MG100_SDCARD_FREE) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_REPORTED_END)];

	snprintf(
		msg, sizeof(msg),
		"%s%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d,%s%d%s",
		SHADOW_REPORTED_START, SHADOW_MG100_BATT_LEVEL,
		battery->batteryCapacity, SHADOW_MG100_BATT_VOLT,
		battery->batteryVoltage, SHADOW_MG100_PWR_STATE,
		battery->batteryChgState, SHADOW_MG100_BATT_0,
		battery->batteryThreshold0, SHADOW_MG100_BATT_1,
		battery->batteryThreshold1, SHADOW_MG100_BATT_2,
		battery->batteryThreshold2, SHADOW_MG100_BATT_3,
		battery->batteryThreshold3, SHADOW_MG100_BATT_4,
		battery->batteryThreshold4, SHADOW_MG100_BATT_GOOD,
		battery->batteryThresholdGood, SHADOW_MG100_BATT_BAD,
		battery->batteryThresholdBad, SHADOW_MG100_BATT_LOW,
		battery->batteryThresholdLow, SHADOW_MG100_TEMP,
		battery->ambientTemperature, SHADOW_MG100_ODR, motion->odr,
		SHADOW_MG100_SCALE, motion->scale, SHADOW_MG100_ACT_THS,
		motion->thr, SHADOW_MG100_MOVEMENT, motion->motion,
		SHADOW_MG100_MAX_LOG_SIZE, sdcard->maxLogSize,
		SHADOW_MG100_CURR_LOG_SIZE, sdcard->currLogSize,
		SHADOW_MG100_SDCARD_FREE, sdcard->freeSpace, SHADOW_RADIO_RSSI,
		radioRssi, SHADOW_RADIO_SINR, radioSinr, SHADOW_REPORTED_END);

	return awsSendData(msg, GATEWAY_TOPIC);
}

bool awsConnected(void)
{
	return connected;
}

int awsSubscribe(uint8_t *topic, uint8_t subscribe)
{
	struct mqtt_topic mt;
	if (topic == NULL) {
		mt.topic.utf8 = topics.update_delta;
	} else {
		mt.topic.utf8 = topic;
	}
	mt.topic.size = strlen(mt.topic.utf8);
	mt.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	__ASSERT(mt.topic.size != 0, "Invalid topic");
	struct mqtt_subscription_list list = {
		.list = &mt, .list_count = 1, .message_id = rand16_nonzero_get()
	};
	int rc = subscribe ? mqtt_subscribe(&client_ctx, &list) :
			     mqtt_unsubscribe(&client_ctx, &list);
	char *s = log_strdup(subscribe ? "Subscribed" : "Unsubscribed");
	char *t = log_strdup(mt.topic.utf8);
	if (rc != 0) {
		AWS_LOG_ERR("%s status %d to %s", s, rc, t);
	} else {
		AWS_LOG_DBG("%s to %s", s, t);
	}
	return rc;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static int tls_init(const uint8_t *cert, const uint8_t *key)
{
	int err = -EINVAL;

	tls_credential_delete(APP_CA_CERT_TAG, TLS_CREDENTIAL_CA_CERTIFICATE);
	err = tls_credential_add(APP_CA_CERT_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
				 root_ca, strlen(root_ca) + 1);
	if (err < 0) {
		AWS_LOG_ERR("Failed to register public certificate: %d", err);
		return err;
	}

	tls_credential_delete(APP_DEVICE_CERT_TAG,
			      TLS_CREDENTIAL_SERVER_CERTIFICATE);
	err = tls_credential_add(APP_DEVICE_CERT_TAG,
				 TLS_CREDENTIAL_SERVER_CERTIFICATE, cert,
				 strlen(cert) + 1);
	if (err < 0) {
		AWS_LOG_ERR("Failed to register device certificate: %d", err);
		return err;
	}

	tls_credential_delete(APP_DEVICE_CERT_TAG, TLS_CREDENTIAL_PRIVATE_KEY);
	err = tls_credential_add(APP_DEVICE_CERT_TAG,
				 TLS_CREDENTIAL_PRIVATE_KEY, key,
				 strlen(key) + 1);
	if (err < 0) {
		AWS_LOG_ERR("Failed to register device key: %d", err);
		return err;
	}

	return err;
}

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
			AWS_LOG_ERR("poll error: %d", errno);
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
			AWS_LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}

		connected = true;
		k_sem_give(&connected_sem);
		AWS_LOG_INF("MQTT client connected!");

		break;

	case MQTT_EVT_DISCONNECT:
		AWS_LOG_INF("MQTT client disconnected %d", evt->result);

		connected = false;
		clear_fds();
		awsDisconnectCallback();
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			AWS_LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		AWS_LOG_INF("PUBACK packet id: %u",
			    evt->param.puback.message_id);

		k_sem_give(&send_ack_sem);
		break;

	case MQTT_EVT_PUBLISH:
		if (evt->result != 0) {
			AWS_LOG_ERR("MQTT PUBLISH error %d", evt->result);
			break;
		}

		AWS_LOG_INF(
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

/* The timestamps can make the shadow size larger than 4K.
 * This is too large to be allocated by malloc or the buffer pool.
 * Therefore, messages are processed here.
 */
static int subscription_handler(struct mqtt_client *const client,
				const struct mqtt_evt *evt)
{
	int rc = 0;
#if CONFIG_BLUEGRASS
	uint16_t id = evt->param.publish.message_id;
	uint32_t length = evt->param.publish.message.payload.len;
	uint8_t qos = evt->param.publish.message.topic.qos;
	const uint8_t *topic = evt->param.publish.message.topic.topic.utf8;

	/* Leave room for null to allow easy printing */
	size_t size = length + 1;
	if (size > CONFIG_SHADOW_IN_MAX_SIZE) {
		return 0;
	}

	memset(subscription_buffer, 0, CONFIG_SHADOW_IN_MAX_SIZE);
	rc = mqtt_read_publish_payload(client, subscription_buffer, length);
	if (rc == length) {
#if CONFIG_JSON_LOG_MQTT_RX_DATA
		print_json("MQTT Read data", rc, subscription_buffer);
#endif

		SensorGatewayParser(topic, subscription_buffer);

		if (qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			struct mqtt_puback_param param = { .message_id = id };
			(void)mqtt_publish_qos1_ack(client, &param);
		} else if (qos == MQTT_QOS_2_EXACTLY_ONCE) {
			AWS_LOG_ERR("QOS 2 not supported");
		}
	}
#endif
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

static int publish_string(struct mqtt_client *client, enum mqtt_qos qos,
			  char *data, uint8_t *topic)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = topic;
	param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
	param.message.payload.data = data;
	param.message.payload.len = strlen(param.message.payload.data);
	param.message_id = rand16_nonzero_get();
	param.dup_flag = 0U;
	param.retain_flag = 0U;

#if CONFIG_JSON_LOG_TOPIC
	print_json("Publish Topic", param.message.topic.topic.size,
		   param.message.topic.topic.utf8);
#endif

#if CONFIG_JSON_LOG_PUBLISH
	print_json("Publish string", param.message.payload.len,
		   param.message.payload.data);
#endif

	return mqtt_publish(client, &param);
}

static void broker_init(void)
{
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

	broker4->sin_family = saddr->ai_family;
	broker4->sin_port = htons(strtol(SERVER_PORT_STR, NULL, 0));
	net_ipaddr_copy(&broker4->sin_addr, &net_sin(saddr->ai_addr)->sin_addr);
}

static void client_init(struct mqtt_client *client)
{
	mqtt_client_init(client);

	broker_init();

	/* MQTT client configuration */
	client->broker = &broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = mqtt_random_id;
	client->client_id.size = strlen(mqtt_random_id);
	client->password = NULL;
	client->user_name = NULL;

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
	client->transport.type = MQTT_TRANSPORT_SECURE;
	struct mqtt_sec_config *tls_config = &client->transport.tls.config;
	tls_config->peer_verify = MBEDTLS_SSL_VERIFY_REQUIRED;
	tls_config->cipher_list = NULL;
	tls_config->sec_tag_list = m_sec_tags;
	tls_config->sec_tag_count = ARRAY_SIZE(m_sec_tags);
	tls_config->hostname = server_endpoint;
}

/* In this routine we block until the connected variable is 1 */
static int try_to_connect(struct mqtt_client *client)
{
	int rc, i = 0;

	while (i++ < APP_CONNECT_TRIES && !connected) {
		client_init(client);

		rc = mqtt_connect(client);
		if (rc != 0) {
			AWS_LOG_ERR("mqtt_connect (%d)", rc);
			k_sleep(K_MSEC(APP_SLEEP_MSECS));
			continue;
		}

		prepare_fds(client);

		wait(APP_SLEEP_MSECS);
		mqtt_input(client);

		if (!connected) {
			mqtt_abort(client);
		}
	}

	if (connected) {
		return 0;
	}

	return -EINVAL;
}

static void awsRxThread(void *arg1, void *arg2, void *arg3)
{
	while (true) {
		if (connected) {
			/* Wait for socket RX data */
			wait(SYS_FOREVER_MS);
			/* process MQTT RX data */
			mqtt_input(&client_ctx);
		} else {
			AWS_LOG_DBG("Waiting for MQTT connection...");
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

/******************************************************************************/
/* Override in application                                                    */
/******************************************************************************/
__weak void awsDisconnectCallback(void)
{
	return;
}
