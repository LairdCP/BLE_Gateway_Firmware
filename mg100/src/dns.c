/**
 * @file dns.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(dns);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>

#include "dns.h"

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int dns_resolve_server_addr(char *endpoint, char *port, struct addrinfo *hints,
			    struct addrinfo **result)
{
	int rc = -EPERM;
#ifdef CONFIG_DNS_RESOLVER
	int dns_retries = CONFIG_DNS_RETRIES;
	do {
		LOG_DBG("Get server address");
		rc = getaddrinfo(endpoint, port, hints, result);
		if (rc != 0) {
			LOG_ERR("Get server addr (%d)", rc);
			k_sleep(K_SECONDS(CONFIG_DNS_RETRY_DELAY_SECONDS));
		}
		dns_retries--;
	} while (rc != 0 && dns_retries != 0);

	if (rc != 0) {
		LOG_ERR("Unable to resolve '%s'", endpoint);
	}
#endif
	return rc;
}
