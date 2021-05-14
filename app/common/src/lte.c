/**
 * @file lte.c
 * @brief LTE management
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lte, CONFIG_LTE_LOG_LEVEL);

#define LTE_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define LTE_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define LTE_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define LTE_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_mgmt.h>
#include <net/socket.h>

#include <drivers/modem/hl7800.h>
#include "fota_smp.h"
#include "led_configuration.h"
#include "lcz_qrtc.h"
#include "attr.h"
#include "gateway_common.h"

#ifdef CONFIG_BLUEGRASS
#include "bluegrass.h"
#endif

#ifdef CONFIG_COAP_FOTA
#include "coap_fota_shadow.h"
#endif
#if CONFIG_HTTP_FOTA
#include "http_fota_shadow.h"
#endif

#ifdef CONFIG_CONTACT_TRACING
#include "ct_ble.h"
#endif

#include "lte.h"

#include "lcz_memfault.h"

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
struct mdm_hl7800_apn *lte_apn_config;

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
struct mgmt_events {
	uint32_t event;
	net_mgmt_event_handler_t handler;
	struct net_mgmt_event_callback cb;
};

static const struct lcz_led_blink_pattern NETWORK_SEARCH_LED_PATTERN = {
	.on_time = CONFIG_DEFAULT_LED_ON_TIME_FOR_1_SECOND_BLINK,
	.off_time = CONFIG_DEFAULT_LED_OFF_TIME_FOR_1_SECOND_BLINK,
	.repeat_count = REPEAT_INDEFINITELY
};

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void iface_ready_evt_handler(struct net_mgmt_event_callback *cb,
				    uint32_t mgmt_event, struct net_if *iface);
static void iface_down_evt_handler(struct net_mgmt_event_callback *cb,
				   uint32_t mgmt_event, struct net_if *iface);

static void setup_iface_events(void);

static void modemEventCallback(enum mdm_hl7800_event event, void *event_data);

static void getLocalTimeFromModemWorkHandler(struct k_work *item);

static void lteSyncQrtc(void);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct net_if *iface;
static struct net_if_config *cfg;
static struct dns_resolve_context *dns;
struct k_work localTimeWork;
static struct tm localTime;
static int32_t localOffset;
static bool connected;
static bool wasConnected;

static struct mgmt_events iface_events[] = {
	{ .event = NET_EVENT_DNS_SERVER_ADD,
	  .handler = iface_ready_evt_handler },
	{ .event = NET_EVENT_IF_DOWN, .handler = iface_down_evt_handler },
	{ 0 } /* The for loop below requires this extra location. */
};

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int lteInit(void)
{
	char *str;
	int rc = LTE_INIT_ERROR_NONE;
	int operator_index;

#ifdef CONFIG_MODEM_HL7800_DELAY_START
	rc = mdm_hl7800_reset();
	if (rc < 0) {
		LOG_ERR("Unable to configure modem");
		goto exit;
	}
#endif

	k_work_init(&localTimeWork, getLocalTimeFromModemWorkHandler);

	mdm_hl7800_register_event_callback(modemEventCallback);
	setup_iface_events();

	iface = net_if_get_default();
	if (!iface) {
		LTE_LOG_ERR("Could not get iface");
		rc = LTE_INIT_ERROR_NO_IFACE;
		goto exit;
	}

	cfg = net_if_get_config(iface);
	if (!cfg) {
		LTE_LOG_ERR("Could not get iface config");
		rc = LTE_INIT_ERROR_IFACE_CFG;
		goto exit;
	}

	dns = dns_resolve_get_default();
	if (!dns) {
		LTE_LOG_ERR("Could not get DNS context");
		rc = LTE_INIT_ERROR_DNS_CFG;
		goto exit;
	}

	str = mdm_hl7800_get_imei();
	attr_set_string(ATTR_ID_gatewayId, str, strlen(str));
	str = mdm_hl7800_get_fw_version();
	attr_set_string(ATTR_ID_lteVersion, str, strlen(str));
	str = mdm_hl7800_get_iccid();
	attr_set_string(ATTR_ID_iccid, str, strlen(str));
	str = mdm_hl7800_get_sn();
	attr_set_string(ATTR_ID_lteSerialNumber, str, strlen(str));
	str = mdm_hl7800_get_imsi();
	attr_set_string(ATTR_ID_imsi, str, strlen(str));

	operator_index = mdm_hl7800_get_operator_index();
	if (operator_index >= 0) {
		attr_set_uint32(ATTR_ID_lteOperatorIndex, operator_index);
	}

	mdm_hl7800_generate_status_events();

exit:
	attr_set_signed32(ATTR_ID_lteInitError, rc);
	return rc;
}

bool lteIsReady(void)
{
	struct sockaddr_in *dnsAddr;

	if (iface != NULL && cfg != NULL && &dns->servers[0] != NULL) {
		dnsAddr = net_sin(&dns->servers[0].dns_server);
		return net_if_is_up(iface) && cfg->ip.ipv4 &&
		       !net_ipv4_is_addr_unspecified(&dnsAddr->sin_addr);
	}

	return false;
}

bool lteConnected(void)
{
	/* On the first connection, wait for the event. */
	if (connected) {
		return true;
	} else if (wasConnected) {
		return lteIsReady();
	} else {
		return false;
	}
}

void lcz_qrtc_sync_handler(void)
{
	if (lteConnected()) {
		lteSyncQrtc();
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void iface_ready_evt_handler(struct net_mgmt_event_callback *cb,
				    uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_DNS_SERVER_ADD) {
		return;
	}

	LTE_LOG_DBG("LTE is ready!");
	lcz_led_turn_on(NETWORK_LED);
	lteEvent(LTE_EVT_READY);
	connected = true;
	wasConnected = true;
}

static void iface_down_evt_handler(struct net_mgmt_event_callback *cb,
				   uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IF_DOWN) {
		return;
	}

	LTE_LOG_DBG("LTE is down");
	lcz_led_turn_off(NETWORK_LED);
	lteEvent(LTE_EVT_DISCONNECTED);
	connected = false;
}

static void setup_iface_events(void)
{
	int i;

	for (i = 0; iface_events[i].event; i++) {
		net_mgmt_init_event_callback(&iface_events[i].cb,
					     iface_events[i].handler,
					     iface_events[i].event);

		net_mgmt_add_event_callback(&iface_events[i].cb);
	}
}

static void modemEventCallback(enum mdm_hl7800_event event, void *event_data)
{
	uint8_t code = ((struct mdm_hl7800_compound_event *)event_data)->code;
	char *s = (char *)event_data;

	switch (event) {
	case HL7800_EVENT_NETWORK_STATE_CHANGE:
		attr_set_uint32(ATTR_ID_lteNetworkState, code);

		switch (code) {
		case HL7800_HOME_NETWORK:
		case HL7800_ROAMING:
			lcz_led_turn_on(NETWORK_LED);
			lteSyncQrtc();
			MFLT_METRICS_TIMER_STOP(lte_ttf);
			break;

		case HL7800_REGISTRATION_DENIED:
		case HL7800_UNABLE_TO_CONFIGURE:
		case HL7800_OUT_OF_COVERAGE:
			lcz_led_turn_off(NETWORK_LED);
			MFLT_METRICS_TIMER_START(lte_ttf);
			break;

		case HL7800_NOT_REGISTERED:
		case HL7800_SEARCHING:
			lcz_led_blink(NETWORK_LED, &NETWORK_SEARCH_LED_PATTERN);
			MFLT_METRICS_TIMER_START(lte_ttf);
			break;

		case HL7800_EMERGENCY:
		default:
			lcz_led_turn_off(NETWORK_LED);
			break;
		}
		break;

	case HL7800_EVENT_APN_UPDATE:
		/* event data points to static data stored in modem driver.
		 * Store the pointer so we can access the APN elsewhere in our app.
		 */
		lte_apn_config = (struct mdm_hl7800_apn *)event_data;
		attr_set_string(ATTR_ID_apn, lte_apn_config->value,
				MDM_HL7800_APN_MAX_STRLEN);
		attr_set_string(ATTR_ID_apnUsername, lte_apn_config->username,
				MDM_HL7800_APN_USERNAME_MAX_STRLEN);
		attr_set_string(ATTR_ID_apnPassword, lte_apn_config->password,
				MDM_HL7800_APN_PASSWORD_MAX_STRLEN);
		break;

	case HL7800_EVENT_RSSI:
		attr_set_signed32(ATTR_ID_lteRsrp, *((int *)event_data));
		MFLT_METRICS_SET_SIGNED(lte_rsrp, *((int *)event_data));
		break;

	case HL7800_EVENT_SINR:
		attr_set_signed32(ATTR_ID_lteSinr, *((int *)event_data));
		MFLT_METRICS_SET_SIGNED(lte_sinr, *((int *)event_data));
		break;

	case HL7800_EVENT_STARTUP_STATE_CHANGE:
		attr_set_uint32(ATTR_ID_lteStartupState, code);
		switch (code) {
		case HL7800_STARTUP_STATE_READY:
		case HL7800_STARTUP_STATE_WAITING_FOR_ACCESS_CODE:
			break;
		case HL7800_STARTUP_STATE_SIM_NOT_PRESENT:
		case HL7800_STARTUP_STATE_SIMLOCK:
		case HL7800_STARTUP_STATE_UNRECOVERABLE_ERROR:
		case HL7800_STARTUP_STATE_UNKNOWN:
		case HL7800_STARTUP_STATE_INACTIVE_SIM:
		default:
			lcz_led_turn_off(NETWORK_LED);
			break;
		}
		break;

	case HL7800_EVENT_SLEEP_STATE_CHANGE:
		attr_set_uint32(ATTR_ID_lteSleepState, code);
		break;

	case HL7800_EVENT_RAT:
		attr_set_uint32(ATTR_ID_lteRat, *((uint8_t *)event_data));
		break;

	case HL7800_EVENT_BANDS:
		attr_set_string(ATTR_ID_bands, s, strlen(s));
		break;

	case HL7800_EVENT_ACTIVE_BANDS:
		attr_set_string(ATTR_ID_activeBands, s, strlen(s));
		break;

	case HL7800_EVENT_FOTA_STATE:
		fota_smp_state_handler(*((uint8_t *)event_data));
		break;

	case HL7800_EVENT_FOTA_COUNT:
		fota_smp_set_count(*((uint32_t *)event_data));
		break;

	case HL7800_EVENT_REVISION:
		attr_set_string(ATTR_ID_lteVersion, s, strlen(s));
#ifdef CONFIG_BLUEGRASS
		/* Update shadow because modem version has changed. */
		bluegrass_init_shadow_request();
#endif
#ifdef CONFIG_COAP_FOTA
		/* This is duplicated for backwards compatability. */
		coap_fota_set_running_version(MODEM_IMAGE_TYPE,
					      (char *)event_data,
					      strlen((char *)event_data));
#endif
#ifdef CONFIG_HTTP_FOTA
		/* This is duplicated for backwards compatability. */
		http_fota_set_running_version(MODEM_IMAGE_TYPE,
					      (char *)event_data,
					      strlen((char *)event_data));
#endif
		break;

	default:
		LOG_ERR("Unknown modem event");
		break;
	}
}

static void getLocalTimeFromModemWorkHandler(struct k_work *item)
{
	ARG_UNUSED(item);

	int32_t status = mdm_hl7800_get_local_time(&localTime, &localOffset);

	if (status == 0) {
		LOG_INF("Epoch set to %u",
			lcz_qrtc_set_epoch_from_tm(&localTime, localOffset));
	} else {
		LOG_WRN("Get local time from modem failed! (%d)", status);
	}
}

static void lteSyncQrtc(void)
{
	k_work_submit(&localTimeWork);
}

__weak void lteEvent(enum lte_event event)
{
	ARG_UNUSED(event);
}
