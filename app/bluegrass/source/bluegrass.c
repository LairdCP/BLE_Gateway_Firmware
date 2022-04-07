/**
 * @file bluegrass.c
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(bluegrass, CONFIG_BLUEGRASS_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>
#include <stdio.h>

#include "aws.h"
#include "bluegrass_json.h"
#include "lte.h"
#include "lcz_memfault.h"
#include "attr.h"
#include "app_version.h"
#include "cloud.h"
#include "gateway_fsm.h"
#include "shadow_parser.h"
#include "shadow_parser_flags_aws.h"
#include "lcz_certs.h"

#ifdef CONFIG_SENSOR_TASK
#include "sensor_task.h"
#include "sensor_table.h"
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

#ifdef CONFIG_CONTACT_TRACING
#include "ct_ble.h"
#endif

#if defined(CONFIG_NET_L2_ETHERNET)
#include <net/dns_resolve.h>
#include <net/ethernet.h>
#include "net_private.h"
#include "ethernet_network.h"
#endif

#include "bluegrass.h"

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define CONNECT_TO_SUBSCRIBE_DELAY 4

#define CONVERSION_MAX_STR_LEN 10
#define HEX_CHARS_PER_HEX_VALUE 2

#if CONFIG_AWS_PUBLISH_WATCHDOG_SECONDS != 0
BUILD_ASSERT((CONFIG_AWS_PUBLISH_WATCHDOG_SECONDS / 2) >
		     CONFIG_BLUEGRASS_HEARTBEAT_SECONDS,
	     "Incompatible publish watchdog and heartbeat configuration");
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct {
	bool init_request;
	bool gateway_subscribed;
	bool subscribed_to_get_accepted;
	bool get_shadow_processed;
	struct k_work_delayable heartbeat;
	uint32_t subscription_delay;
} bg;

static struct shadow_reported_struct shadow_persistent_data;

#if CONFIG_SHADOW_FAUX_DATA_STR_SIZE != 0
static char faux_char = 'a';
static char faux_data_str[CONFIG_SHADOW_FAUX_DATA_STR_SIZE];
#endif

static struct shadow_parser_agent get_accepted_agent;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void heartbeat_work_handler(struct k_work *work);

static void init_shadow(void);

static int publish_shadow_persistent_data();
static int publish_heartbeat(void);
static int publish_ess_sensor_data(float temperature, float humidity,
				   float pressure);

#ifdef CONFIG_NET_L2_ETHERNET
static char *net_sprint_ll_addr_lower(const uint8_t *ll);
#endif

static void set_default_client_id(void);

static void get_accepted_parser(const char *topic, struct topic_flags *flags);

static FwkMsgHandler_t sensor_publish_msg_handler;
static FwkMsgHandler_t gateway_publish_msg_handler;
static FwkMsgHandler_t subscription_msg_handler;
static FwkMsgHandler_t get_accepted_msg_handler;
static FwkMsgHandler_t ess_sensor_msg_handler;
static FwkMsgHandler_t heartbeat_msg_handler;
static FwkMsgHandler_t connected_msg_handler;
static FwkMsgHandler_t disconnected_msg_handler;
static FwkMsgHandler_t bluegrass_subscription_handler;

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void bluegrass_initialize(void)
{
	k_work_init_delayable(&bg.heartbeat, heartbeat_work_handler);

#ifdef CONFIG_SENSOR_TASK
	SensorTask_Initialize();
#endif

	set_default_client_id();

	get_accepted_agent.parser = get_accepted_parser;
	shadow_parser_register_agent(&get_accepted_agent);

	cloud_init_shadow_request();
}

bool bluegrass_ready_for_publish(void)
{
	return (aws_connected() && bg.get_shadow_processed &&
		bg.gateway_subscribed);
}

/* Don't load certs unless commissioned flag is set */
int cloud_commission(void)
{
	int r = -EPERM;

	if (attr_get_uint32(ATTR_ID_commissioned, 0) == 0) {
		attr_set_signed32(ATTR_ID_cert_status, r);
		return r;
	} else {
		return lcz_certs_load();
	}
}

int cloud_decommission(void)
{
	return lcz_certs_unload();
}

int cloud_commission_handler(void)
{
	attr_set_signed32(ATTR_ID_cert_status, CERT_STATUS_BUSY);
	attr_set_uint32(ATTR_ID_commissioning_busy, true);

#ifdef CONFIG_SENSOR_TASK
	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_CLOUD, FWK_ID_SENSOR_TASK,
				      FMC_DECOMMISSION);
#endif

	/* If the value is written, then always decommission so that the connection
	 * is closed and the certs are unloaded.  The files aren't deleted.
	 * If commission is true, then the state machine will load the certs
	 * from the file system after the join cloud delay has expired.
	 */
	gateway_fsm_request_decommission();

	return 0;
}

/******************************************************************************/
/* Weak Overrides                                                             */
/******************************************************************************/
FwkMsgHandler_t *cloud_sub_task_msg_dispatcher(FwkMsgCode_t MsgCode)
{
	/* clang-format off */
	switch (MsgCode)
	{
	case FMC_PERIODIC:                  return bluegrass_subscription_handler;
	case FMC_SENSOR_PUBLISH:            return sensor_publish_msg_handler;
	case FMC_GATEWAY_OUT:               return gateway_publish_msg_handler;
	case FMC_SUBSCRIBE:                 return subscription_msg_handler;
	case FMC_GET_ACCEPTED_RECEIVED:     return get_accepted_msg_handler;
	case FMC_ESS_SENSOR_EVENT:          return ess_sensor_msg_handler;
	case FMC_CLOUD_HEARTBEAT:           return heartbeat_msg_handler;
	case FMC_CLOUD_CONNECTED:           return connected_msg_handler;
	case FMC_CLOUD_DISCONNECTED:        return disconnected_msg_handler;
	default:                            return NULL;
	}
	/* clang-format on */
}

void cloud_init_shadow_request(void)
{
	bg.init_request = true;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void heartbeat_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	extern struct mqtt_client *aws_get_mqtt_client(void);
	LCZ_MEMFAULT_PUBLISH_DATA(aws_get_mqtt_client());

	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_CLOUD, FWK_ID_CLOUD,
				      FMC_CLOUD_HEARTBEAT);
}

/* Start heartbeat. Init shadow if required. Send CT stashed data. */
static DispatchResult_t connected_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					      FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);

	k_work_schedule(&bg.heartbeat, K_NO_WAIT);

	init_shadow();

	LCZ_MEMFAULT_BUILD_TOPIC(CONFIG_LCZ_MEMFAULT_MQTT_TOPIC, CONFIG_BOARD,
				 attr_get_quasi_static(ATTR_ID_gateway_id),
				 CONFIG_MEMFAULT_NCS_PROJECT_KEY);

#ifdef CONFIG_CONTACT_TRACING
	ct_ble_publish_dummy_data_to_aws();
	/* Try to send stashed entries immediately on re-connect */
	ct_ble_check_stashed_log_entries();
#endif

	return DISPATCH_OK;
}

static DispatchResult_t disconnected_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);

	bg.gateway_subscribed = false;
	bg.subscribed_to_get_accepted = false;
	bg.get_shadow_processed = false;

	return DISPATCH_OK;
}

/* Must be periodically called to process subscriptions.
 * The gateway shadow must be processed on connection.
 * The delta topic must be subscribed to.
 */
static DispatchResult_t
bluegrass_subscription_handler(FwkMsgReceiver_t *pMsgRxer, FwkMsg_t *pMsg)
{
	int rc = 0;

	if (!aws_connected()) {
		bg.subscription_delay = CONNECT_TO_SUBSCRIBE_DELAY;
		return rc;
	}

	if (IS_ENABLED(CONFIG_USE_SINGLE_AWS_TOPIC)) {
		return rc;
	}

	if (bg.subscription_delay > 0) {
		bg.subscription_delay -= 1;
		return rc;
	}

	if (!bg.subscribed_to_get_accepted) {
		rc = aws_subscribe_to_get_accepted();
		if (rc == 0) {
			bg.subscribed_to_get_accepted = true;
		}
	}

	if (!bg.get_shadow_processed) {
		rc = aws_get_shadow();
	}

	if (bg.get_shadow_processed && !bg.gateway_subscribed) {
		rc = aws_subscribe(GATEWAY_TOPIC, true);
		if (rc == 0) {
			bg.gateway_subscribed = true;

			FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_CLOUD,
							   FMC_CLOUD_READY);
		}
	}

	return rc;
}

static void init_shadow(void)
{
	int r;

	/* The shadow init is only sent once after the very first connect. */
	if (bg.init_request) {
		aws_generate_gateway_topics(
			attr_get_quasi_static(ATTR_ID_gateway_id));

		r = publish_shadow_persistent_data();

		if (r != 0) {
			LOG_ERR("Could not publish shadow (%d)", r);
		} else {
			bg.init_request = false;
		}
	}
}

static DispatchResult_t sensor_publish_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	JsonMsg_t *pJsonMsg = (JsonMsg_t *)pMsg;

	if (bluegrass_ready_for_publish()) {
		aws_send_data(pJsonMsg->buffer,
			      IS_ENABLED(CONFIG_USE_SINGLE_AWS_TOPIC) ?
					    GATEWAY_TOPIC :
					    pJsonMsg->topic);
	}

	return DISPATCH_OK;
}

static DispatchResult_t gateway_publish_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						    FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	JsonMsg_t *pJsonMsg = (JsonMsg_t *)pMsg;

	aws_send_data(pJsonMsg->buffer, GATEWAY_TOPIC);

	return DISPATCH_OK;
}

static DispatchResult_t subscription_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg)
{
#ifdef CONFIG_SENSOR_TASK
	ARG_UNUSED(pMsgRxer);
	SubscribeMsg_t *pSubMsg = (SubscribeMsg_t *)pMsg;
	int r;

	r = aws_subscribe(pSubMsg->topic, pSubMsg->subscribe);
	pSubMsg->success = (r == 0);

	FRAMEWORK_MSG_REPLY(pSubMsg, FMC_SUBSCRIBE_ACK);

	return DISPATCH_DO_NOT_FREE;
#else
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);
	return DISPATCH_OK;
#endif
}

static DispatchResult_t get_accepted_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);
	int r;

	r = aws_unsubscribe_from_get_accepted();
	if (r == 0) {
		bg.get_shadow_processed = true;
	}
	return DISPATCH_OK;
}

static DispatchResult_t ess_sensor_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					       FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ESSSensorMsg_t *pBmeMsg = (ESSSensorMsg_t *)pMsg;

	publish_ess_sensor_data(pBmeMsg->temperatureC, pBmeMsg->humidityPercent,
				pBmeMsg->pressurePa);

	return DISPATCH_OK;
}

static DispatchResult_t heartbeat_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					      FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);

	int r = publish_heartbeat();
	if (r != 0) {
		LOG_ERR("Unable to publish heartbeat %d", r);
	}

#if CONFIG_BLUEGRASS_HEARTBEAT_SECONDS != 0
	k_work_schedule(&bg.heartbeat,
			K_SECONDS(CONFIG_BLUEGRASS_HEARTBEAT_SECONDS));
#endif

	return DISPATCH_OK;
}

static int publish_shadow_persistent_data(void)
{
	int rc = -ENOMEM;
	ssize_t buf_len;

	struct shadow_persistent_values *reported =
		&shadow_persistent_data.state.reported;

	reported->os_version = KERNEL_VERSION_STRING;
	reported->firmware_version = APP_VERSION_STRING;

#ifdef CONFIG_MODEM_HL7800
	reported->IMEI = attr_get_quasi_static(ATTR_ID_gateway_id);
	reported->ICCID = attr_get_quasi_static(ATTR_ID_iccid);
	reported->radio_version = attr_get_quasi_static(ATTR_ID_lte_version);
	reported->radio_sn = attr_get_quasi_static(ATTR_ID_lte_serial_number);
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

#ifdef CONFIG_NET_L2_ETHERNET

	reported->ethernet.MAC = net_sprint_ll_addr_lower(
		attr_get_quasi_static(ATTR_ID_ethernet_mac));
	reported->ethernet.type = (uint32_t)ETHERNET_TYPE_IPV4;
	reported->ethernet.mode = attr_get_uint32(
		ATTR_ID_ethernet_mode, (uint32_t)ETHERNET_MODE_STATIC);
	reported->ethernet.speed = attr_get_uint32(
		ATTR_ID_ethernet_speed, (uint32_t)ETHERNET_SPEED_UNKNOWN);
	reported->ethernet.duplex = attr_get_uint32(
		ATTR_ID_ethernet_duplex, (uint32_t)ETHERNET_DUPLEX_UNKNOWN);
	reported->ethernet.IPAddress =
		attr_get_quasi_static(ATTR_ID_ethernet_ip_address);
	reported->ethernet.netmaskLength =
		attr_get_uint32(ATTR_ID_ethernet_netmask_length, 0);
	reported->ethernet.gateway =
		attr_get_quasi_static(ATTR_ID_ethernet_gateway);
	reported->ethernet.DNS = attr_get_quasi_static(ATTR_ID_ethernet_dns);

#if defined(CONFIG_NET_DHCPV4)
	reported->ethernet.DHCPLeaseTime =
		attr_get_uint32(ATTR_ID_ethernet_dhcp_lease_time, 0);
	reported->ethernet.DHCPRenewTime =
		attr_get_uint32(ATTR_ID_ethernet_dhcp_renew_time, 0);
	reported->ethernet.DHCPState =
		attr_get_uint32(ATTR_ID_ethernet_dhcp_state, 0);
	reported->ethernet.DHCPAttempts =
		attr_get_uint32(ATTR_ID_ethernet_dhcp_attempts, 0);
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
		LOG_ERR("k_calloc failed");
		return rc;
	}

	rc = json_obj_encode_buf(shadow_descr, ARRAY_SIZE(shadow_descr),
				 &shadow_persistent_data, msg, buf_len);
	if (rc < 0) {
		LOG_ERR("JSON encode failed");
		goto done;
	}

#ifdef CONFIG_BLUEGRASS_CLEAR_SHADOW_ON_STARTUP
	/* Clear the shadow and start fresh */
	rc = aws_send_data(SHADOW_STATE_NULL, GATEWAY_TOPIC);
	if (rc < 0) {
		LOG_ERR("Clear shadow failed");
		goto done;
	}
#endif

	rc = aws_send_data(msg, GATEWAY_TOPIC);
	if (rc < 0) {
		LOG_ERR("Update persistent shadow data failed");
		goto done;
	} else {
		LOG_INF("Sent persistent shadow data");
	}

done:
	k_free(msg);
	return rc;
}

/* BL654 Sensor with BME280 or other ESS device */
static int publish_heartbeat(void)
#if defined(CONFIG_BOARD_MG100)
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
		attr_get_signed32(ATTR_ID_lte_rsrp, 0), SHADOW_RADIO_SINR,
		attr_get_signed32(ATTR_ID_lte_sinr, 0), SHADOW_REPORTED_END);

	return aws_send_data(msg, GATEWAY_TOPIC);
}
#elif defined(CONFIG_BOARD_PINNACLE_100_DVK)
#if CONFIG_SHADOW_FAUX_DATA_STR_SIZE == 0
{
	char msg[strlen(SHADOW_REPORTED_START) + strlen(SHADOW_RADIO_RSSI) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_RADIO_SINR) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_REPORTED_END)];

	snprintf(msg, sizeof(msg), "%s%s%d,%s%d%s", SHADOW_REPORTED_START,
		 SHADOW_RADIO_RSSI, attr_get_signed32(ATTR_ID_lte_rsrp, 0),
		 SHADOW_RADIO_SINR, attr_get_signed32(ATTR_ID_lte_sinr, 0),
		 SHADOW_REPORTED_END);

	return aws_send_data(msg, GATEWAY_TOPIC);
}
#else
{
	char msg[strlen(SHADOW_REPORTED_START) + strlen(SHADOW_RADIO_RSSI) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_RADIO_SINR) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_FAUX_START) +
		 strlen(SHADOW_FAUX_END) + CONFIG_SHADOW_FAUX_DATA_STR_SIZE +
		 strlen(SHADOW_REPORTED_END)];

	memset(faux_data_str, faux_char, sizeof(faux_data_str) - 1);
	faux_char += 1;
	if (faux_char > 'z') {
		faux_char = 'a';
	}

	snprintf(msg, sizeof(msg), "%s%s%d,%s%d,%s%s%s%s",
		 SHADOW_REPORTED_START, SHADOW_RADIO_RSSI,
		 attr_get_signed32(ATTR_ID_lte_rsrp, 0), SHADOW_RADIO_SINR,
		 attr_get_signed32(ATTR_ID_lte_sinr, 0), SHADOW_FAUX_START,
		 faux_data_str, SHADOW_FAUX_END, SHADOW_REPORTED_END);

	return aws_send_data(msg, GATEWAY_TOPIC);
}
#endif /* CONFIG_SHADOW_FAUX_DATA_STR_SIZE */
#else
{
	return 0;
}
#endif

static int publish_ess_sensor_data(float temperature, float humidity,
				   float pressure)
{
	char msg[strlen(SHADOW_REPORTED_START) + strlen(SHADOW_TEMPERATURE) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_HUMIDITY) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_PRESSURE) +
		 CONVERSION_MAX_STR_LEN + strlen(SHADOW_REPORTED_END)];

	snprintf(msg, sizeof(msg), "%s%s%.2f,%s%.2f,%s%.1f%s",
		 SHADOW_REPORTED_START, SHADOW_TEMPERATURE, temperature,
		 SHADOW_HUMIDITY, humidity, SHADOW_PRESSURE, pressure,
		 SHADOW_REPORTED_END);

	return aws_send_data(msg, GATEWAY_TOPIC);
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

static void set_default_client_id(void)
{
	char *s;

	s = attr_get_quasi_static(ATTR_ID_client_id);

	/* Doesn't handle the board changing. */
	if (strlen(s) == 0) {
#if defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
		s = "bl5340";
#elif defined(CONFIG_BOARD_BL5340PA_DVK_CPUAPP)
		s = "bl5340pa";
#elif defined(CONFIG_BOARD_MG100)
		s = "mg100";
#else
		s = "pinnacle100_oob";
#endif
		attr_set_string(ATTR_ID_client_id, s, strlen(s));
	}
}

static void get_accepted_parser(const char *topic, struct topic_flags *flags)
{
	if (SP_FLAGS(get_accepted) && SP_FLAGS(gateway)) {
		FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_CLOUD, FWK_ID_CLOUD,
					      FMC_GET_ACCEPTED_RECEIVED);
	}
}
