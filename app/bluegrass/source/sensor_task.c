/**
 * @file sensor_task.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(sensor_task);
#define FWK_FNAME "sensor"

#define ST_LOG_DEV(...)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <string.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>

#include "FrameworkIncludes.h"
#include "Bracket.h"
#include "laird_bluetooth.h"
#include "bt_scan.h"
#include "vsp_definitions.h"
#include "qrtc.h"
#include "sensor_cmd.h"
#include "sensor_table.h"
#include "sensor_task.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifndef SENSOR_TASK_PRIORITY
#define SENSOR_TASK_PRIORITY K_PRIO_PREEMPT(1)
#endif

#ifndef SENSOR_TASK_STACK_DEPTH
#define SENSOR_TASK_STACK_DEPTH 4096
#endif

#ifndef SENSOR_TASK_QUEUE_DEPTH
#define SENSOR_TASK_QUEUE_DEPTH 32
#endif

/** Limit the amount of space in the queue that can be used for processing
 * advertisements.
 */
#ifndef SENSOR_TASK_MAX_OUTSTANDING_ADS
#define SENSOR_TASK_MAX_OUTSTANDING_ADS (SENSOR_TASK_QUEUE_DEPTH / 2)
#endif

/** At 1 second there are duplicate requests for shadow information. */
#define SENSOR_TICK_RATE_SECONDS 3

#define ENCRYPTION_TIMEOUT_TICKS K_SECONDS(2)
#define CONNECTION_TIMEOUT_TICKS K_SECONDS(CONFIG_BT_CREATE_CONN_TIMEOUT + 2)

#define FIRST_VALID_HANDLE 0x0001
#define LAST_VALID_HANDLE UINT16_MAX

#define SENSOR_PIN_DEFAULT 123456

typedef struct SensorTask {
	FwkMsgTask_t msgTask;
	struct bt_conn *conn;
	struct bt_gatt_discover_params dp;
	struct bt_gatt_subscribe_params sp;
	struct bt_gatt_exchange_params mp;
	uint16_t writeHandle;
	uint16_t mtu;
	bool connected;
	bool paired;
	bool resetSent;
	bool configComplete;
	BracketObj_t *pBracket;
	SensorCmdMsg_t *pCmdMsg;
	bool awsReady;
	struct k_timer resetTimer;
	struct k_timer sensorTick;
	uint32_t fifoTicks;
	int scanUserId;
	uint32_t configDisconnects;
	uint32_t adsProcessed;
	atomic_t adsOutstanding; /* incremented in BT RX thread context */
	atomic_t adsDropped; /* incremented in BT RX thread context */
} SensorTaskObj_t;

/* A connection is not created unless 1M is disabled. */
#define BT_CONN_CODED_CREATE_CONN                                              \
	BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_CODED | BT_CONN_LE_OPT_NO_1M,   \
				BT_GAP_SCAN_FAST_INTERVAL,                     \
				BT_GAP_SCAN_FAST_INTERVAL)

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static SensorTaskObj_t st;

K_THREAD_STACK_DEFINE(sensorTaskStack, SENSOR_TASK_STACK_DEPTH);

K_MSGQ_DEFINE(sensorTaskQueue, FWK_QUEUE_ENTRY_SIZE, SENSOR_TASK_QUEUE_DEPTH,
	      FWK_QUEUE_ALIGNMENT);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void SensorTaskThread(void *pArg1, void *pArg2, void *pArg3);

static DispatchResult_t AdvertisementMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg);

static DispatchResult_t SensorTickHandler(FwkMsgReceiver_t *pMsgRxer,
					  FwkMsg_t *pMsg);

static DispatchResult_t WhitelistRequestMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg);

static DispatchResult_t ConfigRequestMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg);

static DispatchResult_t ConnectRequestMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg);

static DispatchResult_t StartDiscoveryMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg);

static DispatchResult_t DisconnectMsgHandler(FwkMsgReceiver_t *pMsgRxer,
					     FwkMsg_t *pMsg);

static DispatchResult_t DiscoveryMsgHandler(FwkMsgReceiver_t *pMsgRxer,
					    FwkMsg_t *pMsg);

static DispatchResult_t PeriodicTimerMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg);

static DispatchResult_t ResponseHandler(FwkMsgReceiver_t *pMsgRxer,
					FwkMsg_t *pMsg);

static DispatchResult_t SendResetHandler(FwkMsgReceiver_t *pMsgRxer,
					 FwkMsg_t *pMsg);

static DispatchResult_t AwsConnectionMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg);

static DispatchResult_t AwsDecommissionMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						  FwkMsg_t *pMsg);

static DispatchResult_t SubscriptionAckMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						  FwkMsg_t *pMsg);

static DispatchResult_t SensorShadowInitMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg);

static void RegisterConnectionCallbacks(void);
static int StartDiscovery(void);
static int ExchangeMtu(void);
static int RequestDisconnect(SensorTaskObj_t *pObj, const char *str);

static int Discover(void);
static int Subscribe(void);
static int WriteString(const char *str);
static void CreateAndSendResponseMsg(BracketObj_t *p);
static DispatchResult_t RetryConfigRequest(SensorTaskObj_t *pObj);
static void AckConfigRequest(SensorTaskObj_t *pObj);
static void SendSetEpochCommand(void);

static void ConnectedCallback(struct bt_conn *conn, uint8_t err);
static void DisconnectedCallback(struct bt_conn *conn, uint8_t reason);

static int RegisterSecurityCallbacks(void);
static int EncryptLink(SensorTaskObj_t *pObj);
static void PasskeyDisplayCallback(struct bt_conn *conn, unsigned int passkey);
static void PasskeyEntryCallback(struct bt_conn *conn);
static void PasskeyConfirmCallback(struct bt_conn *conn, unsigned int passkey);
static void SecurityCancelCallback(struct bt_conn *conn);
static void PairingConfirmCallback(struct bt_conn *conn);
static void PairingCompleteCallback(struct bt_conn *conn, bool bonded);
static void PairingFailedCallback(struct bt_conn *conn,
				  enum bt_security_err reason);
static void SecurityChangedCallback(struct bt_conn *conn, bt_security_t level,
				    enum bt_security_err err);

static uint8_t DiscoveryCallback(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 struct bt_gatt_discover_params *params);

static uint8_t NotificationCallback(struct bt_conn *conn,
				    struct bt_gatt_subscribe_params *params,
				    const void *data, uint16_t length);

static void MtuCallback(struct bt_conn *conn, uint8_t err,
			struct bt_gatt_exchange_params *params);

static void SendSensorResetTimerCallbackIsr(struct k_timer *timer_id);
static void SensorTickCallbackIsr(struct k_timer *timer_id);
static void StartSensorTick(SensorTaskObj_t *pObj);

#ifdef CONFIG_SCAN_FOR_BT510
static void SensorTaskAdvHandler(const bt_addr_le_t *addr, int8_t rssi,
				 uint8_t type, struct net_buf_simple *ad);
#endif

/******************************************************************************/
/* Framework Message Dispatcher                                               */
/******************************************************************************/
static FwkMsgHandler_t SensorTaskMsgDispatcher(FwkMsgCode_t MsgCode)
{
	/* clang-format off */
	switch (MsgCode) {
	case FMC_INVALID:                  return Framework_UnknownMsgHandler;
	case FMC_ADV:                      return AdvertisementMsgHandler;
	case FMC_SENSOR_TICK:              return SensorTickHandler;
	case FMC_WHITELIST_REQUEST:        return WhitelistRequestMsgHandler;
	case FMC_CONFIG_REQUEST:           return ConfigRequestMsgHandler;
	case FMC_CONNECT_REQUEST:          return ConnectRequestMsgHandler;
	case FMC_START_DISCOVERY:          return StartDiscoveryMsgHandler;
	case FMC_DISCONNECT:               return DisconnectMsgHandler;
	case FMC_DISCOVERY_COMPLETE:       return DiscoveryMsgHandler;
	case FMC_DISCOVERY_FAILED:         return DiscoveryMsgHandler;
	case FMC_RESPONSE:                 return ResponseHandler;
	case FMC_SEND_RESET:               return SendResetHandler;
	case FMC_PERIODIC:                 return PeriodicTimerMsgHandler;
	case FMC_AWS_CONNECTED:            return AwsConnectionMsgHandler;
	case FMC_AWS_DISCONNECTED:         return AwsConnectionMsgHandler;
	case FMC_SUBSCRIBE_ACK:            return SubscriptionAckMsgHandler;
	case FMC_SENSOR_SHADOW_INIT:       return SensorShadowInitMsgHandler;
	case FMC_AWS_DECOMMISSION:         return AwsDecommissionMsgHandler;
	default:                           return NULL;
	}
	/* clang-format on */
}

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void SensorTask_Initialize(void)
{
	memset(&st, 0, sizeof(SensorTaskObj_t));

	st.msgTask.rxer.id = FWK_ID_SENSOR_TASK;
	st.msgTask.rxer.pQueue = &sensorTaskQueue;
	st.msgTask.rxer.rxBlockTicks = K_FOREVER;
	st.msgTask.rxer.pMsgDispatcher = SensorTaskMsgDispatcher;
	Framework_RegisterTask(&st.msgTask);

	st.msgTask.pTid =
		k_thread_create(&st.msgTask.threadData, sensorTaskStack,
				K_THREAD_STACK_SIZEOF(sensorTaskStack),
				SensorTaskThread, &st, NULL, NULL,
				SENSOR_TASK_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(st.msgTask.pTid, FWK_FNAME);

	st.pBracket =
		Bracket_Initialize(CONFIG_JSON_BRACKET_BUFFER_SIZE,
				   k_malloc(CONFIG_JSON_BRACKET_BUFFER_SIZE));
	st.conn = NULL;
	RegisterConnectionCallbacks();
	RegisterSecurityCallbacks();
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void SensorTaskThread(void *pArg1, void *pArg2, void *pArg3)
{
	SensorTaskObj_t *pObj = (SensorTaskObj_t *)pArg1;

	SensorTable_Initialize();

	k_timer_init(&pObj->resetTimer, SendSensorResetTimerCallbackIsr, NULL);
	k_timer_user_data_set(&pObj->resetTimer, pObj);

	k_timer_init(&pObj->sensorTick, SensorTickCallbackIsr, NULL);
	k_timer_user_data_set(&pObj->sensorTick, pObj);

#ifdef CONFIG_SCAN_FOR_BT510
	bt_scan_register(&pObj->scanUserId, SensorTaskAdvHandler);
	bt_scan_start(pObj->scanUserId);
#endif

	while (true) {
		Framework_MsgReceiver(&pObj->msgTask.rxer);
		uint32_t numUsed =
			k_msgq_num_used_get(pObj->msgTask.rxer.pQueue);
		if (numUsed > SENSOR_TASK_QUEUE_DEPTH / 2) {
			LOG_WRN("Sensor Task filled to %u of %u", numUsed,
				SENSOR_TASK_QUEUE_DEPTH);
		}
	}
}

DispatchResult_t AdvertisementMsgHandler(FwkMsgReceiver_t *pMsgRxer,
					 FwkMsg_t *pMsg)
{
	AdvMsg_t *pAdvMsg = (AdvMsg_t *)pMsg;
	SensorTable_AdvertisementHandler(&pAdvMsg->addr, pAdvMsg->rssi,
					 pAdvMsg->type, &pAdvMsg->ad);

	SensorTaskObj_t *pObj = FWK_TASK_CONTAINER(SensorTaskObj_t);
	pObj->adsProcessed += 1;
	atomic_dec(&pObj->adsOutstanding);
	/* Attempt to limit prints when busy. */
	if (atomic_get(&pObj->adsOutstanding) == 0) {
		if (atomic_get(&pObj->adsDropped) > 0) {
			atomic_val_t dropped = atomic_clear(&pObj->adsDropped);
			LOG_WRN("%u advertisements dropped", dropped);
		}
	}
	return DISPATCH_OK;
}

static DispatchResult_t WhitelistRequestMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsgRxer);
	SensorTable_ProcessWhitelistRequest((SensorWhitelistMsg_t *)pMsg);
	return DISPATCH_OK;
}

static DispatchResult_t SensorTickHandler(FwkMsgReceiver_t *pMsgRxer,
					  FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsg);
	SensorTaskObj_t *pObj = FWK_TASK_CONTAINER(SensorTaskObj_t);
	if (pObj->awsReady) {
		SensorTable_TimeToLiveHandler();
		SensorTable_SubscriptionHandler(); /* sensor shadow  delta */
		SensorTable_GetAcceptedSubscriptionHandler();
		SensorTable_InitShadowHandler();
		StartSensorTick(pObj);
	}
	return DISPATCH_OK;
}

static DispatchResult_t StartDiscoveryMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsg);
	SensorTaskObj_t *pObj = FWK_TASK_CONTAINER(SensorTaskObj_t);
	pObj->connected = true;
	k_timer_stop(&pObj->msgTask.timer);
	if (ExchangeMtu() == BT_SUCCESS) {
		StartDiscovery();
	} else {
		RequestDisconnect(pObj, "Exchange MTU Failed");
	}
	return DISPATCH_OK;
}

static DispatchResult_t DiscoveryMsgHandler(FwkMsgReceiver_t *pMsgRxer,
					    FwkMsg_t *pMsg)
{
	SensorTaskObj_t *pObj = FWK_TASK_CONTAINER(SensorTaskObj_t);
	if (pMsg->header.msgCode == FMC_DISCOVERY_COMPLETE) {
		k_timer_start(&pObj->msgTask.timer, ENCRYPTION_TIMEOUT_TICKS,
			      K_NO_WAIT);
		EncryptLink(pObj);
	} else {
		RequestDisconnect(pObj, "Discovery Failure");
	}
	return DISPATCH_OK;
}

/* Write config data or connection timeout or encryption timeout */
static DispatchResult_t PeriodicTimerMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsg);
	SensorTaskObj_t *pObj = FWK_TASK_CONTAINER(SensorTaskObj_t);
	if (pObj->paired && pObj->connected) {
		WriteString(pObj->pCmdMsg->cmd);
	} else if (!pObj->connected) {
		RequestDisconnect(pObj, "Connection failed to be established");
	} else if (!pObj->paired) {
		RequestDisconnect(pObj, "Encryption failure");
	}
	return DISPATCH_OK;
}

static DispatchResult_t ResponseHandler(FwkMsgReceiver_t *pMsgRxer,
					FwkMsg_t *pMsg)
{
	SensorTaskObj_t *pObj = FWK_TASK_CONTAINER(SensorTaskObj_t);
	FwkBufMsg_t *pRsp = (FwkBufMsg_t *)pMsg;
	bool ok = (strstr(pRsp->buffer, SENSOR_CMD_ACCEPTED_SUB_STR) != NULL);
	if (ok) {
		if (pObj->pCmdMsg->setEpochRequest) {
			pObj->pCmdMsg->setEpochRequest = false;
			SendSetEpochCommand();
		} else if (pObj->pCmdMsg->resetRequest) {
			pObj->pCmdMsg->resetRequest = false;
			/* Don't block this task because it also processes adverts */
			k_timer_start(&pObj->resetTimer,
				      BT510_WRITE_TO_RESET_DELAY_TICKS,
				      K_NO_WAIT);
		} else {
			pObj->configComplete = true;
			if (pObj->pCmdMsg->dumpRequest) {
				RequestDisconnect(pObj,
						  "Config Cycle Complete");
				SensorTable_CreateShadowFromDumpResponse(
					pRsp, pObj->pCmdMsg->addrString);
			} else {
				/* When the first part is complete the sensor table
				 * is acked.  It will then generate a dump request to read
				 * all of the sensor configuration.
				 */
				RequestDisconnect(pObj,
						  "Config Part 1 Complete");
			}
		}
	} else {
		RequestDisconnect(pObj, "Invalid JSON response");
	}
	return DISPATCH_OK;
}

static void SendSetEpochCommand(void)
{
	size_t maxSize = strlen(SENSOR_CMD_SET_EPOCH_FMT_STR) +
			 SENSOR_CMD_MAX_EPOCH_SIZE + 1;
	char *buf = BufferPool_Take(maxSize);
	if (buf != NULL) {
		uint32_t epoch = Qrtc_GetEpoch();
		snprintk(buf, maxSize, SENSOR_CMD_SET_EPOCH_FMT_STR, epoch);
		WriteString(buf);
		LOG_DBG("%u", epoch);
	}
}

static DispatchResult_t SendResetHandler(FwkMsgReceiver_t *pMsgRxer,
					 FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsg);
	SensorTaskObj_t *pObj = FWK_TASK_CONTAINER(SensorTaskObj_t);
	if (pObj->connected) {
		WriteString(SENSOR_CMD_REBOOT);
		pObj->resetSent = true;
	}
	return DISPATCH_OK;
}

static DispatchResult_t AwsConnectionMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg)
{
	SensorTaskObj_t *pObj = FWK_TASK_CONTAINER(SensorTaskObj_t);
	if (pMsg->header.msgCode == FMC_AWS_CONNECTED) {
		pObj->awsReady = true;
		StartSensorTick(pObj);
	} else {
		pObj->awsReady = false;
		SensorTable_DisableGatewayShadowGeneration();
		SensorTable_UnsubscribeAll();
	}
	return DISPATCH_OK;
}

static DispatchResult_t AwsDecommissionMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						  FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsgRxer);
	UNUSED_PARAMETER(pMsg);
	SensorTable_DecomissionHandler();
	return DISPATCH_OK;
}

static void StartSensorTick(SensorTaskObj_t *pObj)
{
	k_timer_start(&pObj->sensorTick, K_SECONDS(SENSOR_TICK_RATE_SECONDS),
		      K_NO_WAIT);
}

static DispatchResult_t SubscriptionAckMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						  FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsgRxer);
	SensorTable_SubscriptionAckHandler((SubscribeMsg_t *)pMsg);
	return DISPATCH_OK;
}

static DispatchResult_t ConfigRequestMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsgRxer);
	return SensorTable_AddConfigRequest((SensorCmdMsg_t *)pMsg);
}

static DispatchResult_t ConnectRequestMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg)
{
	int err;
	SensorTaskObj_t *pObj = FWK_TASK_CONTAINER(SensorTaskObj_t);

	if (pObj->pCmdMsg == NULL && pObj->conn == NULL) { /* not busy */
		bt_scan_stop(pObj->scanUserId);
		Bracket_Reset(pObj->pBracket);
		pObj->pCmdMsg = (SensorCmdMsg_t *)pMsg;
		pObj->connected = false;
		pObj->paired = false;
		pObj->resetSent = false;
		pObj->configComplete = false;
		err = bt_conn_le_create(&pObj->pCmdMsg->addr,
					pObj->pCmdMsg->useCodedPhy ?
						BT_CONN_CODED_CREATE_CONN :
						BT_CONN_LE_CREATE_CONN,
					BT_LE_CONN_PARAM_DEFAULT, &pObj->conn);

		LOG_INF("Connection Request (%u): '%s' (%s) %x-%u",
			pObj->pCmdMsg->attempts,
			log_strdup(pObj->pCmdMsg->name),
			log_strdup(pObj->pCmdMsg->addrString),
			(uint32_t)POINTER_TO_UINT(pObj->conn),
			bt_conn_index(pObj->conn));

		if (err) {
			return RetryConfigRequest(pObj);
		} else {
			/* If a connection is requested on the last ad from the sensor,
			 * then the state machine will get stuck waiting ~15 minutes
			 * for the next ad.  This is because the stack does not
			 * provide a timeout for this condition.
			 * (state == BT_CONN_CONNECT_SCAN)
			 * Bug 16483: Zephyr 2.x - Retest connection timeout fix (stack)
			 */
			k_timer_start(&pObj->msgTask.timer,
				      CONNECTION_TIMEOUT_TICKS, K_NO_WAIT);
			return DISPATCH_DO_NOT_FREE;
		}
	} else {
		return RetryConfigRequest(pObj);
	}
}

static DispatchResult_t DisconnectMsgHandler(FwkMsgReceiver_t *pMsgRxer,
					     FwkMsg_t *pMsg)
{
	SensorTaskObj_t *pObj = FWK_TASK_CONTAINER(SensorTaskObj_t);
	k_timer_stop(&pObj->msgTask.timer);
	pObj->connected = false;
	char *name = log_strdup(pObj->pCmdMsg->name);
	if (pObj->configComplete) {
		LOG_INF("'%s' configured", name);
		AckConfigRequest(pObj);
	} else {
		LOG_ERR("'%s' NOT configured", name);
		(void)RetryConfigRequest(pObj);
		pObj->configDisconnects += 1;
	}

	bt_conn_unref(pObj->conn);
	pObj->conn = NULL;
	bt_scan_restart(pObj->scanUserId);

	return DISPATCH_OK;
}

static DispatchResult_t SensorShadowInitMsgHandler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsgRxer);
	SensorTable_ProcessShadowInitMsg((SensorShadowInitMsg_t *)pMsg);
	return DISPATCH_OK;
}

static void RegisterConnectionCallbacks(void)
{
	static struct bt_conn_cb connectionCallbacks = {
		.connected = ConnectedCallback,
		.disconnected = DisconnectedCallback,
		.le_param_req = NULL,
		.le_param_updated = NULL,
		.identity_resolved = NULL,
		.security_changed = SecurityChangedCallback
	};

	bt_conn_cb_register(&connectionCallbacks);
}

static int RegisterSecurityCallbacks(void)
{
	static struct bt_conn_auth_cb securityCallbacks = {
		.passkey_display = PasskeyDisplayCallback,
		.passkey_entry = PasskeyEntryCallback,
		.passkey_confirm = PasskeyConfirmCallback,
		.cancel = SecurityCancelCallback,
		.pairing_confirm = PairingConfirmCallback,
		.pairing_complete = PairingCompleteCallback,
		.pairing_failed = PairingFailedCallback
	};
	int status = bt_conn_auth_cb_register(&securityCallbacks);
	FRAMEWORK_ASSERT(status == BT_SUCCESS);
	return status;
}

static int Discover(void)
{
	int err = bt_gatt_discover(st.conn, &st.dp);
	if (err) {
		LOG_ERR("Discovery Failed %s", lbt_get_hci_err_string(err));
		FRAMEWORK_MSG_SEND_TO_SELF(FWK_ID_SENSOR_TASK,
					   FMC_DISCOVERY_FAILED);
	}
	return err;
}

static int Subscribe(void)
{
	int err = bt_gatt_subscribe(st.conn, &st.sp);
	if (err && err != -EALREADY) {
		LOG_ERR("Subscribe Failed %s", lbt_get_hci_err_string(err));
		FRAMEWORK_MSG_SEND_TO_SELF(FWK_ID_SENSOR_TASK,
					   FMC_DISCOVERY_FAILED);
	} else {
		FRAMEWORK_MSG_SEND_TO_SELF(FWK_ID_SENSOR_TASK,
					   FMC_DISCOVERY_COMPLETE);
	}
	return err;
}

static int WriteString(const char *str)
{
#ifdef CONFIG_VSP_TX_ECHO
	size_t len = strlen(str);
	printk("VSP TX length: %d ", len);
	size_t i;
	for (i = 0; i < len; i++) {
		printk("%c", str[i]);
	}
	printk("\r\n");
#endif

	size_t remaining = strlen(str);
	size_t index = 0;
	int status = 0;
	ST_LOG_DEV("length: %u", remaining);
	/* Chunk data to the size that the link supports.
	 * Zephyr handles flow control
	 */
	while ((remaining > 0) && (status == BT_SUCCESS)) {
		size_t chunk = MIN(st.mtu, remaining);
		remaining -= chunk;
		status = bt_gatt_write_without_response(
			st.conn, st.writeHandle, &str[index], chunk, false);
		index += chunk;
	}
	ST_LOG_DEV("rem: %u status: %d", remaining, status);
	return status;
}

static int StartDiscovery(void)
{
	/* There isn't any reason to discover the VSP service.
	 * The callback doesn't give a range of handles for service discovery.
	 */
	st.dp.uuid = (struct bt_uuid *)&VSP_RX_UUID;
	st.dp.func = DiscoveryCallback;
	st.dp.start_handle = FIRST_VALID_HANDLE;
	st.dp.end_handle = LAST_VALID_HANDLE;
	st.dp.type = BT_GATT_DISCOVER_CHARACTERISTIC;
	return Discover();
}

static int ExchangeMtu(void)
{
	st.mp.func = MtuCallback;
	int status = bt_gatt_exchange_mtu(st.conn, &st.mp);
	return status;
}

static int RequestDisconnect(SensorTaskObj_t *pObj, const char *str)
{
	int status = bt_conn_disconnect(pObj->conn,
					BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	LOG_INF("Disconnect Request: %d Reason: %s", status, str);
	return status;
}

/* Put the request back in to the table (because something failed during
 * attempt to write configuration.
 */
static DispatchResult_t RetryConfigRequest(SensorTaskObj_t *pObj)
{
	FRAMEWORK_ASSERT(pObj->pCmdMsg != NULL);
	DispatchResult_t result = SensorTable_RetryConfigRequest(pObj->pCmdMsg);
	pObj->pCmdMsg = NULL;
	return result;
}

static void AckConfigRequest(SensorTaskObj_t *pObj)
{
	FRAMEWORK_ASSERT(pObj->pCmdMsg != NULL);
	SensorTable_AckConfigRequest(pObj->pCmdMsg);
	pObj->pCmdMsg = NULL;
}

/******************************************************************************/
/* These Callbacks occur in BT thread context                                 */
/******************************************************************************/
/* This following code expects to be on a 32-bit architecture for atomicity. */

static void ConnectedCallback(struct bt_conn *conn, uint8_t err)
{
	LOG_DBG("%x-%u (%s)", (uint32_t)POINTER_TO_UINT(conn),
		bt_conn_index(conn), lbt_get_hci_err_string(err));

	/* CONFIG_BT_CREATE_CONN_TIMEOUT cannot be the default value of 3 when
	 * the BT510 is advertising at its default rate of 1 second.  This will
	 * result in the conn_le_update_timeout firing and an error code of
	 * UNKNOWN_CONN_ID.
	 */
	if (conn == st.conn) {
		if (err) {
			FRAMEWORK_MSG_SEND_TO_SELF(FWK_ID_SENSOR_TASK,
						   FMC_DISCONNECT);
		} else {
			FRAMEWORK_MSG_SEND_TO_SELF(FWK_ID_SENSOR_TASK,
						   FMC_START_DISCOVERY);
		}
	}
}

static void DisconnectedCallback(struct bt_conn *conn, uint8_t reason)
{
	LOG_DBG("%x-%u %s", (uint32_t)POINTER_TO_UINT(conn),
		bt_conn_index(conn), lbt_get_hci_err_string(reason));
	if (conn == st.conn) {
		FRAMEWORK_MSG_SEND_TO_SELF(FWK_ID_SENSOR_TASK, FMC_DISCONNECT);
	}
}

static int EncryptLink(SensorTaskObj_t *pObj)
{
	int status = bt_conn_set_security(pObj->conn, BT_SECURITY_L3);
	LOG_DBG("%d", status);
	return status;
}

static void PasskeyDisplayCallback(struct bt_conn *conn, unsigned int passkey)
{
	LOG_DBG("%d", passkey);
	if (conn == st.conn) {
	}
}

static void PasskeyEntryCallback(struct bt_conn *conn)
{
	/* Bug 16696 - Sensor connection only supports default pin */
	LOG_DBG(".");
	const unsigned int PIN = SENSOR_PIN_DEFAULT;
	if (conn == st.conn) {
		__ASSERT_EVAL((void)bt_conn_auth_passkey_entry(conn, PIN),
			      int result =
				      bt_conn_auth_passkey_entry(conn, PIN),
			      result == BT_SUCCESS, "Passkey entry failed");
	}
}

static void PasskeyConfirmCallback(struct bt_conn *conn, unsigned int passkey)
{
	LOG_DBG(".");
	if (conn == st.conn) {
		(void)bt_conn_auth_passkey_confirm(conn);
	}
}

static void SecurityCancelCallback(struct bt_conn *conn)
{
	LOG_DBG(".");
	if (conn == st.conn) {
	}
}

static void SecurityChangedCallback(struct bt_conn *conn, bt_security_t level,
				    enum bt_security_err err)
{
	LOG_DBG("%u", level);
	if (conn == st.conn) {
	}
}

static void PairingConfirmCallback(struct bt_conn *conn)
{
	LOG_DBG(".");
	if (conn == st.conn) {
		(void)bt_conn_auth_pairing_confirm(conn);
	}
}

static void PairingCompleteCallback(struct bt_conn *conn, bool bonded)
{
	LOG_DBG("%s bonded", bonded ? "" : "NOT");
	if (conn == st.conn) {
		st.paired = true;
	}
}

static void PairingFailedCallback(struct bt_conn *conn,
				  enum bt_security_err reason)
{
	LOG_DBG(".");
	if (conn == st.conn) {
		st.paired = false;
	}
}

static uint8_t DiscoveryCallback(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 struct bt_gatt_discover_params *params)
{
	if (conn != st.conn) {
		return BT_GATT_ITER_STOP;
	}

	if (!attr) {
		return BT_GATT_ITER_STOP;
	}

	ST_LOG_DEV("[ATTRIBUTE] handle %u", attr->handle);

	/* The discovery callback is used as a state machine */
	if (bt_uuid_cmp(params->uuid, &VSP_RX_UUID.uuid) == 0) {
		st.writeHandle = bt_gatt_attr_value_handle(attr);
		st.dp.uuid = (struct bt_uuid *)&VSP_TX_UUID;
		st.dp.type = BT_GATT_DISCOVER_CHARACTERISTIC;
		Discover();
	} else if (bt_uuid_cmp(params->uuid, &VSP_TX_UUID.uuid) == 0) {
		st.dp.uuid = (struct bt_uuid *)&VSP_TX_CCC_UUID;
		st.dp.start_handle = LBT_NEXT_HANDLE_AFTER_CHAR(attr->handle);
		st.dp.type = BT_GATT_DISCOVER_DESCRIPTOR;
		st.sp.value_handle = bt_gatt_attr_value_handle(attr);
		Discover();
	} else {
		/* Check for the expected UUID when discovery is complete. */
		FRAMEWORK_DEBUG_ASSERT(
			bt_uuid_cmp(params->uuid, &VSP_TX_CCC_UUID.uuid) == 0);
		st.sp.notify = NotificationCallback;
		st.sp.value = BT_GATT_CCC_NOTIFY;
		st.sp.ccc_handle = attr->handle;
		Subscribe();
	}

	return BT_GATT_ITER_STOP;
}

static uint8_t NotificationCallback(struct bt_conn *conn,
				    struct bt_gatt_subscribe_params *params,
				    const void *data, uint16_t length)
{
	if (conn != st.conn) {
		return BT_GATT_ITER_STOP;
	}

	if (!data) {
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	char *ptr = (char *)data;
	size_t i;
	for (i = 0; i < length; i++) {
		int result = Bracket_Compute(st.pBracket, ptr[i]);
		if (result == 0) {
			ST_LOG_DEV("Bracket Match");
			CreateAndSendResponseMsg(st.pBracket);
		}
	}

#ifdef CONFIG_VSP_RX_ECHO
	/* This data may be a partial string (and won't have a NULL) */
	printk("VSP RX length: %d ", length);
	for (i = 0; i < length; i++) {
		printk("%c", ptr[i]);
	}
	printk("\r\n");
#endif

	return BT_GATT_ITER_CONTINUE;
}

static void CreateAndSendResponseMsg(BracketObj_t *p)
{
	/* Reserve an extra byte for adding NULL at end of JSON string */
	size_t bufSize = Bracket_Length(p) + 1;
	FwkBufMsg_t *pMsg =
		BufferPool_Take(FWK_BUFFER_MSG_SIZE(FwkBufMsg_t, bufSize));
	if (pMsg != NULL) {
		pMsg->header.msgCode = FMC_RESPONSE;
		pMsg->header.txId = FWK_ID_SENSOR_TASK;
		pMsg->header.rxId = FWK_ID_SENSOR_TASK;
		pMsg->size = bufSize;
		pMsg->length = Bracket_Copy(p, pMsg->buffer);
		FRAMEWORK_MSG_SEND(pMsg);
	}
	Bracket_Reset(p);
}

static void MtuCallback(struct bt_conn *conn, uint8_t err,
			struct bt_gatt_exchange_params *params)
{
	if (conn == st.conn) {
		st.mtu = BT_MAX_PAYLOAD(bt_gatt_get_mtu(conn));
		ST_LOG_DEV("%u", st.mtu);
	}
}

/******************************************************************************/
/* Interrupt Service Routines                                                 */
/******************************************************************************/
static void SendSensorResetTimerCallbackIsr(struct k_timer *timer_id)
{
	UNUSED_PARAMETER(timer_id);
	FRAMEWORK_MSG_SEND_TO_SELF(FWK_ID_SENSOR_TASK, FMC_SEND_RESET);
}

static void SensorTickCallbackIsr(struct k_timer *timer_id)
{
	UNUSED_PARAMETER(timer_id);
	FRAMEWORK_MSG_SEND_TO_SELF(FWK_ID_SENSOR_TASK, FMC_SENSOR_TICK);
}

/******************************************************************************/
/* Occurs in BT RX Thread context                                             */
/******************************************************************************/
#ifdef CONFIG_SCAN_FOR_BT510
static void SensorTaskAdvHandler(const bt_addr_le_t *addr, int8_t rssi,
				 uint8_t type, struct net_buf_simple *ad)
{
	/* After filtering for BT510 sensors, send a message so we can
	 * process ads in Sensor Task context.
	 * This prevents the BLE RX task from being blocked.
	 */
	if (SensorTable_MatchBt510(ad)) {
		if (atomic_get(&st.adsOutstanding) >
		    SENSOR_TASK_MAX_OUTSTANDING_ADS) {
			atomic_inc(&st.adsDropped);
			return;
		}

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
		memcpy(pMsg->ad.data, ad->data,
		       MIN(CONFIG_SENSOR_MAX_AD_SIZE, ad->len));
		FRAMEWORK_MSG_SEND(pMsg);
		atomic_inc(&st.adsOutstanding);
	}
}
#endif