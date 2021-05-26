/**
 * @file ethernet_network.c
 * @brief Ethernet network management
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(ethernet_network, CONFIG_ETHERNET_LOG_LEVEL);

#define ETHERNET_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define ETHERNET_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define ETHERNET_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define ETHERNET_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_mgmt.h>
#include <net/socket.h>

#include "ethernet_network.h"
#include "led_configuration.h"
#include "attr.h"
#include "gateway_common.h"

#ifdef CONFIG_BLUEGRASS
#include "bluegrass.h"
#endif

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/

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

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct net_if *iface;
static struct net_if_config *cfg;
static struct dns_resolve_context *dns;
static bool connected;
static bool wasConnected;
static bool initialized;

static struct mgmt_events iface_events[] = {
	{ .event = NET_EVENT_DNS_SERVER_ADD,
	  .handler = iface_ready_evt_handler },
	{ .event = NET_EVENT_IF_DOWN, .handler = iface_down_evt_handler },
	{ 0 } /* The for loop below requires this extra location. */
};

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int ethernet_init(void)
{
	int rc = ETHERNET_INIT_ERROR_NONE;

	if (!initialized) {
		initialized = true;
		setup_iface_events();

#ifdef CONFIG_BLUEGRASS
		/* Generate Bluegrass topic IDs */
		bluegrass_init_shadow_request();
#endif
	}

	iface = net_if_get_default();
	if (!iface) {
		ETHERNET_LOG_ERR("Could not get iface");
		rc = ETHERNET_INIT_ERROR_NO_IFACE;
		goto exit;
	}

	net_dhcpv4_start(iface);

	cfg = net_if_get_config(iface);
	if (!cfg) {
		ETHERNET_LOG_ERR("Could not get iface config");
		rc = ETHERNET_INIT_ERROR_IFACE_CFG;
		goto exit;
	}

	dns = dns_resolve_get_default();
	if (!dns) {
		ETHERNET_LOG_ERR("Could not get DNS context");
		rc = ETHERNET_INIT_ERROR_DNS_CFG;
		goto exit;
	}

exit:
	attr_set_signed32(ATTR_ID_ethernetInitError, rc);
	return rc;
}

bool ethernet_ready(void)
{
	struct sockaddr_in *dnsAddr;

	if (iface != NULL && cfg != NULL && &dns->servers[0] != NULL) {
		dnsAddr = net_sin(&dns->servers[0].dns_server);
		return net_if_is_up(iface) && cfg->ip.ipv4 &&
		       !net_ipv4_is_addr_unspecified(&dnsAddr->sin_addr);
	}

	return false;
}

bool ethernet_connected(void)
{
	/* On the first connection, wait for the event. */
	if (connected) {
		return true;
	} else if (wasConnected) {
		return ethernet_ready();
	} else {
		return false;
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

	ETHERNET_LOG_DBG("Ethernet is ready!");
	lcz_led_turn_on(NETWORK_LED);
	ethernet_event(ETHERNET_EVT_READY);
	connected = true;
	wasConnected = true;
}

static void iface_down_evt_handler(struct net_mgmt_event_callback *cb,
				   uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IF_DOWN) {
		return;
	}

	ETHERNET_LOG_DBG("Ethernet is down");
	lcz_led_turn_off(NETWORK_LED);
	ethernet_event(ETHERNET_EVT_DISCONNECTED);
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

__weak void ethernet_event(enum ethernet_event event)
{
	ARG_UNUSED(event);
}
