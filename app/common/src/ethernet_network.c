/**
 * @file ethernet_network.c
 * @brief Ethernet network management
 *
 * Copyright (c) 2017 ARM Ltd.
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2018 Vincent van der Locht
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
#include <stdio.h>
#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_mgmt.h>
#include <net/socket.h>
#include <net/dns_resolve.h>
#include <net/ethernet.h>
#include "net_private.h"
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

#define ETHERNET_DNS_MAX_STR_LEN 16
#define ETHERNET_MAX_DNS_ADDRESSES 1
#define ETHERNET_NETWORK_UNSET_IP "0.0.0.0"

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void iface_dns_added_evt_handler(struct net_mgmt_event_callback *cb,
				    uint32_t mgmt_event, struct net_if *iface);
static void iface_up_evt_handler(struct net_mgmt_event_callback *cb,
				 uint32_t mgmt_event, struct net_if *iface);
static void iface_down_evt_handler(struct net_mgmt_event_callback *cb,
				   uint32_t mgmt_event, struct net_if *iface);
static void set_ip_config(struct net_if *iface);
static void reset_iface_details(void);
static void setup_iface_events(void);
#if defined(CONFIG_NET_DHCPV4)
static void set_ip_dhcp_config(struct net_if *iface);
static void iface_dhcp_bound_evt_handler(struct net_mgmt_event_callback *cb,
					 uint32_t mgmt_event, struct net_if *iface);
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct net_if *iface;
static struct net_if_config *cfg;
static struct dns_resolve_context *dns;
static bool connected = false;
static bool initialised = false;
static bool networkSetup = false;

static struct mgmt_events iface_events[] = {
	{ .event = NET_EVENT_DNS_SERVER_ADD,
	  .handler = iface_dns_added_evt_handler },
	{ .event = NET_EVENT_IF_UP,
	  .handler = iface_up_evt_handler },
	{ .event = NET_EVENT_IF_DOWN,
	  .handler = iface_down_evt_handler },
#if defined(CONFIG_NET_DHCPV4)
	{ .event = NET_EVENT_IPV4_CMD_DHCP_BOUND,
	  .handler = iface_dhcp_bound_evt_handler },
#endif
	{ 0 } /* The for loop below requires this extra location. */
};

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int ethernet_network_init(void)
{
	int rc = ETHERNET_INIT_ERROR_NONE;

	if (!initialised) {
		initialised = true;

		setup_iface_events();
		reset_iface_details();

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

	/* Set ethernet MAC address and IPv4 operating type */
	attr_set_byte_array(ATTR_ID_ethernetMAC, net_if_get_link_addr(iface)->addr, net_if_get_link_addr(iface)->len);
	attr_set_uint32(ATTR_ID_ethernetType, (uint32_t)ETHERNET_TYPE_IPV4);

	/* Perform initial cable connected or disconnected check */
	if (net_if_is_up(iface)) {
		iface_up_evt_handler(NULL, NET_EVENT_IF_UP, iface);
	} else {
		iface_down_evt_handler(NULL, NET_EVENT_IF_DOWN, iface);
	}

	if (attr_get_uint32(ATTR_ID_ethernetMode, (uint32_t)ETHERNET_MODE_STATIC) == ETHERNET_MODE_DHCP) {
		/* Start DHCP */
		ETHERNET_LOG_DBG("Starting DHCP for network");
		net_dhcpv4_start(iface);
	}

exit:
	attr_set_signed32(ATTR_ID_ethernetInitError, rc);
	return rc;
}

bool ethernet_network_ready(void)
{
	struct sockaddr_in *dnsAddr;

	if (iface != NULL && cfg != NULL && &dns->servers[0] != NULL) {
		dnsAddr = net_sin(&dns->servers[0].dns_server);

		return net_if_is_up(iface) && cfg->ip.ipv4 &&
#if defined(CONFIG_NET_DHCPV4)
		       (attr_get_uint32(ATTR_ID_ethernetMode, (uint32_t)ETHERNET_MODE_STATIC) == ETHERNET_MODE_STATIC ||
		       (attr_get_uint32(ATTR_ID_ethernetMode, (uint32_t)ETHERNET_MODE_STATIC) == ETHERNET_MODE_DHCP &&
		       iface->config.dhcpv4.state == NET_DHCPV4_BOUND)) &&
#endif
		       !net_ipv4_is_addr_unspecified(&dnsAddr->sin_addr);
	}

	return false;
}

bool ethernet_network_connected(void)
{
	if (connected) {
		return ethernet_network_ready();
	} else {
		return false;
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void set_ip_config(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	const struct ethernet_api *api = dev->api;
	const struct dns_resolve_context *ctx = dns_resolve_get_default();
	struct net_if_ipv4 *ipv4;
	struct net_if_addr *unicast;
	struct ethernet_config config;
	uint8_t i = 0;
	uint8_t count = 0;
	uint8_t netmaskLength = 0;
	uint32_t tmpNetmask;
	static char ethDNS[ETHERNET_DNS_MAX_STR_LEN + 1];

	/* Update persistent shadow with ethernet network details */
	ipv4 = iface->config.ip.ipv4;
	unicast = &ipv4->unicast[0];

	if (unicast->is_used) {
		tmpNetmask = ipv4->netmask.s_addr;

		/* Count number of set bits for netmask length */
		while (tmpNetmask & 0x1) {
			++netmaskLength;
			tmpNetmask = tmpNetmask >> 1;
		}

		/* Format DNS servers into JSON string */
		ethDNS[0] = 0;
		while (i < (CONFIG_DNS_RESOLVER_MAX_SERVERS + DNS_MAX_MCAST_SERVERS)) {
			if (ctx->servers[i].dns_server.sa_family == AF_INET) {
				snprintf(&ethDNS[strlen(ethDNS)], sizeof(ethDNS)-strlen(ethDNS), "%s,", net_sprint_ipv4_addr(&net_sin(&ctx->servers[i].dns_server)->sin_addr));
				++count;

				if (count == ETHERNET_MAX_DNS_ADDRESSES) {
					break;
				}
			}
			++i;
		}

		/* Remove final comma from DNS server list if present */
		if (count > 0 && ethDNS[strlen(ethDNS)-1] == ',') {
			ethDNS[strlen(ethDNS)-1] = 0;
		}

		if (api->get_config != NULL) {
			/* Query link speed and duplex from ethernet driver */
			if (api->get_config(dev, ETHERNET_CONFIG_TYPE_LINK, &config) == 0) {
				attr_set_uint32(ATTR_ID_ethernetSpeed, (uint32_t)(config.l.link_100bt ? ETHERNET_SPEED_100MBPS : (config.l.link_10bt ? ETHERNET_SPEED_10MBPS : ETHERNET_SPEED_UNKNOWN)));
			}

			if (api->get_config(dev, ETHERNET_CONFIG_TYPE_DUPLEX, &config) == 0) {
				attr_set_uint32(ATTR_ID_ethernetDuplex, (uint32_t)(config.full_duplex ? ETHERNET_DUPLEX_FULL : ETHERNET_DUPLEX_HALF));
			}
		}

		attr_set_string(ATTR_ID_ethernetIPAddress, net_sprint_ipv4_addr(&unicast->address.in_addr), strlen(net_sprint_ipv4_addr(&unicast->address.in_addr)));
		attr_set_uint32(ATTR_ID_ethernetNetmaskLength, (uint32_t)netmaskLength);
		attr_set_string(ATTR_ID_ethernetGateway, net_sprint_ipv4_addr(&ipv4->gw), strlen(net_sprint_ipv4_addr(&ipv4->gw)));
		attr_set_string(ATTR_ID_ethernetDNS, ethDNS, strlen(ethDNS));
	} else {
		/* No IP currently set, use empty values */
		attr_set_string(ATTR_ID_ethernetIPAddress, ETHERNET_NETWORK_UNSET_IP, (sizeof(ETHERNET_NETWORK_UNSET_IP) - 1));
		attr_set_uint32(ATTR_ID_ethernetNetmaskLength, 0);
		attr_set_string(ATTR_ID_ethernetGateway, ETHERNET_NETWORK_UNSET_IP, (sizeof(ETHERNET_NETWORK_UNSET_IP) - 1));
		attr_set_string(ATTR_ID_ethernetDNS, ETHERNET_NETWORK_UNSET_IP, (sizeof(ETHERNET_NETWORK_UNSET_IP) - 1));
	}

#if defined(CONFIG_NET_DHCPV4)
	set_ip_dhcp_config(iface);
#endif
}

#if defined(CONFIG_NET_DHCPV4)
static void set_ip_dhcp_config(struct net_if *iface)
{
	attr_set_uint32(ATTR_ID_ethernetDHCPLeaseTime, (uint32_t)iface->config.dhcpv4.lease_time);
	attr_set_uint32(ATTR_ID_ethernetDHCPRenewTime, (uint32_t)iface->config.dhcpv4.renewal_time);
	attr_set_uint32(ATTR_ID_ethernetDHCPState, (uint32_t)iface->config.dhcpv4.state);
	attr_set_uint32(ATTR_ID_ethernetDHCPAttempts, (uint32_t)iface->config.dhcpv4.attempts);
}
#endif

static void iface_dns_added_evt_handler(struct net_mgmt_event_callback *cb,
					uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_DNS_SERVER_ADD) {
		return;
	}

	ETHERNET_LOG_DBG("Ethernet DNS added");
	lcz_led_turn_on(NETWORK_LED);
	ethernet_network_event(ETHERNET_EVT_READY);
	connected = true;

	set_ip_config(iface);
}

static void reset_iface_details(void)
{
	attr_set_uint32(ATTR_ID_ethernetSpeed, (uint32_t)ETHERNET_SPEED_UNKNOWN);
	attr_set_uint32(ATTR_ID_ethernetDuplex, (uint32_t)ETHERNET_DUPLEX_UNKNOWN);
	attr_set_string(ATTR_ID_ethernetIPAddress, ETHERNET_NETWORK_UNSET_IP, (sizeof(ETHERNET_NETWORK_UNSET_IP) - 1));
	attr_set_uint32(ATTR_ID_ethernetNetmaskLength, 0);
	attr_set_string(ATTR_ID_ethernetGateway, ETHERNET_NETWORK_UNSET_IP, (sizeof(ETHERNET_NETWORK_UNSET_IP) - 1));
	attr_set_string(ATTR_ID_ethernetDNS, ETHERNET_NETWORK_UNSET_IP, (sizeof(ETHERNET_NETWORK_UNSET_IP) - 1));
#if defined(CONFIG_NET_DHCPV4)
	attr_set_uint32(ATTR_ID_ethernetDHCPLeaseTime, 0);
	attr_set_uint32(ATTR_ID_ethernetDHCPRenewTime, 0);
	attr_set_uint32(ATTR_ID_ethernetDHCPState, 0);
	attr_set_uint32(ATTR_ID_ethernetDHCPAttempts, 0);
#endif
}

static void iface_up_evt_handler(struct net_mgmt_event_callback *cb,
				 uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IF_UP) {
		return;
	}

	ETHERNET_LOG_DBG("Ethernet cable detected");
	attr_set_uint32(ATTR_ID_ethernetCableDetected, (uint32_t)true);
	ethernet_network_event(ETHERNET_EVT_CABLE_DETECTED);

#if defined(CONFIG_NET_DHCPV4)
	if (networkSetup == false) {
		if (attr_get_uint32(ATTR_ID_ethernetMode, (uint32_t)ETHERNET_MODE_STATIC) == ETHERNET_MODE_STATIC) {
#endif
			/* Setup static configuration */
			ETHERNET_LOG_DBG("Setting static network config");

			int rc, i;
			struct in_addr ipAddress, ipNetmask, ipGateway;
			struct dns_resolve_context *ctx;
			struct sockaddr_in dns;
			const struct sockaddr *dns_servers[] = {
				(struct sockaddr *)&dns, NULL
			};

			(void)memset(&dns, 0, sizeof(dns));

			rc = net_addr_pton(AF_INET, attr_get_quasi_static(ATTR_ID_ethernetStaticDNS), &dns.sin_addr.s4_addr);
			if (rc) {
				/* Invalid DNS */
				ETHERNET_LOG_ERR("Invalid ethernet DNS (%d)", rc);
				goto finished;
			}

			rc = net_addr_pton(AF_INET, attr_get_quasi_static(ATTR_ID_ethernetStaticIPAddress), &ipAddress);
			if (rc) {
				/* Invalid IP address */
				ETHERNET_LOG_ERR("Invalid ethernet IP address (%d)", rc);
				goto finished;
			}

			/* Convert subnet mask length to subnet mask value */
			ipNetmask.s4_addr32[0] = 0;
			i = 0;
			rc = (int)attr_get_uint32(ATTR_ID_ethernetStaticNetmaskLength, 0);
			while (rc > 0) {
				ipNetmask.s4_addr32[0] = ipNetmask.s4_addr32[0] << 1;
				ipNetmask.s4_addr32[0] |= 0b1;
				--rc;
			}

			rc = net_addr_pton(AF_INET, attr_get_quasi_static(ATTR_ID_ethernetStaticGateway), &ipGateway);
			if (rc) {
				/* Invalid gateway */
				ETHERNET_LOG_ERR("Invalid ethernet gateway (%d)", rc);
				goto finished;
			}

			/* Add the IP details to the interface as a manual address type with no expiration time */
			net_if_ipv4_addr_add(iface, &ipAddress, NET_ADDR_MANUAL, 0);
			net_if_ipv4_set_netmask(iface, &ipNetmask);
			net_if_ipv4_set_gw(iface, &ipGateway);

			/* DNS code taken from zephyr/subsys/net/ip/dhcpv4.c */
			ctx = dns_resolve_get_default();
			for (i = 0; i < CONFIG_DNS_NUM_CONCUR_QUERIES; i++) {
				if (!ctx->queries[i].cb) {
					continue;
				}

				dns_resolve_cancel(ctx, ctx->queries[i].id);
			}

			dns_resolve_close(ctx);

			dns.sin_family = AF_INET;
			rc = dns_resolve_init(ctx, NULL, dns_servers);
			if (rc < 0) {
				ETHERNET_LOG_ERR("Failed to add static ethernet DNS (%d)", rc);
				goto finished;
			}
		}

		networkSetup = true;

#if defined(CONFIG_NET_DHCPV4)
	}
#endif

finished:
	set_ip_config(iface);
}

static void iface_down_evt_handler(struct net_mgmt_event_callback *cb,
				   uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IF_DOWN) {
		return;
	}

	ETHERNET_LOG_DBG("Ethernet is down");
	attr_set_uint32(ATTR_ID_ethernetCableDetected, (uint32_t)false);
	lcz_led_turn_off(NETWORK_LED);
	ethernet_network_event(ETHERNET_EVT_DISCONNECTED);
	connected = false;

	/* Reset IP details to empty */
	reset_iface_details();
}

#if defined(CONFIG_NET_DHCPV4)
static void iface_dhcp_bound_evt_handler(struct net_mgmt_event_callback *cb,
					 uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_CMD_DHCP_BOUND) {
		return;
	}

	ETHERNET_LOG_DBG("Ethernet DHCP bound");

	set_ip_config(iface);
}
#endif

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

__weak void ethernet_network_event(enum ethernet_network_event event)
{
	ARG_UNUSED(event);
}
