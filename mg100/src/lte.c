/**
 * @file lte.c
 * @brief LTE management
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(oob_lte);

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
#include "ble_cellular_service.h"
#include "fota.h"
#include "led_configuration.h"
#include "qrtc.h"

#include "lte.h"

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
#ifdef CONFIG_BLUEGRASS
extern bool initShadow;
#endif

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
struct mgmt_events {
	uint32_t event;
	net_mgmt_event_handler_t handler;
	struct net_mgmt_event_callback cb;
};

static const struct led_blink_pattern NETWORK_SEARCH_LED_PATTERN = {
	.on_time = CONFIG_DEFAULT_LED_ON_TIME_FOR_1_SECOND_BLINK,
	.off_time = CONFIG_DEFAULT_LED_OFF_TIME_FOR_1_SECOND_BLINK,
	.repeat_count = REPEAT_INDEFINITELY
};

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void onLteEvent(enum lte_event event);

static void iface_ready_evt_handler(struct net_mgmt_event_callback *cb,
				    uint32_t mgmt_event, struct net_if *iface);
static void iface_down_evt_handler(struct net_mgmt_event_callback *cb,
				   uint32_t mgmt_event, struct net_if *iface);

static void setup_iface_events(void);

static void modemEventCallback(enum mdm_hl7800_event event, void *event_data);

static void getLocalTimeFromModemWorkHandler(struct k_work *item);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct net_if *iface;
static struct net_if_config *cfg;
static struct dns_resolve_context *dns;
static struct lte_status lteStatus;
static lte_event_function_t lteCallbackFunction = NULL;
struct k_work localTimeWork;
static struct tm localTime;
static int32_t localOffset;

static struct mgmt_events iface_events[] = {
	{ .event = NET_EVENT_DNS_SERVER_ADD,
	  .handler = iface_ready_evt_handler },
	{ .event = NET_EVENT_IF_DOWN, .handler = iface_down_evt_handler },
	{ 0 } /* The for loop below requires this extra location. */
};

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void lteRegisterEventCallback(lte_event_function_t callback)
{
	lteCallbackFunction = callback;
}

int lteInit(void)
{
	int rc = LTE_ERR_NONE;
	mdm_hl7800_register_event_callback(modemEventCallback);
	setup_iface_events();
	k_work_init(&localTimeWork, getLocalTimeFromModemWorkHandler);

	/* wait for network interface to be ready */
	iface = net_if_get_default();
	if (!iface) {
		LTE_LOG_ERR("Could not get iface");
		rc = LTE_ERR_NO_IFACE;
		goto exit;
	}

	cfg = net_if_get_config(iface);
	if (!cfg) {
		LTE_LOG_ERR("Could not get iface config");
		rc = LTE_ERR_IFACE_CFG;
		goto exit;
	}

	dns = dns_resolve_get_default();
	if (!dns) {
		LTE_LOG_ERR("Could not get DNS context");
		rc = LTE_ERR_DNS_CFG;
		goto exit;
	}

	lteStatus.radio_version = (const char *)mdm_hl7800_get_fw_version();
	lteStatus.IMEI = (const char *)mdm_hl7800_get_imei();
	lteStatus.ICCID = (const char *)mdm_hl7800_get_iccid();
	lteStatus.serialNumber = (const char *)mdm_hl7800_get_sn();
	mdm_hl7800_generate_status_events();

exit:
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

struct lte_status *lteGetStatus(void)
{
	mdm_hl7800_get_signal_quality(&lteStatus.rssi, &lteStatus.sinr);
	return &lteStatus;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void onLteEvent(enum lte_event event)
{
	if (lteCallbackFunction != NULL) {
		lteCallbackFunction(event);
	}
}

static void iface_ready_evt_handler(struct net_mgmt_event_callback *cb,
				    uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_DNS_SERVER_ADD) {
		return;
	}

	LTE_LOG_DBG("LTE is ready!");
	led_turn_on(RED_LED3);
	onLteEvent(LTE_EVT_READY);
	k_work_submit(&localTimeWork);
}

static void iface_down_evt_handler(struct net_mgmt_event_callback *cb,
				   uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IF_DOWN) {
		return;
	}

	LTE_LOG_DBG("LTE is down");
	led_turn_off(RED_LED3);
	onLteEvent(LTE_EVT_DISCONNECTED);
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
	switch (event) {
	case HL7800_EVENT_NETWORK_STATE_CHANGE:
		cell_svc_set_network_state(code);

		switch (code) {
		case HL7800_HOME_NETWORK:
		case HL7800_ROAMING:
			led_turn_on(RED_LED3);
			break;

		case HL7800_REGISTRATION_DENIED:
		case HL7800_UNABLE_TO_CONFIGURE:
		case HL7800_OUT_OF_COVERAGE:
			led_turn_off(RED_LED3);
			break;

		case HL7800_NOT_REGISTERED:
		case HL7800_SEARCHING:
			led_blink(RED_LED3, &NETWORK_SEARCH_LED_PATTERN);
			break;

		case HL7800_EMERGENCY:
		default:
			led_turn_off(RED_LED3);
			break;
		}
		break;

	case HL7800_EVENT_APN_UPDATE:
		cell_svc_set_apn((struct mdm_hl7800_apn *)event_data);
		break;

	case HL7800_EVENT_RSSI:
		cell_svc_set_rssi(*((int *)event_data));
		break;

	case HL7800_EVENT_SINR:
		cell_svc_set_sinr(*((int *)event_data));
		break;

	case HL7800_EVENT_STARTUP_STATE_CHANGE:
		cell_svc_set_startup_state(code);
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
			led_turn_off(RED_LED3);
			break;
		}
		break;

	case HL7800_EVENT_SLEEP_STATE_CHANGE:
		cell_svc_set_sleep_state(code);
		break;

	case HL7800_EVENT_RAT:
		cell_svc_set_rat(*((uint8_t *)event_data));
		break;

	case HL7800_EVENT_BANDS:
		cell_svc_set_bands((char *)event_data);
		break;

	case HL7800_EVENT_ACTIVE_BANDS:
		cell_svc_set_active_bands((char *)event_data);
		break;

	case HL7800_EVENT_FOTA_STATE:
		fota_state_handler(*((uint8_t *)event_data));
		break;

	case HL7800_EVENT_FOTA_COUNT:
		fota_set_count(*((uint32_t *)event_data));
		break;

	case HL7800_EVENT_REVISION:
		cell_svc_set_fw_ver((char *)event_data);
#ifdef CONFIG_BLUEGRASS
		/* Update shadow because modem version has changed. */
		initShadow = true;
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

	if (!Qrtc_EpochWasSet()) {
		int32_t status =
			mdm_hl7800_get_local_time(&localTime, &localOffset);
		if (status == 0) {
			LOG_INF("Epoch set to %u",
				Qrtc_SetEpochFromTm(&localTime, localOffset));
		}
	}
}
