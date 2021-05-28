/**
 * @file ethernet_network.h
 * @brief Abstraction layer between ethernet network driver and application.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __ETHERNET_NETWORK_H__
#define __ETHERNET_NETWORK_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
enum ethernet_network_event { ETHERNET_EVT_READY, ETHERNET_EVT_DISCONNECTED };
enum ethernet_network_type { ETHERNET_TYPE_IPV4 = 0x1, ETHERNET_TYPE_IPV6 = 0x2 };
enum ethernet_network_mode { ETHERNET_MODE_UNKNOWN = 0x0, ETHERNET_MODE_STATIC = 0x1, ETHERNET_MODE_DHCP = 0x2 };
enum ethernet_network_speed { ETHERNET_SPEED_UNKNOWN = 0x0, ETHERNET_SPEED_10MBPS = 0x1, ETHERNET_SPEED_100MBPS = 0x2, ETHERNET_SPEED_1GBPS = 0x4 };
enum ethernet_network_duplex { ETHERNET_DUPLEX_UNKNOWN = 0x0, ETHERNET_DUPLEX_HALF = 0x1, ETHERNET_DUPLEX_FULL = 0x2 };

/* Callback function for ethernet events */
typedef void (*ethernet_network_event_function_t)(enum ethernet_network_event event);

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Init network
 *
 * @retval 0 on success, ethernet_init_status enum on failure.
 */
int ethernet_network_init(void);

/**
* @brief Accessor
 */
bool ethernet_network_ready(void);

/**
 * @brief Accessor
 */
bool ethernet_network_connected(void);

/**
 * @brief Callback from ethernet driver that can be implemented in application.
 */
void ethernet_network_event(enum ethernet_network_event event);

#ifdef __cplusplus
}
#endif

#endif /* __ETHERNET_NETWORK_H__ */
