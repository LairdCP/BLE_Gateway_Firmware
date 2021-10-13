/**
 * @file lte.h
 * @brief Abstraction layer between hl7800/network and application.
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LTE_H__
#define __LTE_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Init LTE modem and update modem information
 *
 * @retval 0 on success, lte_init_status enum on failure.
 */
int lte_init(void);

/**
 * @brief Init network
 *
 * @retval 0 on success, lte_init_status enum on failure.
 */
int lte_network_init(void);

/**
 * @brief Accessor
 */
bool lte_ready(void);

/**
 * @brief Get the IP address of the LTE network interface
 *
 * @param get_ipv6 true to get the IPv6 address if available
 * @param ip_addr point to buffer to hold the IP address string
 * @param ip_addr_len length of the address string buffer
 * @return int 0 on success
 */
int lte_get_ip_address(bool get_ipv6, char *ip_addr, int ip_addr_len);

#ifdef __cplusplus
}
#endif

#endif /* __LTE_H__ */
