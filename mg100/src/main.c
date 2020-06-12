/**
 * @file main.c
 * @brief Application main entry point
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(mg100_main);

#define MAIN_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define MAIN_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define MAIN_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define MAIN_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stdio.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr.h>
#include <version.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>

#include "mg100_common.h"
#include "led.h"
#include "mg100_ble.h"
#include "lte.h"
#include "aws.h"
#include "battery.h"
#include "nv.h"
#include "ble_cellular_service.h"
#include "ble_aws_service.h"
#include "ble_battery_service.h"
#include "ble_sensor_service.h"
#include "ble_power_service.h"
#include "power.h"
#include "dis.h"
#include "bootloader.h"
#include "FrameworkIncludes.h"
#include "sensor_task.h"
#include "sensor_table.h"
#include "laird_utility_macros.h"
#include "sdcard_log.h"

#ifdef CONFIG_LWM2M
#include "lwm2m_client.h"
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#ifndef CONFIG_USE_SINGLE_AWS_TOPIC
#define CONFIG_USE_SINGLE_AWS_TOPIC 0
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
K_SEM_DEFINE(lte_ready_sem, 0, 1);
K_SEM_DEFINE(rx_cert_sem, 0, 1);

static float temperatureReading = 0;
static float humidityReading = 0;
static float pressureReading = 0;
static bool initShadow = true;
static bool resolveAwsServer = true;
static bool lteNeverConnected = true;

static bool bUpdatedTemperature = false;
static bool bUpdatedHumidity = false;
static bool bUpdatedPressure = false;

static bool gatewaySubscribed;
static bool subscribedToGetAccepted;
static bool getShadowProcessed;
static bool commissioned;
static bool allowCommissioning = false;
static bool appReady = false;
static bool devCertSet;
static bool devKeySet;

static app_state_function_t appState;
struct lte_status *lteInfo;

static FwkMsgReceiver_t awsMsgReceiver;

K_MSGQ_DEFINE(sensorQ, FWK_QUEUE_ENTRY_SIZE, 16, FWK_QUEUE_ALIGNMENT);

static struct led_blink_pattern dataSendLedPattern;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void appStateAwsSendSensorData(void);
static void appStateAwsInitShadow(void);
static void appStateAwsConnect(void);
static void appStateAwsDisconnect(void);
static void appStateAwsResolveServer(void);
static void appStateWaitForLte(void);
static void appStateCommissionDevice(void);
static void appStateStartup(void);
static void appStateLteConnectedAws(void);

static void appSetNextState(app_state_function_t next);
static const char *getAppStateString(app_state_function_t state);

static void setAwsStatusWrapper(struct bt_conn *conn, enum aws_status status);

static void initializeAwsMsgReceiver(void);
static void awsMsgHandler(s32_t *blinks);
static void awsSvcEvent(enum aws_svc_event event);
static int setAwsCredentials(void);
static void generateShadowRequestMsg(void);
static void ledPatternCompleteCallback(void);
static void sensorUpdated(u8_t sensor, s32_t reading);
static void lteEvent(enum lte_event event);
static void softwareReset(u32_t DelayMs);

#ifdef CONFIG_LWM2M
static void appStateInitLwm2mClient(void);
static void appStateLwm2m(void);
static void lwm2mMsgHandler(void);
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void main(void)
{
	int rc;

	/* init LEDS */
	led_init();

	Framework_Initialize();
	SensorTask_Initialize();
	initializeAwsMsgReceiver();

	/* Init NV storage */
	rc = nvInit();
	if (rc < 0) {
		MAIN_LOG_ERR("NV init (%d)", rc);
		goto exit;
	}

	sdCardLogInit();

	nvReadCommissioned(&commissioned);

	/* init LTE */
	lteRegisterEventCallback(lteEvent);
	rc = lteInit();
	if (rc < 0) {
		MAIN_LOG_ERR("LTE init (%d)", rc);
		goto exit;
	}
	lteInfo = lteGetStatus();

	/* init AWS */
	rc = awsInit();
	if (rc != 0) {
		goto exit;
	}

	dis_initialize();

	/* Start up BLE portion of the demo */
	cell_svc_init();
	cell_svc_assign_connection_handler_getter(
		mg100_ble_get_central_connection);
	cell_svc_set_imei(lteInfo->IMEI);
	cell_svc_set_fw_ver(lteInfo->radio_version);
	cell_svc_set_iccid(lteInfo->ICCID);
	cell_svc_set_serial_number(lteInfo->serialNumber);

	bss_init();
	bss_assign_connection_handler_getter(mg100_ble_get_central_connection);

	/* Setup the power service */
	power_svc_init();
	power_svc_assign_connection_handler_getter(
		mg100_ble_get_central_connection);

	/* Setup the battery service */
	battery_svc_init();
	battery_svc_assign_connection_handler_getter(
		mg100_ble_get_central_connection);

	/* Initialize the battery managment sub-system.
	 * NOTE: This must be executed after nvInit.
	 */
	power_init();
	BatteryInit();

	motion_svc_init();
	motion_svc_assign_connection_handler_getter(
		mg100_ble_get_central_connection);

	bootloader_init();

	rc = aws_svc_init(lteInfo->IMEI);
	if (rc != 0) {
		goto exit;
	}
	aws_svc_set_event_callback(awsSvcEvent);
	if (commissioned) {
		aws_svc_set_status(NULL, AWS_STATUS_DISCONNECTED);
	} else {
		aws_svc_set_status(NULL, AWS_STATUS_NOT_PROVISIONED);
	}

	mg100_ble_initialise(lteInfo->IMEI);
	mg100_ble_set_callback(sensorUpdated);

	appReady = true;
	printk("\n!!!!!!!! App is ready! !!!!!!!!\n");

	appSetNextState(appStateStartup);

	led_register_pattern_complete_function(GREEN_LED2,
					       ledPatternCompleteCallback);
	dataSendLedPattern.on_time = DATA_SEND_LED_ON_TIME_TICKS;
	dataSendLedPattern.off_time = DATA_SEND_LED_OFF_TIME_TICKS;
	dataSendLedPattern.repeat_count = 0;

	print_thread_list();

	while (true) {
		appState();
	}
exit:
	MAIN_LOG_ERR("Exiting main thread");
	return;
}

/******************************************************************************/
/* Framework                                                                  */
/******************************************************************************/
EXTERNED void Framework_AssertionHandler(char *file, int line)
{
	static atomic_t busy = ATOMIC_INIT(0);
	/* prevent recursion (buffer alloc fail, ...) */
	if (!busy) {
		atomic_set(&busy, 1);
		LOG_ERR("\r\n!---> Framework Assertion <---! %s:%d\r\n", file,
			line);
		LOG_ERR("Thread name: %s",
			log_strdup(k_thread_name_get(k_current_get())));
	}

#if CONFIG_LAIRD_CONNECTIVITY_DEBUG
	/* breakpoint location */
	volatile bool wait = true;
	while (wait)
		;
#endif

	softwareReset(CONFIG_FWK_RESET_DELAY_MS);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/

/* This is a callback function which receives sensor readings */
static void sensorUpdated(u8_t sensor, s32_t reading)
{
	static u64_t bmeEventTime = 0;
	/* On init send first data immediately. */
	static u32_t delta =
		K_MSEC(CONFIG_BL654_SENSOR_SEND_TO_AWS_RATE_SECONDS);

	if (sensor == SENSOR_TYPE_TEMPERATURE) {
		/* Divide by 100 to get xx.xxC format */
		temperatureReading = reading / 100.0;
		bUpdatedTemperature = true;
	} else if (sensor == SENSOR_TYPE_HUMIDITY) {
		/* Divide by 100 to get xx.xx% format */
		humidityReading = reading / 100.0;
		bUpdatedHumidity = true;
	} else if (sensor == SENSOR_TYPE_PRESSURE) {
		/* Divide by 10 to get x.xPa format */
		pressureReading = reading / 10.0;
		bUpdatedPressure = true;
	}

	delta += k_uptime_delta_32(&bmeEventTime);
	if (delta < K_MSEC(CONFIG_BL654_SENSOR_SEND_TO_AWS_RATE_SECONDS)) {
		return;
	}

	if (bUpdatedTemperature && bUpdatedHumidity && bUpdatedPressure) {
		BL654SensorMsg_t *pMsg =
			(BL654SensorMsg_t *)BufferPool_TryToTake(
				sizeof(BL654SensorMsg_t));
		if (pMsg == NULL) {
			return;
		}
		pMsg->header.msgCode = FMC_BL654_SENSOR_EVENT;
		pMsg->header.rxId = FWK_ID_AWS;
		pMsg->temperatureC = temperatureReading;
		pMsg->humidityPercent = humidityReading;
		pMsg->pressurePa = pressureReading;
		sdCardLogBL654Data(pMsg);
		FRAMEWORK_MSG_SEND(pMsg);
		bUpdatedTemperature = false;
		bUpdatedHumidity = false;
		bUpdatedPressure = false;
		delta = 0;
	}
}

static void lteEvent(enum lte_event event)
{
	switch (event) {
	case LTE_EVT_READY:
		lteNeverConnected = false;
		k_sem_give(&lte_ready_sem);
		break;
	case LTE_EVT_DISCONNECTED:
		k_sem_reset(&lte_ready_sem);
		break;
	default:
		break;
	}
}

static void appStateAwsSendSensorData(void)
{
	int rc;
	s32_t blinks = 0;

	/* If decommissioned then disconnect. */
	if (!commissioned || !awsConnected()) {
		appSetNextState(appStateAwsDisconnect);
		led_turn_off(GREEN_LED2);
		return;
	}

	setAwsStatusWrapper(mg100_ble_get_central_connection(),
			    AWS_STATUS_CONNECTED);

	if (!CONFIG_USE_SINGLE_AWS_TOPIC) {
		if (!subscribedToGetAccepted) {
			rc = awsGetAcceptedSubscribe();
			if (rc == 0) {
				subscribedToGetAccepted = true;
			}
		}

		if (!getShadowProcessed) {
			awsGetShadow();
		}

		if (getShadowProcessed && !gatewaySubscribed) {
			rc = awsSubscribe(GATEWAY_TOPIC, true);
			if (rc == 0) {
				gatewaySubscribed = true;
			}
		}

		if (getShadowProcessed && gatewaySubscribed) {
			generateShadowRequestMsg();
		}
	}

	awsMsgHandler(&blinks);

	/* Periodically sending the RSSI keeps AWS connection alive. */
	if (blinks == 0) {
		lteInfo = lteGetStatus();
		rc = awsPublishPinnacleData(lteInfo->rssi, lteInfo->sinr);
		if (rc == 0) {
			blinks += 1;
		}
	}

	/* The RSSI is always sent. If the count is 0 there was a problem. */
	if (blinks > 0) {
		dataSendLedPattern.repeat_count = blinks - 1;
		led_blink(GREEN_LED2, &dataSendLedPattern);
	} else {
		led_turn_off(GREEN_LED2);
	}

	if (k_msgq_num_used_get(&sensorQ) != 0) {
		MAIN_LOG_WRN("%u unsent messages",
			     k_msgq_num_used_get(&sensorQ));
	}
}

static const char *getAppStateString(app_state_function_t state)
{
#ifdef CONFIG_LWM2M
	IF_RETURN_STRING(state, appStateLwm2m);
	IF_RETURN_STRING(state, appStateInitLwm2mClient);
#endif
	IF_RETURN_STRING(state, appStateAwsSendSensorData);
	IF_RETURN_STRING(state, appStateAwsConnect);
	IF_RETURN_STRING(state, appStateAwsDisconnect);
	IF_RETURN_STRING(state, appStateWaitForLte);
	IF_RETURN_STRING(state, appStateLteConnectedAws);
	IF_RETURN_STRING(state, appStateAwsResolveServer);
	IF_RETURN_STRING(state, appStateAwsInitShadow);
	IF_RETURN_STRING(state, appStateCommissionDevice);
	IF_RETURN_STRING(state, appStateStartup);
	return "appStateUnknown";
}

static void appSetNextState(app_state_function_t next)
{
	MAIN_LOG_DBG("%s->%s", getAppStateString(appState),
		     getAppStateString(next));
	appState = next;
}

static void appStateStartup(void)
{
#ifdef CONFIG_LWM2M
	appSetNextState(appStateWaitForLte);
#else
	if (commissioned && setAwsCredentials() == 0) {
		appSetNextState(appStateWaitForLte);
	} else {
		appSetNextState(appStateCommissionDevice);
	}
#endif
}

/* This function will throw away sensor data if it can't send it. */
static void awsMsgHandler(s32_t *blinks)
{
	int rc = 0;
	FwkMsg_t *pMsg;
	bool freeMsg;

	while (rc == 0) {
		/* Remove sensor/gateway data from queue and send it to cloud.
		 * Block if there are not any messages, but periodically send RSSI to
		 * keep AWS connection open.
		 */
		rc = -EINVAL;
		pMsg = NULL;
		Framework_Receive(awsMsgReceiver.pQueue, &pMsg,
				  K_SECONDS(CONFIG_AWS_QUEUE_TIMEOUT_SECONDS));
		if (pMsg == NULL) {
			return;
		}
		freeMsg = true;

		switch (pMsg->header.msgCode) {
		case FMC_BL654_SENSOR_EVENT: {
			BL654SensorMsg_t *pBmeMsg = (BL654SensorMsg_t *)pMsg;
			rc = awsPublishBl654SensorData(pBmeMsg->temperatureC,
						       pBmeMsg->humidityPercent,
						       pBmeMsg->pressurePa);
		} break;

		case FMC_SENSOR_PUBLISH: {
			JsonMsg_t *pJsonMsg = (JsonMsg_t *)pMsg;
			rc = awsSendData(pJsonMsg->buffer,
					 CONFIG_USE_SINGLE_AWS_TOPIC ?
						 GATEWAY_TOPIC :
						 pJsonMsg->topic);
		} break;

		case FMC_GATEWAY_OUT: {
			JsonMsg_t *pJsonMsg = (JsonMsg_t *)pMsg;
			rc = awsSendData(pJsonMsg->buffer, GATEWAY_TOPIC);
			if (rc == 0) {
				FRAMEWORK_MSG_CREATE_AND_SEND(
					FWK_ID_AWS, FWK_ID_SENSOR_TASK,
					FMC_SHADOW_ACK);
			}
		} break;

		case FMC_SUBSCRIBE: {
			SubscribeMsg_t *pSubMsg = (SubscribeMsg_t *)pMsg;
			rc = awsSubscribe(pSubMsg->topic, pSubMsg->subscribe);
			pSubMsg->success = (rc == 0);
			FRAMEWORK_MSG_REPLY(pSubMsg, FMC_SUBSCRIBE_ACK);
			freeMsg = false;
		} break;

		case FMC_AWS_GET_ACCEPTED_RECEIVED: {
			rc = awsGetAcceptedUnsub();
			if (rc == 0) {
				getShadowProcessed = true;
			}
		} break;

		default:
			break;
		}
		if (freeMsg) {
			BufferPool_Free(pMsg);
		}

		if (rc != 0) {
			MAIN_LOG_ERR("Could not send data (%d)", rc);
		} else {
			*blinks += 1;
		}
	}
}

static void ledPatternCompleteCallback(void)
{
	/* This occurs in LED timer context (system workq) */
	if (awsConnected()) {
		led_turn_on(GREEN_LED2);
	} else {
		led_turn_off(GREEN_LED2);
	}
}

/* Used to limit table generation to once per data interval. */
static void generateShadowRequestMsg(void)
{
	FwkMsg_t *pMsg = BufferPool_Take(sizeof(FwkMsg_t));
	if (pMsg != NULL) {
		pMsg->header.msgCode = FMC_SHADOW_REQUEST;
		pMsg->header.txId = FWK_ID_RESERVED;
		pMsg->header.rxId = FWK_ID_SENSOR_TASK;
		FRAMEWORK_MSG_SEND(pMsg);
		/* Allow sensor task to process message immediately
		 * (if system is idle). */
		k_yield();
	}
}

/* The shadow init is only sent once after the very first connect.*/
static void appStateAwsInitShadow(void)
{
	int rc = 0;

	if (initShadow) {
		awsGenerateGatewayTopics(lteInfo->IMEI);
		/* Fill in base shadow info and publish */
		awsSetShadowAppFirmwareVersion(APP_VERSION_STRING);
		awsSetShadowKernelVersion(KERNEL_VERSION_STRING);
		awsSetShadowIMEI(lteInfo->IMEI);
		awsSetShadowICCID(lteInfo->ICCID);
		awsSetShadowRadioFirmwareVersion(lteInfo->radio_version);
		awsSetShadowRadioSerialNumber(lteInfo->serialNumber);

		MAIN_LOG_INF("Send persistent shadow data");
		rc = awsPublishShadowPersistentData();
	}

	if (rc != 0) {
		LOG_ERR("%d", rc);
		appSetNextState(appStateAwsDisconnect);
	} else {
		initShadow = false;
		appSetNextState(appStateAwsSendSensorData);
		FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_RESERVED,
						   FMC_AWS_CONNECTED);
	}
}

static void appStateAwsConnect(void)
{
	if (awsConnect() != 0) {
		MAIN_LOG_ERR("Could not connect to AWS");
		setAwsStatusWrapper(mg100_ble_get_central_connection(),
				    AWS_STATUS_CONNECTION_ERR);

		/* wait some time before trying to re-connect */
		k_sleep(WAIT_TIME_BEFORE_RETRY_TICKS);
		return;
	}

	nvStoreCommissioned(true);
	commissioned = true;
	allowCommissioning = false;

	setAwsStatusWrapper(mg100_ble_get_central_connection(),
			    AWS_STATUS_CONNECTING);

	appSetNextState(appStateAwsInitShadow);
}

static bool areCertsSet(void)
{
	return (devCertSet && devKeySet);
}

static void appStateAwsDisconnect(void)
{
	setAwsStatusWrapper(mg100_ble_get_central_connection(),
			    AWS_STATUS_DISCONNECTED);
	awsDisconnect();
	gatewaySubscribed = false;
	FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_RESERVED,
					   FMC_AWS_DISCONNECTED);
	appSetNextState(appStateAwsConnect);
}

static void appStateAwsResolveServer(void)
{
	if (awsGetServerAddr() != 0) {
		MAIN_LOG_ERR("Could not get server address");
		/* wait some time before trying to resolve address again */
		k_sleep(WAIT_TIME_BEFORE_RETRY_TICKS);
		return;
	}
	resolveAwsServer = false;
	appSetNextState(appStateAwsConnect);
}

static void appStateWaitForLte(void)
{
	setAwsStatusWrapper(mg100_ble_get_central_connection(),
			    AWS_STATUS_DISCONNECTED);

	if (lteNeverConnected && !lteIsReady()) {
		/* Wait for LTE ready evt */
		k_sem_take(&lte_ready_sem, K_FOREVER);
	}

#ifdef CONFIG_LWM2M
	appSetNextState(appStateInitLwm2mClient);
#else
	appSetNextState(appStateLteConnectedAws);
#endif
}

static void appStateLteConnectedAws(void)
{
	if (resolveAwsServer && areCertsSet()) {
		appSetNextState(appStateAwsResolveServer);
	} else if (areCertsSet()) {
		appSetNextState(appStateAwsConnect);
	} else {
		appSetNextState(appStateCommissionDevice);
	}
}

#ifdef CONFIG_LWM2M
static void appStateInitLwm2mClient(void)
{
	lwm2m_client_init();
	appSetNextState(appStateLwm2m);
}

static void appStateLwm2m(void)
{
	lwm2mMsgHandler();
}

static void lwm2mMsgHandler(void)
{
	int rc = 0;
	FwkMsg_t *pMsg;

	while (rc == 0) {
		/* Remove sensor/gateway data from queue and send it to cloud. */
		rc = -EINVAL;
		pMsg = NULL;
		Framework_Receive(awsMsgReceiver.pQueue, &pMsg, K_FOREVER);
		if (pMsg == NULL) {
			return;
		}

		switch (pMsg->header.msgCode) {
		case FMC_BL654_SENSOR_EVENT: {
			BL654SensorMsg_t *pBmeMsg = (BL654SensorMsg_t *)pMsg;
			rc = lwm2m_set_bl654_sensor_data(
				pBmeMsg->temperatureC, pBmeMsg->humidityPercent,
				pBmeMsg->pressurePa);
		} break;

		default:
			break;
		}
		BufferPool_Free(pMsg);

		if (rc != 0) {
			MAIN_LOG_ERR("Could not send data (%d)", rc);
		}
	}
}
#endif

static int setAwsCredentials(void)
{
	int rc;

	if (!aws_svc_client_cert_is_stored()) {
		return APP_ERR_READ_CERT;
	}

	if (!aws_svc_client_key_is_stored()) {
		return APP_ERR_READ_KEY;
	}
	devCertSet = true;
	devKeySet = true;
	rc = awsSetCredentials(aws_svc_get_client_cert(),
			       aws_svc_get_client_key());
	return rc;
}

static void appStateCommissionDevice(void)
{
	printk("\n\nWaiting to commission device\n\n");
	setAwsStatusWrapper(mg100_ble_get_central_connection(),
			    AWS_STATUS_NOT_PROVISIONED);
	allowCommissioning = true;

	k_sem_take(&rx_cert_sem, K_FOREVER);
	if (setAwsCredentials() == 0) {
		appSetNextState(appStateWaitForLte);
	}
}

static void decommission(void)
{
	nvStoreCommissioned(false);
	devCertSet = false;
	devKeySet = false;
	commissioned = false;
	allowCommissioning = true;
	appSetNextState(appStateAwsDisconnect);
	printk("Device is decommissioned\n");
}

static void awsSvcEvent(enum aws_svc_event event)
{
	switch (event) {
	case AWS_SVC_EVENT_SETTINGS_SAVED:
		devCertSet = true;
		devKeySet = true;
		k_sem_give(&rx_cert_sem);
		break;
	case AWS_SVC_EVENT_SETTINGS_CLEARED:
		decommission();
		break;
	}
}

static void setAwsStatusWrapper(struct bt_conn *conn, enum aws_status status)
{
	aws_svc_set_status(conn, status);
}

static void initializeAwsMsgReceiver(void)
{
	awsMsgReceiver.id = FWK_ID_AWS;
	awsMsgReceiver.pQueue = &sensorQ;
	awsMsgReceiver.rxBlockTicks = 0; /* unused */
	awsMsgReceiver.pMsgDispatcher = NULL; /* unused */
	Framework_RegisterReceiver(&awsMsgReceiver);
}

static void softwareReset(u32_t DelayMs)
{
#ifdef CONFIG_REBOOT
	LOG_ERR("Software Reset in %d milliseconds", DelayMs);
	k_sleep(K_MSEC(DelayMs));
	power_reboot_module(REBOOT_TYPE_NORMAL);
#endif
}

/******************************************************************************/
/* Shell                                                                      */
/******************************************************************************/
#ifdef CONFIG_SHELL
static int shellSetCert(enum CREDENTIAL_TYPE type, u8_t *cred)
{
	int rc;
	int certSize;
	int expSize = 0;
	char *newCred = NULL;

	if (!appReady) {
		printk("App is not ready\n");
		return APP_ERR_NOT_READY;
	}

	if (!allowCommissioning) {
		printk("Not ready for commissioning, decommission device first\n");
		return APP_ERR_COMMISSION_DISALLOWED;
	}

	certSize = strlen(cred);

	if (type == CREDENTIAL_CERT) {
		expSize = AWS_CLIENT_CERT_MAX_LENGTH;
		newCred = (char *)aws_svc_get_client_cert();
	} else if (type == CREDENTIAL_KEY) {
		expSize = AWS_CLIENT_KEY_MAX_LENGTH;
		newCred = (char *)aws_svc_get_client_key();
	} else {
		return APP_ERR_UNKNOWN_CRED;
	}

	if (certSize > expSize) {
		printk("Cert is too large (%d)\n", certSize);
		return APP_ERR_CRED_TOO_LARGE;
	}

	replace_word(cred, "\\n", "\n", newCred, expSize);
	replace_word(newCred, "\\s", " ", newCred, expSize);

	rc = aws_svc_save_clear_settings(true);
	if (rc < 0) {
		MAIN_LOG_ERR("Error storing credential (%d)", rc);
	} else if (type == CREDENTIAL_CERT) {
		printk("Stored cert:\n%s\n", newCred);
		devCertSet = true;

	} else if (type == CREDENTIAL_KEY) {
		printk("Stored key:\n%s\n", newCred);
		devKeySet = true;
	}

	if (rc >= 0 && areCertsSet()) {
		k_sem_give(&rx_cert_sem);
	}

	return rc;
}

static int shell_set_aws_device_cert(const struct shell *shell, size_t argc,
				     char **argv)
{
	ARG_UNUSED(argc);

	return shellSetCert(CREDENTIAL_CERT, argv[1]);
}

static int shell_set_aws_device_key(const struct shell *shell, size_t argc,
				    char **argv)
{
	ARG_UNUSED(argc);

	return shellSetCert(CREDENTIAL_KEY, argv[1]);
}

static int shell_decommission(const struct shell *shell, size_t argc,
			      char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!appReady) {
		printk("App is not ready\n");
		return APP_ERR_NOT_READY;
	}

	aws_svc_save_clear_settings(false);
	decommission();

	return 0;
}

#ifdef CONFIG_REBOOT
static int shell_reboot(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	power_reboot_module(REBOOT_TYPE_NORMAL);

	return 0;
}

static int shell_bootloader(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	power_reboot_module(REBOOT_TYPE_BOOTLOADER);

	return 0;
}
#endif /* CONFIG_REBOOT */
#endif /* CONFIG_SHELL */

#ifdef CONFIG_SHELL
SHELL_STATIC_SUBCMD_SET_CREATE(
	mg100_cmds,
	SHELL_CMD_ARG(set_cert, NULL, "Set device cert",
		      shell_set_aws_device_cert, 2, 0),
	SHELL_CMD_ARG(set_key, NULL, "Set device key", shell_set_aws_device_key,
		      2, 0),
	SHELL_CMD(reset, NULL, "Factory reset (decommission) device",
		  shell_decommission),
#ifdef CONFIG_REBOOT
	SHELL_CMD(reboot, NULL, "Reboot module", shell_reboot),
	SHELL_CMD(bootloader, NULL, "Boot to UART bootloader",
		  shell_bootloader),
#endif
	SHELL_SUBCMD_SET_END /* Array terminated. */
);
SHELL_CMD_REGISTER(mg100, &mg100_cmds, "MG100 commands", NULL);

static int shell_send_at_cmd(const struct shell *shell, size_t argc,
			     char **argv)
{
	if ((argc == 2) && (argv[1] != NULL)) {
		int result = mdm_hl7800_send_at_cmd(argv[1]);
		if (result < 0) {
			shell_error(shell, "Command not accepted");
		}
	} else {
		shell_error(shell, "Invalid parameter");
		return -EINVAL;
	}
	return 0;
}

SHELL_CMD_REGISTER(at, NULL, "Send an AT command string to the HL7800",
		   shell_send_at_cmd);

static int print_thread_cmd(const struct shell *shell, size_t argc, char **argv)
{
	print_thread_list();
	return 0;
}

SHELL_CMD_REGISTER(print_threads, NULL, "Print list of threads",
		   print_thread_cmd);
#endif /* CONFIG_SHELL */
