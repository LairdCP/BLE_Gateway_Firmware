/**
 * @file dns.h
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __DNS_H__
#define __DNS_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <net/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Wraps getaddrinfo with DNS_RETRIES counter and print statements.
 */
int dns_resolve_server_addr(char *endpoint, char *port, struct addrinfo *hints,
			    struct addrinfo **result);

#ifdef __cplusplus
}
#endif

#endif /* __DNS_H__ */
