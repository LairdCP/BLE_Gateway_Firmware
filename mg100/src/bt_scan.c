/**
 * @file bt_scan.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(bt_scan);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/bluetooth.h>

#include "FrameworkIncludes.h"
#include "bt_scan.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
/* Sensor events are not received properly unless filter duplicates is OFF
 * Bug 16484: Zephyr 2.x - Retest filter duplicates */
#define BT_LE_SCAN_CONFIG1                                                     \
	BT_LE_SCAN_PARAM(BT_HCI_LE_SCAN_ACTIVE,                                \
			 BT_HCI_LE_SCAN_FILTER_DUP_DISABLE,                    \
			 BT_GAP_SCAN_FAST_INTERVAL, BT_GAP_SCAN_FAST_WINDOW)

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static atomic_t scanning = ATOMIC_INIT(0);

K_SEM_DEFINE(stop_requests, 0, CONFIG_BT_MAX_CONN);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void adv_handler(const bt_addr_le_t *addr, s8_t rssi, u8_t type,
			struct net_buf_simple *ad);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void bt_scan_start(void)
{
	if (k_sem_count_get(&stop_requests) != 0) {
		return;
	}

	if (atomic_cas(&scanning, 0, 1)) {
		int err = bt_le_scan_start(BT_LE_SCAN_CONFIG1, adv_handler);
		LOG_DBG("%d", err);
	}
}

void bt_scan_stop(void)
{
	k_sem_give(&stop_requests);
	if (atomic_cas(&scanning, 1, 0)) {
		int err = bt_le_scan_stop();
		LOG_DBG("%d", err);
	}
}

void bt_scan_resume(void)
{
	k_sem_take(&stop_requests, K_FOREVER);
	bt_scan_start();
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
/* This callback is triggered after receiving BLE adverts */
static void adv_handler(const bt_addr_le_t *addr, s8_t rssi, u8_t type,
			struct net_buf_simple *ad)
{
	/* Send a message so we can process ads in Sensor Task context
	 * (so that the BLE RX task isn't blocked). */
	AdvMsg_t *pMsg = BufferPool_Take(sizeof(AdvMsg_t));
	if (pMsg == NULL) {
		return;
	}

	pMsg->header.msgCode = FMC_ADV;
	pMsg->header.rxId = FWK_ID_SENSOR_TASK;

	pMsg->rssi = rssi;
	pMsg->type = type;
	pMsg->ad.len = ad->len;
	memcpy(&pMsg->addr, addr, sizeof(bt_addr_le_t));
	memcpy(pMsg->ad.data, ad->data, MIN(MAX_AD_SIZE, ad->len));
	/* If the sensor task queue is full, then delete the message
	 * because the system is too busy to process it. */
	FRAMEWORK_MSG_SEND(pMsg);
}
