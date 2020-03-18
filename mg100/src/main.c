/* main.c - Application main entry point
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(mg100_main);

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
#include "nv.h"
#include "ble_cellular_service.h"
#include "ble_aws_service.h"
#include "ble_sensor_service.h"
#include "ble_power_service.h"
#include "power.h"
#include "dis.h"
#include "bootloader.h"
#include "Framework.h"
#include "SensorTask.h"
#include "SensorBt510.h"

#define MAIN_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define MAIN_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define MAIN_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define MAIN_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

static void appStateAwsSendSensorData(void);
static void appStateAwsInitShadow(void);
static void appStateAwsConnect(void);
static void appStateAwsDisconnect(void);
static void appStateAwsResolveServer(void);
static void appStateWaitForLte(void);
static void appStateCommissionDevice(void);
static void appStateWaitForModemDisconnect(void);

static void setAwsStatusWrapper(struct bt_conn *conn, enum aws_status status);

static void initializeAwsMsgReceiver(void);
static void awsMsgHandler(void);
static void generateBt510shadowRequestMsg(void);
static void blinkLedForDataSend(void);

K_SEM_DEFINE(lte_ready_sem, 0, 1);
K_SEM_DEFINE(rx_cert_sem, 0, 1);

static float temperatureReading = 0;
static float humidityReading = 0;
static float pressureReading = 0;
static bool initShadow = true;
static bool resolveAwsServer = true;

static bool bUpdatedTemperature = false;
static bool bUpdatedHumidity = false;
static bool bUpdatedPressure = false;

static bool subscribed;
static bool commissioned;
static bool allowCommissioning = false;
static bool appReady = false;
static bool devCertSet;
static bool devKeySet;

static volatile app_state_function_t appState;
struct lte_status *lteInfo;

static FwkMsgReceiver_t awsMsgReceiver;

K_MSGQ_DEFINE(sensorQ, FWK_QUEUE_ENTRY_SIZE, 16, FWK_QUEUE_ALIGNMENT);

static u64_t linkUpTime = 0;
static u32_t linkUpDelta;

/* This is a callback function which receives sensor readings */
static void SensorUpdated(u8_t sensor, s32_t reading)
{
	static u64_t bmeEventTime = 0;
	/* On init send first data immediately. */
	static u32_t delta = BL654_SENSOR_SEND_TO_AWS_RATE_TICKS;

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
	if (delta < BL654_SENSOR_SEND_TO_AWS_RATE_TICKS) {
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
		FRAMEWORK_MSG_TRY_TO_SEND(pMsg);
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
		k_uptime_delta_32(&linkUpTime);
		k_sem_give(&lte_ready_sem);
		break;
	case LTE_EVT_DISCONNECTED:
		if (awsConnected()) {
			appState = appStateAwsDisconnect;
		} else {
			appState = appStateWaitForLte;
		}
		break;
	default:
		break;
	}
}

static void appStateAwsSendSensorData(void)
{
	int rc;
	MAIN_LOG_DBG("AWS send sensor data state");

	/* If decommissioned then disconnect. */
	if (!commissioned || !awsConnected()) {
		appState = appStateAwsDisconnect;
		return;
	}

	setAwsStatusWrapper(mg100_ble_get_central_connection(),
			    AWS_STATUS_CONNECTED);

	if (!BT510_USES_SINGLE_AWS_TOPIC) {
		if (!subscribed) {
			rc = awsSubscribe();
			subscribed = (rc == 0);
		}

		generateBt510shadowRequestMsg();
	}

	lteInfo = lteGetStatus();
	rc = awsPublishPinnacleData(lteInfo->rssi, lteInfo->sinr);
	if (rc == 0) {
		blinkLedForDataSend();
		awsMsgHandler();
	}
	LOG_INF("%u unsent messages", k_msgq_num_used_get(&sensorQ));

	/* The purpose of this delay is to allow the UI (green LED) to be on
	 * for a noticeable amount of time after data is sent. */
	led_turn_on(GREEN_LED2);
	k_sleep(SEND_DATA_TO_DISCONNECT_DELAY_TICKS);

#if CONFIG_MODEM_HL7800_PSM
	/* Disconnect from AWS now that the data has been sent. */
	appState = appStateAwsDisconnect;
#else
	k_sleep(PSM_DISABLED_SEND_DATA_RATE_TICKS);
#endif
}

/* The modem will periodically wake and sleep.
 * After data is sent wait for the modem to go to sleep.
 * Then wait for the modem to wakeup.
 * (The modem controls the timing of sending the data.)
 */
static void appStateWaitForModemDisconnect(void)
{
	MAIN_LOG_DBG("Wait for Disconnect state");

	while (lteIsReady()) {
		k_sleep(WAIT_FOR_DISCONNECT_POLL_RATE_TICKS);
	}

	if (appState == appStateWaitForModemDisconnect) {
		appState = appStateWaitForLte;
	}
}

/* This function will throw away sensor data if it can't send it. */
static void awsMsgHandler(void)
{
	int rc = 0;
	FwkMsg_t *pMsg;

	linkUpDelta = 0;

	while (rc == 0) {
#if CONFIG_MODEM_HL7800_PSM
		/* Only send data if we have enough time to send it */
		linkUpDelta += k_uptime_delta_32(&linkUpTime);
		if (linkUpDelta >= PSM_ENABLED_SEND_DATA_WINDOW_TICKS) {
			LOG_INF("Send data window closed");
			return;
		}
#endif
		/* Remove sensor/gateway data from queue and send it to cloud. */
		rc = -EINVAL;
		pMsg = NULL;
		Framework_Receive(awsMsgReceiver.pQueue, &pMsg, K_NO_WAIT);
		if (pMsg == NULL) {
			return;
		}

		switch (pMsg->header.msgCode) {
		case FMC_BL654_SENSOR_EVENT: {
			BL654SensorMsg_t *pBmeMsg = (BL654SensorMsg_t *)pMsg;
			rc = awsPublishBl654SensorData(pBmeMsg->temperatureC,
						       pBmeMsg->humidityPercent,
						       pBmeMsg->pressurePa);
		} break;

		case FMC_BT510_EVENT: {
			JsonMsg_t *pJsonMsg = (JsonMsg_t *)pMsg;
			rc = awsSendData(pJsonMsg->buffer,
					 BT510_USES_SINGLE_AWS_TOPIC ?
						 GATEWAY_TOPIC :
						 pJsonMsg->topic);
		} break;

		case FMC_BT510_GATEWAY_OUT: {
			JsonMsg_t *pJsonMsg = (JsonMsg_t *)pMsg;
			rc = awsSendData(pJsonMsg->buffer, GATEWAY_TOPIC);
		} break;

		default:
			break;
		}
		BufferPool_Free(pMsg);

		if (rc != 0) {
			MAIN_LOG_ERR("Could not send data (%d)", rc);
		} else {
			MAIN_LOG_INF("Data sent");
			blinkLedForDataSend();
		}
	}
}

static void blinkLedForDataSend(void)
{
	led_turn_off(GREEN_LED2);
	k_sleep(DATA_SEND_LED_OFF_TIME_TICKS);
}

/* Used to limit table generation to once per data interval. */
static void generateBt510shadowRequestMsg(void)
{
	FwkMsg_t *pMsg = BufferPool_TryToTake(sizeof(FwkMsg_t));
	if (pMsg != NULL) {
		pMsg->header.msgCode = FMC_BT510_SHADOW_REQUEST;
		pMsg->header.txId = FWK_ID_RESERVED;
		pMsg->header.rxId = FWK_ID_SENSOR_TASK;
		/* Try is used because the sensor task queue can be filled with ads. */
		FRAMEWORK_MSG_TRY_TO_SEND(pMsg);
		/* Allow sensor task to process message immediately (if system is idle). */
		k_yield();
	}
}

static void appStateAwsInitShadow(void)
{
	int rc;

	MAIN_LOG_DBG("AWS init shadow state");

	/* Fill in base shadow info and publish */
	awsSetShadowAppFirmwareVersion(APP_VERSION_STRING);
	awsSetShadowKernelVersion(KERNEL_VERSION_STRING);
	awsSetShadowIMEI(lteInfo->IMEI);
	awsSetShadowICCID(lteInfo->ICCID);
	awsSetShadowRadioFirmwareVersion(lteInfo->radio_version);
	awsSetShadowRadioSerialNumber(lteInfo->serialNumber);

	MAIN_LOG_INF("Send persistent shadow data");
	rc = awsPublishShadowPersistentData();
	if (rc != 0) {
		appState = appStateAwsDisconnect;
	} else {
		initShadow = false;
		/* The shadow init is only sent once after the very first connect.*/
		appState = appStateAwsSendSensorData; /* code */
	}
}

static void appStateAwsConnect(void)
{
	MAIN_LOG_DBG("AWS connect state");

	if (!lteIsReady()) {
		appState = appStateWaitForLte;
		return;
	}

	if (awsConnect() != 0) {
		MAIN_LOG_ERR("Could not connect to AWS");
		setAwsStatusWrapper(mg100_ble_get_central_connection(),
				    AWS_STATUS_CONNECTION_ERR);
		appState = appStateWaitForModemDisconnect;
		return;
	}

	nvStoreCommissioned(true);
	commissioned = true;
	allowCommissioning = false;

	setAwsStatusWrapper(mg100_ble_get_central_connection(),
			    AWS_STATUS_CONNECTING);

	if (initShadow) {
		/* Init the shadow once, the first time we connect */
		appState = appStateAwsInitShadow;
	} else {
		appState = appStateAwsSendSensorData;
	}
}

static bool areCertsSet(void)
{
	return (devCertSet && devKeySet);
}

static void appStateAwsDisconnect(void)
{
	MAIN_LOG_DBG("AWS disconnect state");
	setAwsStatusWrapper(mg100_ble_get_central_connection(),
			    AWS_STATUS_DISCONNECTED);
	awsDisconnect();
	appState = appStateWaitForModemDisconnect;
}

static void appStateAwsResolveServer(void)
{
	MAIN_LOG_DBG("AWS resolve server state");

	if (!lteIsReady()) {
		appState = appStateWaitForLte;
		return;
	}

	if (awsGetServerAddr() != 0) {
		MAIN_LOG_ERR("Could not get server address");
		appState = appStateWaitForModemDisconnect;
		return;
	}
	resolveAwsServer = false;
	appState = appStateAwsConnect;
}

static void appStateWaitForLte(void)
{
	MAIN_LOG_DBG("Wait for LTE state");

	setAwsStatusWrapper(mg100_ble_get_central_connection(),
			    AWS_STATUS_DISCONNECTED);

	if (!lteIsReady()) {
		/* Wait for LTE read evt */
		k_sem_reset(&lte_ready_sem);
		k_sem_take(&lte_ready_sem, K_FOREVER);
	}

	if (resolveAwsServer && areCertsSet()) {
		appState = appStateAwsResolveServer;
	} else if (areCertsSet()) {
		appState = appStateAwsConnect;
	} else {
		appState = appStateCommissionDevice;
	}
}

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
	MAIN_LOG_DBG("Commission device state");
	printk("\n\nWaiting to commission device\n\n");
	setAwsStatusWrapper(mg100_ble_get_central_connection(),
			    AWS_STATUS_NOT_PROVISIONED);
	allowCommissioning = true;

	k_sem_take(&rx_cert_sem, K_FOREVER);
	if (setAwsCredentials() == 0) {
		appState = appStateWaitForLte;
	}
}

static void decommission(void)
{
	nvStoreCommissioned(false);
	devCertSet = false;
	devKeySet = false;
	commissioned = false;
	allowCommissioning = true;
	appState = appStateAwsDisconnect;
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

	if (status == AWS_STATUS_CONNECTED) {
		led_turn_on(GREEN_LED2);
	} else {
		led_turn_off(GREEN_LED2);
	}
}

static void initializeAwsMsgReceiver(void)
{
	awsMsgReceiver.id = FWK_ID_AWS;
	awsMsgReceiver.pQueue = &sensorQ;
	awsMsgReceiver.rxBlockTicks = 0; /* unused */
	awsMsgReceiver.pMsgDispatcher = NULL; /* unused */
	Framework_RegisterReceiver(&awsMsgReceiver);
}

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

	bss_init();
	bss_assign_connection_handler_getter(mg100_ble_get_central_connection);

	/* Setup the power service */
	power_svc_init();
	power_svc_assign_connection_handler_getter(
		mg100_ble_get_central_connection);
	power_init();

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
	mg100_ble_set_callback(SensorUpdated);

	appReady = true;
	printk("\n!!!!!!!! App is ready! !!!!!!!!\n");

	if (commissioned && setAwsCredentials() == 0) {
		appState = appStateWaitForLte;
	} else {
		appState = appStateCommissionDevice;
	}

	print_thread_list();

	while (true) {
		appState();
	}
exit:
	MAIN_LOG_ERR("Exiting main thread");
	return;
}

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

static void SoftwareReset(u32_t DelayMs)
{
#ifdef CONFIG_REBOOT
	LOG_ERR("Software Reset in %d milliseconds", DelayMs);
	k_sleep(K_MSEC(DelayMs));
	power_reboot_module(REBOOT_TYPE_NORMAL);
#endif
}

EXTERNED void Framework_AssertionHandler(char *file, int line)
{
	static atomic_t busy = ATOMIC_INIT(0);
	/* prevent recursion (buffer alloc fail, ...) */
	if (!busy) {
		atomic_set(&busy, 1);
		LOG_ERR("\r\n!---> Fwk Assertion <---! %s:%d\r\n", file, line);
		LOG_ERR("Thread name: %s",
			log_strdup(k_thread_name_get(k_current_get())));
	}

#if LAIRD_DEBUG
	/* breakpoint location */
	volatile bool wait = true;
	while (wait)
		;
#endif

	SoftwareReset(FWK_DEFAULT_RESET_DELAY_MS);
}
