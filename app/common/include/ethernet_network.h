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
enum ethernet_network_event { ETHERNET_EVT_READY, ETHERNET_EVT_CABLE_DETECTED, ETHERNET_EVT_DISCONNECTED };

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

#ifdef CONFIG_SNTP
/**
 * @brief Force an SNTP time update
 *
 * @retval false if failed, true if succesfully submitted
 */
bool sntp_update_time(void);
#endif

/**
 * @brief Get the IP address of the ethernet network interface
 *
 * @param ip_addr point to buffer to hold the IP address string
 * @param ip_addr_len length of the address string buffer
 * @retval 0 on success, error code if failed
 */
int ethernet_get_ip_address(char *ip_addr, int ip_addr_len);

/**
 * @brief Callback from ethernet driver that can be implemented in application.
 */
void ethernet_network_event(enum ethernet_network_event event);

#ifdef __cplusplus
}
#endif

#endif /* __ETHERNET_NETWORK_H__ */
