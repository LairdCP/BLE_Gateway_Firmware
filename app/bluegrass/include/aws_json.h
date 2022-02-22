/**
 * @file aws_json.h
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __AWS_JSON_H__
#define __AWS_JSON_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>
#include <data/json.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define SHADOW_STATE_NULL "{\"state\":null}"
#define SHADOW_REPORTED_START "{\"state\":{\"reported\":{"
#define SHADOW_REPORTED_END "}}}"

#define SHADOW_TEMPERATURE "\"temperature\":"
#define SHADOW_HUMIDITY "\"humidity\":"
#define SHADOW_PRESSURE "\"pressure\":"
#define SHADOW_RADIO_RSSI "\"radio_rssi\":"
#define SHADOW_RADIO_SINR "\"radio_sinr\":"
#define SHADOW_MG100_TEMP "\"tempC\":"
#define SHADOW_MG100_BATT_LEVEL "\"batteryLevel\":"
#define SHADOW_MG100_BATT_VOLT "\"batteryVoltageMv\":"
#define SHADOW_MG100_PWR_STATE "\"powerState\":"
#define SHADOW_MG100_BATT_LOW "\"batteryLowThreshold\":"
#define SHADOW_MG100_BATT_0 "\"battery0\":"
#define SHADOW_MG100_BATT_1 "\"battery1\":"
#define SHADOW_MG100_BATT_2 "\"battery2\":"
#define SHADOW_MG100_BATT_3 "\"battery3\":"
#define SHADOW_MG100_BATT_4 "\"battery4\":"
#define SHADOW_MG100_BATT_GOOD "\"batteryGood\":"
#define SHADOW_MG100_BATT_BAD "\"batteryBadThreshold\":"
#define SHADOW_MG100_ODR "\"odr\":"
#define SHADOW_MG100_SCALE "\"scale\":"
#define SHADOW_MG100_ACT_THS "\"activationThreshold\":"
#define SHADOW_MG100_MOVEMENT "\"movement\":"
#define SHADOW_MG100_MAX_LOG_SIZE "\"maxLogSizeMB\":"
#define SHADOW_MG100_SDCARD_FREE "\"sdCardFreeMB\":"
#define SHADOW_MG100_CURR_LOG_SIZE "\"logSizeMB\":"
#define SHADOW_FAUX_START "\"faux\":\""
#define SHADOW_FAUX_END "\""

#ifdef CONFIG_NET_L2_ETHERNET
struct shadow_persistent_values_ethernet {
	const char *MAC;
	uint32_t type;
	uint32_t mode;
	uint32_t speed;
	uint32_t duplex;
	const char *IPAddress;
	uint32_t netmaskLength;
	const char *gateway;
	const char *DNS;
#ifdef CONFIG_NET_DHCPV4
	uint32_t DHCPLeaseTime;
	uint32_t DHCPRenewTime;
	uint32_t DHCPState;
	uint32_t DHCPAttempts;
#endif /* CONFIG_NET_DHCPV4 */
};
#endif /* CONFIG_NET_L2_ETHERNET */

struct shadow_persistent_values {
	const char *firmware_version;
	const char *os_version;
#ifdef CONFIG_MODEM_HL7800
	const char *radio_version;
	const char *IMEI;
	const char *ICCID;
	const char *radio_sn;
#endif /* CONFIG_MODEM_HL7800 */
#ifdef CONFIG_NET_L2_ETHERNET
	struct shadow_persistent_values_ethernet ethernet;
#endif /* CONFIG_NET_L2_ETHERNET */
	bool codedPhySupported;
	bool httpFotaEnabled;
};

struct shadow_state_reported {
	struct shadow_persistent_values reported;
};

struct shadow_reported_struct {
	struct shadow_state_reported state;
};

#ifdef CONFIG_NET_L2_ETHERNET
static const struct json_obj_descr shadow_persistent_values_ethernet_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, MAC,
			    JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, type,
			    JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, mode,
			    JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, speed,
			    JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, duplex,
			    JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, IPAddress,
			    JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, netmaskLength,
			    JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, gateway,
			    JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, DNS,
			    JSON_TOK_STRING),
#ifdef CONFIG_NET_DHCPV4
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, DHCPLeaseTime,
			    JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, DHCPRenewTime,
			    JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, DHCPState,
			    JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values_ethernet, DHCPAttempts,
			    JSON_TOK_NUMBER),
#endif /* CONFIG_NET_DHCPV4 */
};
#endif /* CONFIG_NET_L2_ETHERNET */

static const struct json_obj_descr shadow_persistent_values_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, firmware_version,
			    JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, os_version,
			    JSON_TOK_STRING),
#ifdef CONFIG_MODEM_HL7800
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, radio_version,
			    JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, IMEI,
			    JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, ICCID,
			    JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, radio_sn,
			    JSON_TOK_STRING),
#endif /* CONFIG_MODEM_HL7800 */
#ifdef CONFIG_NET_L2_ETHERNET
	JSON_OBJ_DESCR_OBJECT(struct shadow_persistent_values, ethernet,
			      shadow_persistent_values_ethernet_descr),
#endif /* CONFIG_NET_L2_ETHERNET */
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, codedPhySupported,
			    JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, httpFotaEnabled,
			    JSON_TOK_TRUE),
};

static const struct json_obj_descr shadow_state_reported_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct shadow_state_reported, reported,
			      shadow_persistent_values_descr),
};

static const struct json_obj_descr shadow_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct shadow_reported_struct, state,
			      shadow_state_reported_descr),
};

#ifdef __cplusplus
}
#endif

#endif /* __AWS_JSON_H__ */
