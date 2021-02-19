/**
 * @file lte.h
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LTE_H__
#define __LTE_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
enum lte_errors {
	LTE_ERR_NONE = 0,
	LTE_ERR_NO_IFACE = -1,
	LTE_ERR_IFACE_CFG = -2,
	LTE_ERR_DNS_CFG = -3,
	LTE_ERR_MDM_CTX = -4,
};

enum lte_event { LTE_EVT_READY, LTE_EVT_DISCONNECTED };

struct lte_status {
	const char *radio_version;
	const char *IMEI;
	const char *ICCID;
	const char *serialNumber;
	/* This is actually RSRP (Reference Signals Received Power in dBm)*/
	int rssi;
	/* Signal to Interference plus Noise Ratio (dBm) */
	int sinr;
};

/* Callback function for LTE events */
typedef void (*lte_event_function_t)(enum lte_event event);

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
void lteRegisterEventCallback(lte_event_function_t callback);
int lteInit(void);
bool lteIsReady(void);
struct lte_status *lteGetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __LTE_H__ */
