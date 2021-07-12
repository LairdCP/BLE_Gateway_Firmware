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

#ifdef CONFIG_LCZ_MEMFAULT
#define BUILD_ID_SIZE 9
#endif

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void iface_ready_evt_handler(struct net_mgmt_event_callback *cb,
				    uint32_t mgmt_event, struct net_if *iface);
static void iface_down_evt_handler(struct net_mgmt_event_callback *cb,
				   uint32_t mgmt_event, struct net_if *iface);

static void setup_iface_events(void);

static void modem_event_callback(enum mdm_hl7800_event event, void *event_data);

static void get_local_time_from_modem(struct k_work *item);

static void lte_sync_qrtc(void);

#ifdef CONFIG_LCZ_MEMFAULT
static uint32_t lte_version_to_int(char *ver);
extern int memfault_ncs_device_id_set(const char *device_id, size_t len);
#endif

#ifdef CONFIG_MODEM_HL7800_GPS
static void gps_str_handler(void *event_data);
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct net_if *iface;
static struct net_if_config *cfg;
static struct dns_resolve_context *dns;
struct k_work local_time_work;
static struct tm local_time;
static int32_t local_offset;
static bool initialized;
static bool log_lte_dropped = false;

static struct mgmt_events iface_events[] = {
	{
		.event = NET_EVENT_DNS_SERVER_ADD,
		.handler = iface_ready_evt_handler,
	},
	{
		.event = NET_EVENT_IF_DOWN,
		.handler = iface_down_evt_handler,
	},
	{ 0 } /* The for loop below requires this extra location. */
};

#ifdef CONFIG_LCZ_MEMFAULT
static char build_id[BUILD_ID_SIZE];
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int lte_init(void)
{
	char *str;
	char *device_id;
	int rc = LTE_INIT_ERROR_NONE;
	int operator_index;

	if (!initialized) {
		initialized = true;
		k_work_init(&local_time_work, get_local_time_from_modem);
		mdm_hl7800_register_event_callback(modem_event_callback);
		setup_iface_events();
	}

#ifdef CONFIG_MODEM_HL7800_BOOT_DELAY
	rc = mdm_hl7800_reset();
	if (rc < 0) {
		rc = LTE_INIT_ERROR_MODEM;
		LOG_ERR("Unable to configure modem");
		attr_set_signed32(ATTR_ID_lteInitError, rc);
		return rc;
	}
#endif

	str = mdm_hl7800_get_imei();
	attr_set_string(ATTR_ID_gatewayId, str, strlen(str));
	str = mdm_hl7800_get_fw_version();
	MFLT_METRICS_SET_UNSIGNED(lte_ver, lte_version_to_int(str));
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

	attr_prepare_modemFunctionality();

	attr_set_signed32(ATTR_ID_lteInitError, rc);

#ifdef CONFIG_LCZ_MEMFAULT
	/* Provide ID after it is known. */
	device_id = attr_get_quasi_static(ATTR_ID_gatewayId);
	memfault_ncs_device_id_set(device_id, strlen(device_id));

	memfault_build_id_get_string(build_id, sizeof(build_id));
	attr_set_string(ATTR_ID_buildId, build_id, strlen(build_id));
#endif

	return rc;
}

int lte_network_init(void)
{
	int rc = LTE_INIT_ERROR_NONE;

#ifdef CONFIG_MODEM_HL7800_BOOT_IN_AIRPLANE_MODE
	rc = mdm_hl7800_set_functionality(HL7800_FUNCTIONALITY_FULL);
	if (rc < 0) {
		rc = LTE_INIT_ERROR_AIRPLANE;
		LOG_ERR("Unable to get modem out of airplane mode");
		goto exit;
	}
#endif

	attr_prepare_modemFunctionality();

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

exit:
	attr_set_signed32(ATTR_ID_lteInitError, rc);
	return rc;
}

bool lte_ready(void)
{
	bool ready = false;
	struct sockaddr_in *dnsAddr;

	if (iface != NULL && cfg != NULL && &dns->servers[0] != NULL) {
		dnsAddr = net_sin(&dns->servers[0].dns_server);
		ready = net_if_is_up(iface) && cfg->ip.ipv4 &&
			!net_ipv4_is_addr_unspecified(&dnsAddr->sin_addr);
	}

#ifdef CONFIG_BOARD_PINNACLE_100_DVK
	if (ready) {
		lcz_led_turn_on(NET_MGMT_LED);
	} else {
		lcz_led_turn_off(NET_MGMT_LED);
	}
#endif

	return ready;
}

void lcz_qrtc_sync_handler(void)
{
	if (lte_ready()) {
		lte_sync_qrtc();
	}
}

int attr_prepare_modemFunctionality(void)
{
	int r = mdm_hl7800_get_functionality();

	attr_set_signed32(ATTR_ID_modemFunctionality, r);
	return r;
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
}

static void iface_down_evt_handler(struct net_mgmt_event_callback *cb,
				   uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IF_DOWN) {
		return;
	}

	LTE_LOG_DBG("LTE is down");
	lcz_led_turn_off(NETWORK_LED);
}

/**
 * @note Events from different layers need their own cb
 */
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

static void modem_event_callback(enum mdm_hl7800_event event, void *event_data)
{
	uint8_t code = ((struct mdm_hl7800_compound_event *)event_data)->code;
	char *s = (char *)event_data;

	switch (event) {
	case HL7800_EVENT_NETWORK_STATE_CHANGE:
		attr_set_uint32(ATTR_ID_lteNetworkState, code);

		switch (code) {
		case HL7800_HOME_NETWORK:
		case HL7800_ROAMING:
			log_lte_dropped = true;
			lcz_led_turn_on(NETWORK_LED);
			lte_sync_qrtc();
			MFLT_METRICS_TIMER_STOP(lte_ttf);
			break;

		case HL7800_NOT_REGISTERED:
		case HL7800_REGISTRATION_DENIED:
		case HL7800_UNABLE_TO_CONFIGURE:
		case HL7800_OUT_OF_COVERAGE:
			lcz_led_turn_off(NETWORK_LED);
			MFLT_METRICS_TIMER_START(lte_ttf);
			if ((code == HL7800_OUT_OF_COVERAGE ||
			     code == HL7800_NOT_REGISTERED) &&
			    log_lte_dropped) {
				log_lte_dropped = false;
				MFLT_METRICS_ADD(lte_drop, 1);
			}
			break;

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
		MFLT_METRICS_SET_UNSIGNED(lte_ver, lte_version_to_int(s));
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

#ifdef CONFIG_MODEM_HL7800_GPS
	case HL7800_EVENT_GPS:
		gps_str_handler(event_data);
		break;

	case HL7800_EVENT_GPS_POSITION_STATUS:
		attr_set_signed32(ATTR_ID_gpsStatus,
				  (int32_t) * ((int8_t *)event_data));
		break;
#endif

	default:
		LOG_ERR("Unknown modem event");
		break;
	}
}

#ifdef CONFIG_MODEM_HL7800_GPS
static void gps_str_handler(void *event_data)
{
	uint8_t code = ((struct mdm_hl7800_compound_event *)event_data)->code;
	char *str = ((struct mdm_hl7800_compound_event *)event_data)->string;

	switch (code) {
	case HL7800_GPS_STR_LATITUDE:
		attr_set_string(ATTR_ID_gpsLatitude, str, strlen(str));
		break;
	case HL7800_GPS_STR_LONGITUDE:
		attr_set_string(ATTR_ID_gpsLongitude, str, strlen(str));
		break;
	case HL7800_GPS_STR_GPS_TIME:
		attr_set_string(ATTR_ID_gpsTime, str, strlen(str));
		break;
	case HL7800_GPS_STR_FIX_TYPE:
		attr_set_string(ATTR_ID_gpsFixType, str, strlen(str));
		break;
	case HL7800_GPS_STR_HEPE:
		attr_set_string(ATTR_ID_gpsHepe, str, strlen(str));
		break;
	case HL7800_GPS_STR_ALTITUDE:
		attr_set_string(ATTR_ID_gpsAltitude, str, strlen(str));
		break;
	case HL7800_GPS_STR_ALT_UNC:
		attr_set_string(ATTR_ID_gpsAltUnc, str, strlen(str));
		break;
	case HL7800_GPS_STR_DIRECTION:
		attr_set_string(ATTR_ID_gpsHeading, str, strlen(str));
		break;
	case HL7800_GPS_STR_HOR_SPEED:
		attr_set_string(ATTR_ID_gpsHorSpeed, str, strlen(str));
		break;
	case HL7800_GPS_STR_VER_SPEED:
		attr_set_string(ATTR_ID_gpsVerSpeed, str, strlen(str));
		break;
	}
}
#endif /* CONFIG_MODEM_HL7800_GPS */

static void get_local_time_from_modem(struct k_work *item)
{
	ARG_UNUSED(item);
	uint32_t epoch;
	int32_t status = mdm_hl7800_get_local_time(&local_time, &local_offset);

	if (status == 0) {
		epoch = lcz_qrtc_set_epoch_from_tm(&local_time, local_offset);
		LOG_INF("Epoch set to %u", epoch);
	} else {
		LOG_WRN("Get local time from modem failed! (%d)", status);
	}
}

static void lte_sync_qrtc(void)
{
	k_work_submit(&local_time_work);
}

#ifdef CONFIG_LCZ_MEMFAULT
/**
 * @brief Convert HL7800 version string from format 'HL7800.4.4.14.0' to
 * 04041400
 *
 * @param ver HL7800 version string
 * @return 0 on error or version as an int
 */
static uint32_t lte_version_to_int(char *ver)
{
	uint32_t ver_int = 0;
	int num_delims = 4;
	char *delims[num_delims];
	char *search_start;
	int major, minor, fix, build;

	search_start = ver;

	/* find all delimiters (.) */
	for (int i = 0; i < num_delims; i++) {
		delims[i] = strchr(search_start, '.');
		if (!delims[i]) {
			LOG_ERR("Could not find delim %d, val: %s", i,
				log_strdup(ver));
			return ver_int;
		}
		/* Start next search after current delim location */
		search_start = delims[i] + 1;
	}

	major = strtol(delims[0] + 1, NULL, 10);
	minor = strtol(delims[1] + 1, NULL, 10);
	fix = strtol(delims[2] + 1, NULL, 10);
	build = strtol(delims[3] + 1, NULL, 10);

	ver_int += major * 1000000;
	ver_int += minor * 10000;
	ver_int += fix * 100;
	ver_int += build;

	LOG_INF("LTE version int: %d", ver_int);
	return ver_int;
}
#endif
