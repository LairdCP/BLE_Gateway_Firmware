/**
 * @file lcz_lwm2m_conn_mon.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lwm2m_conn_mon, CONFIG_LCZ_LWM2M_CONN_MON_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <net/lwm2m.h>

#include "lcz_lwm2m_conn_mon.h"
#include "attr.h"
#if defined(CONFIG_MODEM_HL7800)
#include "lte.h"
#else
#include "ethernet_network.h"
#endif

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define LTE_FDD_BEARER 6U
#define NB_IOT_BEARER 7U
#define ETHERNET_BEARER 41U

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
#if defined(CONFIG_MODEM_HL7800)
static uint8_t network_bearers[2] = { LTE_FDD_BEARER, NB_IOT_BEARER };
#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
static uint8_t network_bearers[1] = { ETHERNET_BEARER };
#else
#error "Need to define network bearers"
#endif

static bool init = true;
static char ipv4_addr[NET_IPV4_ADDR_LEN];
#if defined(CONFIG_MODEM_HL7800)
static char ipv6_addr[NET_IPV6_ADDR_LEN];
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/

int lcz_lwm2m_conn_mon_update_values(void)
{
	int rc = 0;
	char path[sizeof("12345/12345/12345/12345")];
#if defined(CONFIG_MODEM_HL7800)
	char *temp;
#endif

#if defined(CONFIG_MODEM_HL7800)
	switch (*(uint8_t *)attr_get_quasi_static(ATTR_ID_lteRat)) {
	case LTE_RAT_CAT_M1:
		lwm2m_engine_set_u8("4/0/0", LTE_FDD_BEARER);
		break;
	case LTE_RAT_CAT_NB1:
		lwm2m_engine_set_u8("4/0/0", NB_IOT_BEARER);
		break;
	default:
		LOG_ERR("LTE bearer unknown");
		rc = -EIO;
		goto done;
	}
#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
	lwm2m_engine_set_u8("4/0/0", ETHERNET_BEARER);
#endif

	if (init) {
		init = false;

		for (int i = 0; i < sizeof(network_bearers); i++) {
			snprintk(path, sizeof(path), "4/0/1/%d", i);
			lwm2m_engine_create_res_inst(path);
			lwm2m_engine_set_res_data(path, &network_bearers[i],
						  sizeof(network_bearers[i]),
						  LWM2M_RES_DATA_FLAG_RO);
		}

		lwm2m_engine_create_res_inst("4/0/4/0");
#if defined(CONFIG_MODEM_HL7800)
		lte_get_ip_address(true, ipv6_addr, sizeof(ipv6_addr));
		if (strlen(ipv6_addr) > 0) {
			lwm2m_engine_create_res_inst("4/0/4/1");
		}

		lwm2m_engine_create_res_inst("4/0/7/0");
#endif
		/* delete unused resources created by zephyr */
		lwm2m_engine_delete_res_inst("4/0/8/0");
		lwm2m_engine_delete_res_inst("4/0/9/0");
		lwm2m_engine_delete_res_inst("4/0/10/0");
	}

#if defined(CONFIG_MODEM_HL7800)
	lwm2m_engine_set_s8("4/0/2",
			    *(int8_t *)attr_get_quasi_static(ATTR_ID_lteRsrp));

	lwm2m_engine_set_s8("4/0/3",
			    *(int8_t *)attr_get_quasi_static(ATTR_ID_lteSinr));

	/* interface IP address */
	lte_get_ip_address(false, ipv4_addr, sizeof(ipv4_addr));
	lwm2m_engine_set_res_data("4/0/4/0", ipv4_addr, sizeof(ipv4_addr),
				  LWM2M_RES_DATA_FLAG_RO);
	lte_get_ip_address(true, ipv6_addr, sizeof(ipv6_addr));
	if (strlen(ipv6_addr) > 0) {
		lwm2m_engine_set_res_data("4/0/4/1", ipv6_addr,
					  sizeof(ipv6_addr),
					  LWM2M_RES_DATA_FLAG_RO);
	}
#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
	/* interface IP address */
	ethernet_get_ip_address(ipv4_addr, sizeof(ipv4_addr));
	lwm2m_engine_set_res_data("4/0/4/0", ipv4_addr, sizeof(ipv4_addr),
				  LWM2M_RES_DATA_FLAG_RO);
#else
#error Fill in IP address
#endif

#if defined(CONFIG_MODEM_HL7800)
	/* APN */
	temp = (char *)attr_get_quasi_static(ATTR_ID_apn);
	lwm2m_engine_set_res_data("4/0/7/0", temp, strlen(temp),
				  LWM2M_RES_DATA_FLAG_RO);
done:
#endif
	return rc;
}
