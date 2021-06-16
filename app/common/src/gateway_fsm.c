/**
 * @file gateway_fsm.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(gateway_fsm, CONFIG_GATEWAY_FSM_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>

#include "laird_utility_macros.h"
#include "aws.h"
#if defined(CONFIG_MODEM_HL7800)
#include "lte.h"
#endif
#if defined(CONFIG_NET_L2_ETHERNET)
#include "ethernet_network.h"
#endif
#include "lcz_certs.h"
#include "attr.h"
#ifdef CONFIG_BLUEGRASS
#include "bluegrass.h"
#endif
#include "gateway_fsm.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/

#define ERROR_RETRY_SECONDS 120

#define RESOLVE_SERVER_RETRY_SECONDS 60

/** Workaround for sporadic DNS error */
#define NETWORK_CONNECT_CALLBACK_DELAY 4

#define CLOUD_CONNECT_TIMEOUT 10

typedef int gsm_func(void);
typedef bool gsm_status_func(void);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct {
	enum gateway_state state;
	uint32_t timer;

	bool modem_and_network_init_complete;
	bool server_resolved;
	bool cloud_disconnect_request;
	bool decommission_request;

	gsm_func *modem_init;
	gsm_func *network_init;
	gsm_status_func *network_is_connected;
	gsm_func *resolve_server;
	gsm_func *cloud_connect;
	gsm_func *cloud_disconnect;
	gsm_status_func *cloud_is_connected;
	gsm_func *cert_load;
	gsm_func *cert_unload;
} gsm;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void modem_init_handler(void);
static void network_init_handler(void);
static void wait_for_network_handler(void);
static void wait_for_commission_handler(void);
static void resolve_server_handler(void);
static void wait_before_cloud_connect_handler(void);
static void cloud_connecting_handler(void);
static void cloud_connected_handler(void);
static void disconnected_handler(void);
static void fota_handler(void);
static void decommission_handler(void);

static bool timer_expired(void);

static void set_state(enum gateway_state next_state);

static uint32_t get_modem_init_delay(void);
static uint32_t get_join_network_delay(void);
static uint32_t get_join_cloud_delay(void);
static uint32_t get_reconnect_cloud_delay(void);

#if defined(CONFIG_LWM2M) || defined(CONFIG_NET_L2_ETHERNET)
static int unused_function(void);
#endif
#if defined(CONFIG_LWM2M)
static bool status_true(void);
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void gateway_fsm_init(void)
{
#if defined(CONFIG_LWM2M)
#if defined(CONFIG_MODEM_HL7800)
	/* MG100 or Pinnacle 100 LwM2M */
	gsm.modem_init = lte_init;
	gsm.network_init = lte_network_init;
	gsm.network_is_connected = status_true;
#elif defined(CONFIG_NET_L2_ETHERNET)
	/* BL5340 LwM2M */
	gsm.modem_init = unused_function;
	gsm.network_init = ethernet_network_init;
	gsm.network_is_connected = ethernet_network_connected;
#else
	/* Unknown board LwM2M */
#error "Unknown board/network configuration, add to gateway_fsm_init()"
#endif

	gsm.resolve_server = unused_function;
	gsm.cloud_connect = unused_function;
	gsm.cloud_disconnect = unused_function;
	gsm.cloud_is_connected = status_true;
	gsm.cert_load = unused_function;
	gsm.cert_unload = unused_function;
#else

#if defined(CONFIG_MODEM_HL7800)
	/* MG100 or Pinnacle 100 Bluegrass/CT */
	gsm.modem_init = lte_init;
	gsm.network_init = lte_network_init;
	gsm.network_is_connected = lte_ready;
#elif defined(CONFIG_NET_L2_ETHERNET)
	/* BL5340 Bluegrass/CT */
	gsm.modem_init = unused_function;
	gsm.network_init = ethernet_network_init;
	gsm.network_is_connected = ethernet_network_connected;
#else
	/* Unknown board Bluegrass/CT */
#error "Unknown board/network configuration, add to gateway_fsm_init()"
#endif

	gsm.resolve_server = awsGetServerAddr;
	gsm.cloud_connect = awsConnect;
	gsm.cloud_disconnect = awsDisconnect;
	gsm.cloud_is_connected = awsConnected;
	gsm.cert_load = lcz_certs_load;
	gsm.cert_unload = lcz_certs_unload;
#endif
}

void gateway_fsm(void)
{
	switch (gsm.state) {
	case GATEWAY_STATE_POWER_UP_INIT:
		set_state(GATEWAY_STATE_MODEM_INIT);
		gsm.timer = get_modem_init_delay();
		break;

	case GATEWAY_STATE_MODEM_INIT:
		modem_init_handler();
		break;

	case GATEWAY_STATE_NETWORK_INIT:
		network_init_handler();
		break;

	case GATEWAY_STATE_WAIT_FOR_NETWORK:
		wait_for_network_handler();
		break;

	case GATEWAY_STATE_NETWORK_CONNECTED:
#if defined(CONFIG_LWM2M)
		if (!gsm.network_is_connected()) {
			set_state(GATEWAY_STATE_NETWORK_DISCONNECTED);
		}
#else
		set_state(GATEWAY_STATE_WAIT_FOR_COMMISSION);
#endif
		break;

	case GATEWAY_STATE_NETWORK_DISCONNECTED:
		gateway_fsm_network_disconnected_callback();
		gateway_fsm_cloud_disconnected_callback();
		set_state(GATEWAY_STATE_CLOUD_WAIT_FOR_DISCONNECT);
		break;

	case GATEWAY_STATE_MODEM_ERROR:
	case GATEWAY_STATE_NETWORK_ERROR:
		if (gateway_fsm_network_error_callback() == 0) {
			set_state(GATEWAY_STATE_MODEM_INIT);
			gsm.timer = ERROR_RETRY_SECONDS;
		}
		break;

	case GATEWAY_STATE_WAIT_FOR_COMMISSION:
		wait_for_commission_handler();
		break;

	case GATEWAY_STATE_RESOLVE_SERVER:
		resolve_server_handler();
		break;

	case GATEWAY_STATE_WAIT_BEFORE_CLOUD_CONNECT:
		wait_before_cloud_connect_handler();
		break;

	case GATEWAY_STATE_CLOUD_CONNECTING:
		cloud_connecting_handler();
		break;

	case GATEWAY_STATE_CLOUD_CONNECTED:
		cloud_connected_handler();
		break;

	case GATEWAY_STATE_CLOUD_REQUEST_DISCONNECT:
		gsm.cloud_disconnect_request = false;
		if (gsm.cloud_disconnect() == 0) {
			set_state(GATEWAY_STATE_CLOUD_WAIT_FOR_DISCONNECT);
		} else {
			set_state(GATEWAY_STATE_CLOUD_ERROR);
		}
		break;

	case GATEWAY_STATE_CLOUD_WAIT_FOR_DISCONNECT:
		if (!gsm.cloud_is_connected()) {
			set_state(GATEWAY_STATE_CLOUD_DISCONNECTED);
		}
		break;

	case GATEWAY_STATE_CLOUD_DISCONNECTED:
		disconnected_handler();
		gateway_fsm_cloud_disconnected_callback();
		break;

	case GATEWAY_STATE_CLOUD_ERROR:
		set_state(GATEWAY_STATE_CLOUD_DISCONNECTED);
		break;

	case GATEWAY_STATE_FOTA_BUSY:
		fota_handler();
		break;

	case GATEWAY_STATE_DECOMMISSION:
		decommission_handler();
		break;

	default:
		break;
	}
}

void gateway_fsm_request_decommission(void)
{
	gsm.cloud_disconnect_request = true;
	gsm.decommission_request = true;
}

void gateway_fsm_request_cloud_disconnect(void)
{
	gsm.cloud_disconnect_request = true;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void set_state(enum gateway_state next_state)
{
	if (next_state != gsm.state) {
		gsm.state = next_state;
		attr_set_uint32(ATTR_ID_gatewayState, gsm.state);
	}
}

static bool timer_expired(void)
{
	if (gsm.timer > 0) {
		gsm.timer -= 1;
	}

	if (gsm.timer == 0) {
		return true;
	} else {
		return false;
	}
}

/**
 * @brief Initialize modem after wait has expired.
 */
static void modem_init_handler(void)
{
	if (timer_expired()) {
		if (gsm.modem_init() < 0) {
			set_state(GATEWAY_STATE_MODEM_ERROR);
		} else {
			gsm.timer = get_join_network_delay();
			set_state(GATEWAY_STATE_NETWORK_INIT);
			gateway_fsm_modem_init_complete_callback();
		}
	}
}

/**
 * @brief Initialize (LTE) network after wait has expired.
 */
static void network_init_handler(void)
{
	if (timer_expired()) {
		if (gsm.network_init() < 0) {
			gsm.modem_and_network_init_complete = false;
			set_state(GATEWAY_STATE_NETWORK_ERROR);
		} else {
			gsm.modem_and_network_init_complete = true;
			gsm.timer = NETWORK_CONNECT_CALLBACK_DELAY;
			set_state(GATEWAY_STATE_WAIT_FOR_NETWORK);
			gateway_fsm_network_init_complete_callback();
		}
	}
}

static void wait_for_network_handler(void)
{
	if (!gsm.modem_and_network_init_complete) {
		set_state(GATEWAY_STATE_MODEM_INIT);
	} else if (gsm.network_is_connected()) {
		if (timer_expired()) {
			gateway_fsm_network_connected_callback();
			set_state(GATEWAY_STATE_NETWORK_CONNECTED);
		}
	} else {
		gsm.timer = NETWORK_CONNECT_CALLBACK_DELAY;
	}
}

static void wait_for_commission_handler(void)
{
	if (gsm.cloud_disconnect_request) {
		set_state(GATEWAY_STATE_CLOUD_REQUEST_DISCONNECT);
	} else if (gsm.cert_load() == 0) {
		set_state(GATEWAY_STATE_RESOLVE_SERVER);
	} else {
		attr_set_uint32(ATTR_ID_commissioningBusy, false);
	}
}

static void resolve_server_handler(void)
{
	if (!gsm.network_is_connected()) {
		set_state(GATEWAY_STATE_NETWORK_DISCONNECTED);
	} else if (gsm.server_resolved) {
		set_state(GATEWAY_STATE_WAIT_BEFORE_CLOUD_CONNECT);
		gsm.timer = get_join_cloud_delay();
	} else if (timer_expired()) {
		if (gsm.resolve_server() == 0) {
			set_state(GATEWAY_STATE_WAIT_BEFORE_CLOUD_CONNECT);
			gsm.timer = get_join_cloud_delay();
			gsm.server_resolved = true;
		} else {
			set_state(GATEWAY_STATE_RESOLVE_SERVER);
			gsm.timer = RESOLVE_SERVER_RETRY_SECONDS;
		}
	}
}

static void wait_before_cloud_connect_handler(void)
{
	if (!gsm.network_is_connected()) {
		set_state(GATEWAY_STATE_NETWORK_DISCONNECTED);
	} else if (gateway_fsm_fota_request() || gsm.cloud_disconnect_request) {
		set_state(GATEWAY_STATE_CLOUD_REQUEST_DISCONNECT);
	} else if (timer_expired()) {
		set_state(GATEWAY_STATE_CLOUD_CONNECTING);
		if (gsm.cloud_connect() == 0) {
			gsm.timer = CLOUD_CONNECT_TIMEOUT;
		} else {
			set_state(GATEWAY_STATE_CLOUD_ERROR);
			attr_set_uint32(ATTR_ID_commissioningBusy, false);
		}
	}
}

static void cloud_connecting_handler(void)
{
	if (gsm.cloud_is_connected()) {
		set_state(GATEWAY_STATE_CLOUD_CONNECTED);
		attr_set_uint32(ATTR_ID_commissioningBusy, false);
		gateway_fsm_cloud_connected_callback();
	} else if (timer_expired()) {
		set_state(GATEWAY_STATE_CLOUD_ERROR);
		attr_set_uint32(ATTR_ID_commissioningBusy, false);
	}
}

static void cloud_connected_handler(void)
{
	if (!gsm.network_is_connected()) {
		set_state(GATEWAY_STATE_NETWORK_DISCONNECTED);
	} else if (!gsm.cloud_is_connected()) {
		set_state(GATEWAY_STATE_CLOUD_DISCONNECTED);
	} else if (gateway_fsm_fota_request() || gsm.cloud_disconnect_request) {
		set_state(GATEWAY_STATE_CLOUD_REQUEST_DISCONNECT);
	}
}

static void disconnected_handler(void)
{
	gsm.timer = get_reconnect_cloud_delay();

	if (gateway_fsm_fota_request()) {
		set_state(GATEWAY_STATE_FOTA_BUSY);
	} else if (gsm.decommission_request) {
		set_state(GATEWAY_STATE_DECOMMISSION);
	} else if (!gsm.network_is_connected()) {
		set_state(GATEWAY_STATE_WAIT_FOR_NETWORK);
	} else {
		set_state(GATEWAY_STATE_WAIT_FOR_COMMISSION);
	}
}

static void fota_handler(void)
{
	if (!gateway_fsm_fota_request()) {
		set_state(GATEWAY_STATE_WAIT_FOR_NETWORK);
	}
}

static void decommission_handler(void)
{
	gsm.decommission_request = false;
	gsm.server_resolved = false;
	gsm.cert_unload();
#ifdef CONFIG_BLUEGRASS
	/* The decomissioning process on the mobile app deletes the shadow */
	bluegrass_init_shadow_request();
#endif
	set_state(GATEWAY_STATE_WAIT_FOR_NETWORK);
}

static uint32_t get_modem_init_delay(void)
{
#ifdef CONFIG_MODEM_HL7800_BOOT_DELAY
	return attr_get_uint32(ATTR_ID_joinDelay, 0);
#else
	return 0;
#endif
}

static uint32_t get_join_network_delay(void)
{
#ifdef CONFIG_MODEM_HL7800_BOOT_IN_AIRPLANE_MODE
	return attr_get_uint32(ATTR_ID_joinDelay, 0);
#else
	return 0;
#endif
}

static uint32_t get_join_cloud_delay(void)
{
#ifdef CONFIG_MODEM_HL7800_BOOT_NORMAL
	return attr_get_uint32(ATTR_ID_joinDelay, 0);
#else
	return 0;
#endif
}

static uint32_t get_reconnect_cloud_delay(void)
{
	uint32_t delay = 0;

	if (attr_get_uint32(ATTR_ID_delayCloudReconnect, 0)) {
		delay = attr_get_uint32(ATTR_ID_joinDelay, 0);
	}

	return delay;
}

#if defined(CONFIG_LWM2M) || defined(CONFIG_NET_L2_ETHERNET)
static int unused_function(void)
{
	return 0;
}
#endif

#if defined(CONFIG_LWM2M)
static bool status_true(void)
{
	return true;
}
#endif

/******************************************************************************/
/* Weak Defaults                                                              */
/******************************************************************************/
__weak bool gateway_fsm_fota_request(void)
{
	return false;
}

__weak int gateway_fsm_network_error_callback(void)
{
	return 0;
}

__weak void gateway_fsm_modem_init_complete_callback(void)
{
	return;
}

__weak void gateway_fsm_network_init_complete_callback(void)
{
	return;
}

__weak void gateway_fsm_network_connected_callback(void)
{
	return;
}

__weak void gateway_fsm_network_disconnected_callback(void)
{
	return;
}

__weak void gateway_fsm_cloud_connected_callback(void)
{
	return;
}

__weak void gateway_fsm_cloud_disconnected_callback(void)
{
	return;
}
