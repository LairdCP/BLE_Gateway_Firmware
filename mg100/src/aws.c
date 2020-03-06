/* aws.c - AWS APIs
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(mg100_aws);

#include <mbedtls/ssl.h>
#include <net/socket.h>
#include <net/mqtt.h>
#include <stdio.h>

#include "mg100_common.h"
#include "aws.h"

#ifndef CONFIG_MQTT_LIB_TLS
#error "AWS must use MQTT with TLS"
#endif

#define APP_CA_CERT_TAG CA_TAG
#define APP_DEVICE_CERT_TAG DEVICE_CERT_TAG

#define AWS_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define AWS_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define AWS_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define AWS_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

K_THREAD_STACK_DEFINE(rxThreadStack, AWS_RX_THREAD_STACK_SIZE);
static struct k_thread rxThread;

K_SEM_DEFINE(connected_sem, 0, 1);

/* Buffers for MQTT client. */
static u8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static u8_t tx_buffer[APP_MQTT_BUFFER_SIZE];

/* mqtt client id */
static char *mqtt_client_id;

/* The mqtt client struct */
static struct mqtt_client client_ctx;

/* MQTT Broker details. */
static struct sockaddr_storage broker;

static struct pollfd fds[1];
static int nfds;

static bool connected = false;

static struct addrinfo server_addr;
struct addrinfo *saddr;

static sec_tag_t m_sec_tags[] = { APP_CA_CERT_TAG, APP_DEVICE_CERT_TAG };

static struct shadow_reported_struct shadow_persistent_data;

static char *server_endpoint;
static char *root_ca;

static int tls_init(const u8_t *cert, const u8_t *key)
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

void mqtt_evt_handler(struct mqtt_client *const client,
		      const struct mqtt_evt *evt)
{
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

		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			AWS_LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		AWS_LOG_INF("PUBACK packet id: %u",
			    evt->param.puback.message_id);

		break;

	case MQTT_EVT_PUBLISH:
		if (evt->result != 0) {
			AWS_LOG_ERR("MQTT PUBLISH error %d", evt->result);
			break;
		}

		AWS_LOG_INF("MQTT RXd ID: %d Topic: %s len: %d",
			    evt->param.publish.message_id,
			    evt->param.publish.message.topic.topic.utf8,
			    evt->param.publish.message.payload.len);

		break;
	default:
		break;
	}
}

static char *get_mqtt_topic(void)
{
	static char topic[sizeof(
		"$aws/things/deviceId-###############/shadow/update")];

	snprintk(topic, sizeof(topic), "$aws/things/deviceId-%s/shadow/update",
		 shadow_persistent_data.state.reported.IMEI);

	return topic;
}

static int publish_string(struct mqtt_client *client, enum mqtt_qos qos,
			  char *data)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = (u8_t *)get_mqtt_topic();
	param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
	param.message.payload.data = data;
	param.message.payload.len = strlen(param.message.payload.data);
	param.message_id = sys_rand32_get();
	param.dup_flag = 0U;
	param.retain_flag = 0U;

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
	client->client_id.utf8 = mqtt_client_id;
	client->client_id.size = strlen(mqtt_client_id);
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
			k_sleep(APP_SLEEP_MSECS);
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
			AWS_LOG_DBG("Waiting for MQTT RX data...");
			/* Wait for socket RX data */
			wait(K_FOREVER);

			AWS_LOG_DBG("MQTT data RXd");

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

int awsInit(void)
{
	/* init shadow data */
	shadow_persistent_data.state.reported.firmware_version = "";
	shadow_persistent_data.state.reported.os_version = "";
	shadow_persistent_data.state.reported.radio_version = "";
	shadow_persistent_data.state.reported.IMEI = "";
	shadow_persistent_data.state.reported.ICCID = "";
	server_endpoint = AWS_DEFAULT_ENDPOINT;
	mqtt_client_id = DEFAULT_MQTT_CLIENTID;

	k_thread_create(&rxThread, rxThreadStack,
			K_THREAD_STACK_SIZEOF(rxThreadStack), awsRxThread, NULL,
			NULL, NULL, AWS_RX_THREAD_PRIORITY, 0, K_NO_WAIT);

	return 0;
}

int awsSetCredentials(const u8_t *cert, const u8_t *key)
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
	int rc, dns_retries;

	server_addr.ai_family = AF_INET;
	server_addr.ai_socktype = SOCK_STREAM;
	dns_retries = DNS_RETRIES;
	do {
		AWS_LOG_DBG("Get AWS server address");
		rc = getaddrinfo(server_endpoint, SERVER_PORT_STR, &server_addr,
				 &saddr);
		if (rc != 0) {
			AWS_LOG_ERR("Get AWS server addr (%d)", rc);
			k_sleep(K_SECONDS(5));
		}
		dns_retries--;
	} while (rc != 0 && dns_retries != 0);
	if (rc != 0) {
		AWS_LOG_ERR("Unable to resolve '%s'", server_endpoint);
	}

	return rc;
}

int awsConnect()
{
	AWS_LOG_INF("Attempting to connect %s to AWS...", mqtt_client_id);
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

static int sendData(char *data)
{
	int rc;

	rc = publish_string(&client_ctx, MQTT_QOS_0_AT_MOST_ONCE, data);
	if (rc != 0) {
		AWS_LOG_ERR("MQTT publish err (%d)", rc);
		goto done;
	}

done:
	return rc;
}

int awsSendData(char *data)
{
	return sendData(data);
}

int awsPublishShadowPersistentData()
{
	int rc;
	size_t buf_len;

	buf_len = json_calc_encoded_len(shadow_descr, ARRAY_SIZE(shadow_descr),
					&shadow_persistent_data);
	char msg[buf_len];

	rc = json_obj_encode_buf(shadow_descr, ARRAY_SIZE(shadow_descr),
				 &shadow_persistent_data, msg, sizeof(msg));
	if (rc < 0) {
		goto done;
	}

	/* Clear the shadow and start fresh */
	rc = sendData(SHADOW_STATE_NULL);
	if (rc < 0) {
		goto done;
	}

	rc = sendData(msg);
	if (rc < 0) {
		goto done;
	}

done:
	return rc;
}

int awsPublishSensorData(float temperature, float humidity, int pressure,
			 int radioRssi, int radioSinr)
{
	char msg[strlen(SHADOW_REPORTED_START) + strlen(SHADOW_TEMPERATURE) +
		 10 + strlen(SHADOW_HUMIDITY) + 10 + strlen(SHADOW_PRESSURE) +
		 10 + strlen(SHADOW_RADIO_RSSI) + 10 +
		 strlen(SHADOW_RADIO_SINR) + 10 + strlen(SHADOW_REPORTED_END)];

	snprintf(msg, sizeof(msg), "%s%s%.2f,%s%.2f,%s%d,%s%d,%s%d%s",
		 SHADOW_REPORTED_START, SHADOW_TEMPERATURE, temperature,
		 SHADOW_HUMIDITY, humidity, SHADOW_PRESSURE, pressure,
		 SHADOW_RADIO_RSSI, radioRssi, SHADOW_RADIO_SINR, radioSinr,
		 SHADOW_REPORTED_END);
	return awsSendData(msg);
}

bool awsConnected(void)
{
	return connected;
}