/**
 * @file aws.c
 * @brief AWS APIs
 *
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(aws, CONFIG_AWS_LOG_LEVEL);

#define AWS_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define AWS_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define AWS_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define AWS_LOG_DBG(...) LOG_DBG(__VA_ARGS__)
#define AWS_LOG_ACK(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <mbedtls/ssl.h>
#include <net/socket.h>
#include <stdio.h>
#include <kernel.h>
#include <random/rand32.h>
#include <bluetooth/bluetooth.h>
#include <version.h>

#include "app_version.h"
#include "lcz_dns.h"
#include "print_json.h"
#include "lcz_qrtc.h"
#include "lcz_software_reset.h"
#include "lte.h"
#include "attr.h"
#include "fota_smp.h"

#ifdef CONFIG_BLUEGRASS
#include "sensor_gateway_parser.h"
#endif

#if defined(CONFIG_BOARD_MG100)
#include "lairdconnect_battery.h"
#endif

#if defined(CONFIG_LCZ_MOTION)
#include "lcz_motion.h"
#endif

#if defined(CONFIG_SD_CARD_LOG)
#include "sdcard_log.h"
#endif

#include "aws_json.h"
#include "aws.h"

#if defined(CONFIG_NET_L2_ETHERNET)
#include <net/dns_resolve.h>
#include <net/ethernet.h>
#include "net_private.h"
#include "ethernet_network.h"
#endif

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifndef CONFIG_MQTT_LIB_TLS
#error "AWS must use MQTT with TLS"
#endif

#define CONVERSION_MAX_STR_LEN 10
#define HEX_CHARS_PER_HEX_VALUE 2

struct topics {
	uint8_t update[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t update_delta[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t get[CONFIG_AWS_TOPIC_MAX_SIZE];
	uint8_t get_accepted[CONFIG_AWS_TOPIC_MAX_SIZE];
};

#if CONFIG_AWS_PUBLISH_WATCHDOG_SECONDS != 0
BUILD_ASSERT((CONFIG_AWS_PUBLISH_WATCHDOG_SECONDS / 2) >
		     CONFIG_AWS_HEARTBEAT_SECONDS,
	     "Incompatible publish watchdog and heartbeat configuration");
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
K_THREAD_STACK_DEFINE(rx_thread_stack, AWS_RX_THREAD_STACK_SIZE);
static struct k_thread rxThread;

static struct k_sem connected_sem;
static struct k_sem disconnected_sem;

/* Buffers for MQTT client. */
static uint8_t rx_buffer[CONFIG_MQTT_RX_BUFFER_SIZE];
static uint8_t tx_buffer[CONFIG_MQTT_TX_BUFFER_SIZE];

#ifdef CONFIG_BLUEGRASS
static uint8_t subscription_buffer[CONFIG_SHADOW_IN_MAX_SIZE];
#endif

/* mqtt client id */
static char mqtt_random_id[AWS_MQTT_ID_MAX_SIZE];

/* The mqtt client struct */
static struct mqtt_client client_ctx;

/* MQTT Broker details. */
static struct sockaddr_storage broker;

static struct pollfd fds[1];
static int nfds;

static bool aws_connected;
static bool aws_disconnect;

static struct addrinfo *saddr;

static sec_tag_t m_sec_tags[] = {
	CONFIG_APP_CA_CERT_TAG,
	CONFIG_APP_DEVICE_CERT_TAG,
};

static struct shadow_reported_struct shadow_persistent_data;

static struct topics topics;

static struct k_work_delayable publish_watchdog;
static struct k_work_delayable keep_alive;

static struct {
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
} aws_stats;

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
static int aws_send_data(bool binary, char *data, uint32_t len, uint8_t *topic);

#ifdef CONFIG_NET_L2_ETHERNET
static char *net_sprint_ll_addr_lower(const uint8_t *ll);
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
char *awsGetGatewayUpdateDeltaTopic()
{
	return (topics.update);
}

int awsInit(void)
{
	struct shadow_persistent_values *reported =
		&shadow_persistent_data.state.reported;

	k_sem_init(&connected_sem, 0, 1);
	k_sem_init(&disconnected_sem, 0, 1);

	/* init shadow data */
	reported->os_version = KERNEL_VERSION_STRING;
	reported->firmware_version = APP_VERSION_STRING;
#ifdef CONFIG_MODEM_HL7800
	reported->IMEI = attr_get_quasi_static(ATTR_ID_gatewayId);
	reported->ICCID = attr_get_quasi_static(ATTR_ID_iccid);
	reported->radio_version = attr_get_quasi_static(ATTR_ID_lteVersion);
	reported->radio_sn = attr_get_quasi_static(ATTR_ID_lteSerialNumber);
#endif
#ifdef CONFIG_SCAN_FOR_BT510_CODED
	reported->codedPhySupported = true;
#else
	reported->codedPhySupported = false;
#endif
#ifdef CONFIG_HTTP_FOTA
	reported->httpFotaEnabled = true;
#else
	reported->httpFotaEnabled = false;
#endif

	k_thread_name_set(
		k_thread_create(&rxThread, rx_thread_stack,
				K_THREAD_STACK_SIZEOF(rx_thread_stack),
				aws_rx_thread, NULL, NULL, NULL,
				AWS_RX_THREAD_PRIORITY, 0, K_NO_WAIT),
		"aws");

	k_work_init_delayable(&publish_watchdog, publish_watchdog_work_handler);
	k_work_init_delayable(&keep_alive, keep_alive_work_handler);

	return 0;
}

int awsGetServerAddr(void)
{
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	return dns_resolve_server_addr(attr_get_quasi_static(ATTR_ID_endpoint),
				       attr_get_quasi_static(ATTR_ID_port),
				       &hints, &saddr);
}

int awsConnect()
{
	int rc = 0;
	size_t id_len;

	if (!aws_connected) {
		aws_disconnect = false;
		/* Add randomness to client id to make each connection unique.*/
		strncpy(mqtt_random_id, attr_get_quasi_static(ATTR_ID_clientId),
			AWS_MQTT_ID_MAX_SIZE);
		id_len = strlen(mqtt_random_id);
		snprintf(mqtt_random_id + id_len, AWS_MQTT_ID_MAX_SIZE - id_len,
			 "_%08x", sys_rand32_get());

		AWS_LOG_INF("Attempting to connect %s to AWS...",
			    log_strdup(mqtt_random_id));
		rc = try_to_connect(&client_ctx);
		if (rc != 0) {
			AWS_LOG_ERR("AWS connect err (%d)", rc);
			aws_stats.consecutive_connection_failures += 1;
			if ((aws_stats.consecutive_connection_failures >
			     CONFIG_AWS_MAX_CONSECUTIVE_CONNECTION_FAILURES) &&
			    (CONFIG_AWS_MAX_CONSECUTIVE_CONNECTION_FAILURES !=
			     0)) {
				LOG_WRN("Maximum consecutive connection failures exceeded");
				lcz_software_reset_after_assert(0);
			}
		} else {
			aws_stats.consecutive_connection_failures = 0;
		}
	}
	return rc;
}

int awsDisconnect(void)
{
	if (aws_connected) {
		aws_disconnect = true;
		AWS_LOG_DBG("Waiting to close MQTT connection");
		k_sem_take(&disconnected_sem, K_FOREVER);
		AWS_LOG_DBG("MQTT connection closed");
	}

	return 0;
}

int awsSendData(char *data, uint8_t *topic)
{
	/* If the topic is NULL, then publish to the gateway (Pinnacle-100) topic.
	 * Otherwise, publish to a sensor topic. */
	if (topic == NULL) {
		return aws_send_data(false, data, 0, topics.update);
	} else {
		return aws_send_data(false, data, 0, topic);
	}
}

int awsSendBinData(char *data, uint32_t len, uint8_t *topic)
{
	if (topic == NULL) {
		/* don't publish binary data to the default topic (device shadow) */
		return -EOPNOTSUPP;
	} else {
		return aws_send_data(true, data, len, topic);
	}
}

void awsGenerateGatewayTopics(const char *id)
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
	int rc = aws_send_data(false, msg, 0, topics.get);
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

#ifdef CONFIG_NET_L2_ETHERNET
	struct shadow_persistent_values *reported =
		&shadow_persistent_data.state.reported;

	reported->ethernet.MAC = net_sprint_ll_addr_lower(
		attr_get_quasi_static(ATTR_ID_ethernetMAC));
	reported->ethernet.type = (uint32_t)ETHERNET_TYPE_IPV4;
	reported->ethernet.mode = attr_get_uint32(
		ATTR_ID_ethernetMode, (uint32_t)ETHERNET_MODE_STATIC);
	reported->ethernet.speed = attr_get_uint32(
		ATTR_ID_ethernetSpeed, (uint32_t)ETHERNET_SPEED_UNKNOWN);
	reported->ethernet.duplex = attr_get_uint32(
		ATTR_ID_ethernetDuplex, (uint32_t)ETHERNET_DUPLEX_UNKNOWN);
	reported->ethernet.IPAddress =
		attr_get_quasi_static(ATTR_ID_ethernetIPAddress);
	reported->ethernet.netmaskLength =
		attr_get_uint32(ATTR_ID_ethernetNetmaskLength, 0);
	reported->ethernet.gateway =
		attr_get_quasi_static(ATTR_ID_ethernetGateway);
	reported->ethernet.DNS = attr_get_quasi_static(ATTR_ID_ethernetDNS);

#if defined(CONFIG_NET_DHCPV4)
	reported->ethernet.DHCPLeaseTime =
		attr_get_uint32(ATTR_ID_ethernetDHCPLeaseTime, 0);
	reported->ethernet.DHCPRenewTime =
		attr_get_uint32(ATTR_ID_ethernetDHCPRenewTime, 0);
	reported->ethernet.DHCPState =
		attr_get_uint32(ATTR_ID_ethernetDHCPState, 0);
	reported->ethernet.DHCPAttempts =
		attr_get_uint32(ATTR_ID_ethernetDHCPAttempts, 0);
#endif
#endif

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

#ifdef CONFIG_AWS_CLEAR_SHADOW_ON_STARTUP
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
	} else {
		AWS_LOG_INF("Sent persistent shadow data");
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

#if defined(CONFIG_BOARD_MG100)
int awsPublishHeartbeat(void)
{
	char msg[strlen(SHADOW_REPORTED_START) + strlen(SHADOW_RADIO_RSSI) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_RADIO_SINR) +
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

	struct battery_data *battery = batteryGetStatus();
	struct motion_status *motion = lcz_motion_get_status();

	int log_size = -1;
	int max_log_size = -1;
	int free_space = -1;

#ifdef CONFIG_SD_CARD_LOG
	log_size = sdCardLogGetSize();
	max_log_size = sdCardLogGetMaxSize();
	free_space = sdCardLogGetFree();
#endif

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
		motion->thr, SHADOW_MG100_MOVEMENT, motion->alarm,
		SHADOW_MG100_MAX_LOG_SIZE, max_log_size,
		SHADOW_MG100_CURR_LOG_SIZE, log_size, SHADOW_MG100_SDCARD_FREE,
		free_space, SHADOW_RADIO_RSSI,
		attr_get_signed32(ATTR_ID_lteRsrp, 0), SHADOW_RADIO_SINR,
		attr_get_signed32(ATTR_ID_lteSinr, 0), SHADOW_REPORTED_END);

	return awsSendData(msg, GATEWAY_TOPIC);
}
#elif defined(CONFIG_BOARD_PINNACLE_100_DVK)
int awsPublishHeartbeat(void)
{
	char msg[strlen(SHADOW_REPORTED_START) + strlen(SHADOW_RADIO_RSSI) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_RADIO_SINR) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_REPORTED_END)];

	snprintf(msg, sizeof(msg), "%s%s%d,%s%d%s", SHADOW_REPORTED_START,
		 SHADOW_RADIO_RSSI, attr_get_signed32(ATTR_ID_lteRsrp, 0),
		 SHADOW_RADIO_SINR, attr_get_signed32(ATTR_ID_lteSinr, 0),
		 SHADOW_REPORTED_END);

	return awsSendData(msg, GATEWAY_TOPIC);
}
#else
int awsPublishHeartbeat(void)
{
	return 0;
}
#endif

bool awsConnected(void)
{
	return aws_connected;
}

bool awsPublished(void)
{
	return (aws_stats.success > 0);
}

int awsSubscribe(uint8_t *topic, uint8_t subscribe)
{
	struct mqtt_topic mt;
	int rc = -EPERM;
	const char *const str = subscribe ? "Subscribed" : "Unsubscribed";
	struct mqtt_subscription_list list = {
		.list = &mt, .list_count = 1, .message_id = rand16_nonzero_get()
	};

	if (!aws_connected) {
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
	rc = subscribe ? mqtt_subscribe(&client_ctx, &list) :
			       mqtt_unsubscribe(&client_ctx, &list);
	if (rc != 0) {
		AWS_LOG_ERR("%s status %d to %s", str, rc,
			    log_strdup(mt.topic.utf8));
	} else {
		AWS_LOG_DBG("%s to %s", str, log_strdup(mt.topic.utf8));
	}
	return rc;
}

struct mqtt_client *awsGetMqttClient(void)
{
	return &client_ctx;
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

		aws_connected = true;
		k_sem_give(&connected_sem);
		AWS_LOG_INF("MQTT client connected!");
		k_work_schedule(&keep_alive,
				K_SECONDS(CONFIG_MQTT_KEEPALIVE / 2));
		break;

	case MQTT_EVT_DISCONNECT:
		AWS_LOG_INF("MQTT client disconnected %d", evt->result);
		aws_connected = false;
		aws_disconnect = true;
		k_work_cancel_delayable(&keep_alive);
		aws_stats.disconnects += 1;
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			AWS_LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		/* Unless a single caller/semaphore is used the delta isn't foolproof. */
		aws_stats.acks += 1;
		aws_stats.delta = k_uptime_delta(&aws_stats.time);
		aws_stats.delta_max = MAX(aws_stats.delta_max, aws_stats.delta);

		AWS_LOG_ACK("PUBACK packet id: %u delta: %d",
			    evt->param.puback.message_id,
			    (int32_t)aws_stats.delta);

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
#ifdef CONFIG_BLUEGRASS
	uint16_t id = evt->param.publish.message_id;
	uint32_t length = evt->param.publish.message.payload.len;
	uint8_t qos = evt->param.publish.message.topic.qos;
	const uint8_t *topic = evt->param.publish.message.topic.topic.utf8;

	/* Leave room for null to allow easy printing */
	size_t size = length + 1;
	if (size > CONFIG_SHADOW_IN_MAX_SIZE) {
		return 0;
	}

	aws_stats.rx_payload_bytes += length;

	rc = mqtt_read_publish_payload(client, subscription_buffer, length);
	if (rc == length) {
		subscription_buffer[length] = 0; /* null terminate */

#ifdef CONFIG_JSON_LOG_MQTT_RX_DATA
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

#ifdef CONFIG_JSON_LOG_TOPIC
	print_json("Publish Topic", param.message.topic.topic.size,
		   param.message.topic.topic.utf8);
#endif

#ifdef CONFIG_JSON_LOG_PUBLISH
	if (!binary) {
		print_json("Publish string", len, data);
	}
#endif

	/* Does the tx buf size matter? */
	if (client_ctx.tx_buf_size < len) {
		AWS_LOG_WRN("len: %u", len);
	}

	aws_stats.time = k_uptime_get();

	return mqtt_publish(client, &param);
}

static void broker_init(void)
{
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

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
	tls_config->peer_verify =
		attr_get_signed32(ATTR_ID_peerVerify, MBEDTLS_SSL_VERIFY_NONE);
	tls_config->cipher_list = NULL;
	tls_config->sec_tag_list = m_sec_tags;
	tls_config->sec_tag_count = ARRAY_SIZE(m_sec_tags);
	tls_config->hostname = attr_get_quasi_static(ATTR_ID_endpoint);
}

/* In this routine we block until the connected variable is 1 */
static int try_to_connect(struct mqtt_client *client)
{
	int rc, i = 0;

	while (i++ < APP_CONNECT_TRIES && !aws_connected) {
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

		if (!aws_connected) {
			mqtt_abort(client);
		}
	}

	if (aws_connected) {
		return 0;
	}

	return -EINVAL;
}

static void aws_rx_thread(void *arg1, void *arg2, void *arg3)
{
	while (true) {
		if (aws_connected) {
			/* Wait for socket RX data */
			wait(SOCKET_POLL_WAIT_TIME_MSECS);
			/* process MQTT RX data */
			mqtt_input(&client_ctx);
			/* Disconnect (request) flag is set from the disconnect callback
			 * and from a user request.
			 */
			if (aws_disconnect) {
				AWS_LOG_DBG("Closing MQTT connection");
				mqtt_disconnect(&client_ctx);
				clear_fds();
				aws_disconnect = false;
				aws_connected = false;
				k_sem_give(&disconnected_sem);
				awsDisconnectCallback();
			}
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

static void publish_watchdog_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_WRN("Unable to publish (AWS) in the last hour");

	if (!fota_smp_ble_prepared()) {
#ifdef CONFIG_MODEM_HL7800
		if (attr_get_signed32(ATTR_ID_modemFunctionality, 0) !=
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

	if (aws_connected) {
		rc = mqtt_live(&client_ctx);
		if (rc != 0 && rc != -EAGAIN) {
			AWS_LOG_ERR("mqtt_live (%d)", rc);
		}

		k_work_schedule(&keep_alive, K_SECONDS(CONFIG_MQTT_KEEPALIVE));
	}
}

static int aws_send_data(bool binary, char *data, uint32_t len, uint8_t *topic)
{
	int rc = -EPERM;
	uint32_t length;

	if (!aws_connected) {
		return rc;
	}

	if (binary) {
		length = len;
	} else {
		length = strlen(data);
	}

	aws_stats.sends += 1;
	aws_stats.tx_payload_bytes += length;

	rc = publish(&client_ctx, MQTT_QOS_1_AT_LEAST_ONCE, data, length, topic,
		     binary);

	if (rc == 0) {
		aws_stats.success += 1;
		aws_stats.consecutive_fails = 0;
		if (CONFIG_AWS_PUBLISH_WATCHDOG_SECONDS != 0) {
			k_work_reschedule(
				&publish_watchdog,
				K_SECONDS(CONFIG_AWS_PUBLISH_WATCHDOG_SECONDS));
		}
	} else {
		aws_stats.failure += 1;
		aws_stats.consecutive_fails += 1;
		AWS_LOG_ERR("MQTT publish err %u (%d)", aws_stats.failure, rc);
	}

	return rc;
}

#ifdef CONFIG_NET_L2_ETHERNET
/* Function taken from net_private.h
 * Copyright (c) 2016 Intel Corporation
 */
static char *net_sprint_ll_addr_lower(const uint8_t *ll)
{
	static char buf[sizeof("xxxxxxxxxxxx")];
	uint8_t i, len, blen;
	char *ptr = buf;

	len = (sizeof(buf) - 1) / HEX_CHARS_PER_HEX_VALUE;

	for (i = 0U, blen = sizeof(buf); i < len && blen > 0; i++) {
		ptr = net_byte_to_hex(ptr, (char)ll[i], 'a', true);
		blen -= 3U;
	}

	if (!(ptr - buf)) {
		return NULL;
	}

	*(ptr) = '\0';
	return buf;
}
#endif

/******************************************************************************/
/* Override in application                                                    */
/******************************************************************************/
__weak void awsDisconnectCallback(void)
{
	return;
}
