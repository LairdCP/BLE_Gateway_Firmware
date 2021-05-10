/**
 * @file main.c
 * @brief Application main entry point
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(main);

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
#include <string.h>

#include "led_configuration.h"
#include "ble.h"
#ifdef CONFIG_MODEM_HL7800
#include "lte.h"
#include "ble_cellular_service.h"
#endif
#include "nv.h"
#include "ble_aws_service.h"
#ifdef CONFIG_BLE_POWER_SERVICE
#include "ble_power_service.h"
#endif
#ifdef CONFIG_LCZ_POWER
#include "laird_power.h"
#endif
#include "dis.h"
#include "FrameworkIncludes.h"
#include "laird_utility_macros.h"
#include "lcz_bluetooth.h"
#include "single_peripheral.h"
#include "string_util.h"
#include "app_version.h"
#include "lcz_bt_scan.h"
#include "fota.h"
#include "button.h"
#include "lcz_software_reset.h"
#include "sdcard_log.h"

#ifdef CONFIG_BOARD_MG100
#include "lairdconnect_battery.h"
#include "ble_battery_service.h"
#include "ble_motion_service.h"
#endif

#ifdef CONFIG_LCZ_NFC
#include "laird_connectivity_nfc.h"
#endif

#ifdef CONFIG_BL654_SENSOR
#include "bl654_sensor.h"
#endif

#ifdef CONFIG_BLUEGRASS
#include "aws.h"
#include "bluegrass.h"
#endif

#ifdef CONFIG_LWM2M
#include "lcz_lwm2m_client.h"
#include "ble_lwm2m_service.h"
#endif

#ifdef CONFIG_MCUMGR
#include "mcumgr_wrapper.h"
#endif

#if defined(CONFIG_BLUEGRASS) && defined(CONFIG_COAP_FOTA)
#include "coap_fota_task.h"
#endif
#if defined(CONFIG_BLUEGRASS) && defined(CONFIG_HTTP_FOTA)
#include "http_fota_task.h"
#endif

#include "lcz_memfault.h"

#ifdef CONFIG_CONTACT_TRACING
#include "ct_app.h"
#include "ct_ble.h"
#endif

#ifdef CONFIG_DFU_TARGET_MCUBOOT
#include <dfu/mcuboot.h>
#endif

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifdef CONFIG_LCZ_MEMFAULT
#define BUILD_ID_SIZE 9
#define BUILD_ID_DELIM "+"
#endif

#define WAIT_TIME_BEFORE_RETRY_TICKS K_SECONDS(10)

#define BT_ID_ADDR_SIZE 15 /* BLE address string size with null character */

enum CREDENTIAL_TYPE { CREDENTIAL_CERT, CREDENTIAL_KEY };

enum APP_ERROR {
	APP_ERR_NOT_READY = -1,
	APP_ERR_COMMISSION_DISALLOWED = -2,
	APP_ERR_CRED_TOO_LARGE = -3,
	APP_ERR_UNKNOWN_CRED = -4,
	APP_ERR_READ_CERT = -5,
	APP_ERR_READ_KEY = -6,
};

typedef void (*app_state_function_t)(void);

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
#ifdef CONFIG_BLUEGRASS
bool initShadow = true; /* can be set by lte */
#endif

#ifdef CONFIG_BOARD_BL5340_DVK_CPUAPP
char bt_id_addr[BT_ID_ADDR_SIZE];
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
#ifdef CONFIG_LCZ_MEMFAULT
static char build_id[BUILD_ID_SIZE];
static char software_ver[sizeof(APP_VERSION_STRING) + sizeof(BUILD_ID_DELIM) +
			 sizeof(build_id)];
#endif

K_SEM_DEFINE(lte_ready_sem, 0, 1);
K_SEM_DEFINE(rx_cert_sem, 0, 1);

#ifdef CONFIG_BLUEGRASS
static bool resolveAwsServer = true;
static bool allowCommissioning = false;
static bool devCertSet;
static bool devKeySet;
static struct k_delayed_work heartbeat;
static bool start_fota = false;
#endif

static FwkMsgReceiver_t cloudMsgReceiver;
static bool commissioned;
static bool appReady = false;

static app_state_function_t appState;
struct lte_status *lteInfo;
#ifdef CONFIG_BOARD_MG100
struct battery_data *batteryInfo;
struct motion_status *motionInfo;
struct sdcard_status *sdcardInfo;
#endif

K_MSGQ_DEFINE(cloudQ, FWK_QUEUE_ENTRY_SIZE, CONFIG_CLOUD_QUEUE_SIZE,
	      FWK_QUEUE_ALIGNMENT);

/* Sensor events are not received properly unless filter duplicates is OFF */
#if defined(CONFIG_SCAN_FOR_BT510_CODED)
static struct bt_le_scan_param scanParameters =
	BT_LE_SCAN_PARAM_INIT(BT_LE_SCAN_TYPE_ACTIVE, BT_LE_SCAN_OPT_CODED,
			      BT_GAP_SCAN_FAST_INTERVAL,
			      BT_GAP_SCAN_FAST_WINDOW);
#elif defined(CONFIG_SCAN_FOR_BT510)
static struct bt_le_scan_param scanParameters =
	BT_LE_SCAN_PARAM_INIT(BT_LE_SCAN_TYPE_ACTIVE, BT_LE_SCAN_OPT_NONE,
			      BT_GAP_SCAN_FAST_INTERVAL,
			      BT_GAP_SCAN_FAST_WINDOW);
#endif

static struct k_timer fifo_timer;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
#ifdef CONFIG_BLUEGRASS
static void appStateAwsSendSensorData(void);
static void appStateAwsInitShadow(void);
static void appStateAwsConnect(void);
static void appStateAwsDisconnect(void);
static void appStateAwsResolveServer(void);
static int setAwsCredentials(void);
static void appStateCommissionDevice(void);
static void appStateLteConnectedAws(void);
static void awsMsgHandler(void);
static void set_commissioned(void);
static void appStateWaitFota(void);
static void heartbeat_work_handler(struct k_work *work);
#endif

static void initializeCloudMsgReceiver(void);
static void appStateWaitForLte(void);
static void appStateLteConnected(void);

static void appSetNextState(app_state_function_t next);
static const char *getAppStateString(app_state_function_t state);

static void lteEvent(enum lte_event event);

#ifdef CONFIG_LWM2M
static void appStateInitLwm2mClient(void);
static void appStateLwm2m(void);
static void lwm2mMsgHandler(void);
#endif

static void configure_leds(void);

static void cloud_fifo_monitor_isr(struct k_timer *timer_id);

#ifndef CONFIG_CONTACT_TRACING
static int adv_on_button_isr(void);
#endif

static void configure_button(void);
static void reset_reason_handler(void);
static char *get_app_type(void);

#if defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
static inline int bt_addr_le_to_str_compact(const bt_addr_le_t *addr,
					    char *str, size_t len);
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void main(void)
{
	int rc;

	MFLT_METRICS_TIMER_START(lte_ttf);

#ifdef CONFIG_LCZ_MEMFAULT_HTTP_TRANSPORT
	lcz_memfault_http_init(CONFIG_LCZ_MEMFAULT_PROJECT_API_KEY);
#endif

	printk("\n" CONFIG_BOARD " - %s v%s\n", get_app_type(),
	       APP_VERSION_STRING);

	reset_reason_handler();

	configure_leds();

	Framework_Initialize();

	/* Init NV storage */
	rc = nvInit();
	if (rc < 0) {
		MAIN_LOG_ERR("NV init (%d)", rc);
		goto exit;
	}

#ifdef CONFIG_SD_CARD_LOG
	rc = sdCardLogInit();
#ifdef CONFIG_CONTACT_TRACING
	if (rc == 0) {
		sdCardCleanup();
	}
#endif
#endif

	nvReadCommissioned(&commissioned);

#ifdef CONFIG_MODEM_HL7800
	/* init LTE */
	lteRegisterEventCallback(lteEvent);
	rc = lteInit();
	if (rc < 0) {
		MAIN_LOG_ERR("LTE init (%d)", rc);
		goto exit;
	}
	lteInfo = lteGetStatus();
#endif

#if defined(CONFIG_BOARD_MG100) || defined(CONFIG_BOARD_PINNACLE_100_DVK)
	LCZ_MEMFAULT_BUILD_TOPIC(CONFIG_BOARD, lteInfo->IMEI);
#endif

	/* init AWS */
#ifdef CONFIG_BLUEGRASS
	rc = awsInit();
	if (rc != 0) {
		goto exit;
	}
#endif

	initializeCloudMsgReceiver();

#if defined(CONFIG_BOARD_MG100) || defined(CONFIG_BOARD_PINNACLE_100_DVK)
	ble_update_name(lteInfo->IMEI);
#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
	/* Get local BLE address in compact form */
	bt_addr_le_t addr;
	size_t addrs;
	addrs = 1;
	bt_id_get(&addr, &addrs);

	if (addrs == 0) {
		MAIN_LOG_ERR("Failed to get local Bluetooth ID");
		goto exit;
	}

	bt_addr_le_to_str_compact(&addr, bt_id_addr, sizeof(bt_id_addr));
	ble_update_name(bt_id_addr);
#endif
	configure_button();

#ifdef CONFIG_SCAN_FOR_BT510
	lcz_bt_scan_set_parameters(&scanParameters);
#endif

#ifdef CONFIG_BL654_SENSOR
	bl654_sensor_initialize();
#endif

#ifdef CONFIG_BLUEGRASS
	Bluegrass_Initialize(cloudMsgReceiver.pQueue);
	k_delayed_work_init(&heartbeat, heartbeat_work_handler);
#endif

	dis_initialize(APP_VERSION_STRING);

#ifdef CONFIG_BLE_CELLULAR_SERVICE
	/* Start up BLE portion of the demo */
	cell_svc_init();
	cell_svc_set_imei(lteInfo->IMEI);
	cell_svc_set_fw_ver(lteInfo->radio_version);
	cell_svc_set_iccid(lteInfo->ICCID);
	cell_svc_set_serial_number(lteInfo->serialNumber);
#endif

#ifdef CONFIG_FOTA_SERVICE
	fota_init();
#endif

	/* Setup the power service */
#ifdef CONFIG_BLE_POWER_SERVICE
	power_svc_init();
#endif
#ifdef CONFIG_LCZ_POWER
	power_init();
#endif

#ifdef CONFIG_BOARD_MG100
	/* Setup the battery service */
	battery_svc_init();

	/* Initialize the battery management sub-system.
	 * NOTE: This must be executed after nvInit.
	 */
	BatteryInit();

	motion_svc_init();
#endif

#ifdef CONFIG_LCZ_NFC
	laird_connectivity_nfc_init();
#endif

#ifdef CONFIG_LCZ_MCUMGR_WRAPPER
	mcumgr_wrapper_register_subsystems();
#endif

#ifdef CONFIG_BLUEGRASS
#if defined(CONFIG_BOARD_MG100) || defined(CONFIG_BOARD_PINNACLE_100_DVK)
	rc = aws_svc_init(lteInfo->IMEI);
#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
	rc = aws_svc_init(bt_id_addr);
#endif
	if (rc != 0) {
		goto exit;
	}

	if (commissioned) {
		aws_svc_set_status(AWS_STATUS_DISCONNECTED);
	} else {
		aws_svc_set_status(AWS_STATUS_NOT_PROVISIONED);
	}
#endif

#ifdef CONFIG_LWM2M
	ble_lwm2m_service_init();
#endif

#if defined(CONFIG_BLUEGRASS) && defined(CONFIG_COAP_FOTA)
	coap_fota_task_initialize();
#endif
#if defined(CONFIG_BLUEGRASS) && defined(CONFIG_HTTP_FOTA)
	http_fota_task_initialize();
#endif

#ifdef CONFIG_CONTACT_TRACING
	ct_app_init();
#endif

	k_timer_init(&fifo_timer, cloud_fifo_monitor_isr, NULL);
	k_timer_start(&fifo_timer,
		      K_SECONDS(CONFIG_CLOUD_FIFO_CHECK_RATE_SECONDS),
		      K_SECONDS(CONFIG_CLOUD_FIFO_CHECK_RATE_SECONDS));

#ifdef CONFIG_DFU_TARGET_MCUBOOT
	/* if we've gotten this far, assume the image is ok */
	if (!boot_is_img_confirmed()) {
		boot_write_img_confirmed();
	}
#endif

	appReady = true;
	printk("\n!!!!!!!! App is ready! !!!!!!!!\n");

	appSetNextState(appStateWaitForLte);

	/* Advertise on startup after setting name and after CT init.
	 * This allows image confirmation from the mobile application during FOTA.
	 * Button handling normally happens in ISR context, but in this case it does not.
	 */
#ifdef CONFIG_CONTACT_TRACING
	ct_adv_on_button_isr();
#else
	adv_on_button_isr();
#endif

	while (true) {
		appState();
	}
exit:
	MAIN_LOG_ERR("Exiting main thread");
	return;
}

#ifdef CONFIG_LCZ_MEMFAULT
void memfault_platform_get_device_info(sMemfaultDeviceInfo *info)
{
	memset(software_ver, 0, sizeof(software_ver));
	memfault_build_id_get_string(build_id, sizeof(build_id));
	strcat(software_ver, APP_VERSION_STRING);
	strcat(software_ver, BUILD_ID_DELIM);
	strcat(software_ver, build_id);

	/* platform specific version information */
	*info = (sMemfaultDeviceInfo)
	{
#if defined(CONFIG_BOARD_MG100) || defined(CONFIG_BOARD_PINNACLE_100_DVK)
		.device_serial = lteInfo->IMEI,
#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
		.device_serial = bt_id_addr,
#endif
		.software_type = CONFIG_LCZ_MEMFAULT_SOFTWARE_TYPE,
		.software_version = software_ver,
		.hardware_version = CONFIG_BOARD,
	};
}
#endif /* CONFIG_LCZ_MEMFAULT */

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

#ifdef CONFIG_LAIRD_CONNECTIVITY_DEBUG
	/* breakpoint location */
	volatile bool wait = true;
	while (wait)
		;
#endif

	lcz_software_reset(CONFIG_FWK_RESET_DELAY_MS);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/

static void initializeCloudMsgReceiver(void)
{
	cloudMsgReceiver.id = FWK_ID_CLOUD;
	cloudMsgReceiver.pQueue = &cloudQ;
	cloudMsgReceiver.rxBlockTicks = K_NO_WAIT; /* unused */
	cloudMsgReceiver.pMsgDispatcher = NULL; /* unused */
	Framework_RegisterReceiver(&cloudMsgReceiver);
}

static void lteEvent(enum lte_event event)
{
	switch (event) {
	case LTE_EVT_READY:
		k_sem_give(&lte_ready_sem);
		break;
	case LTE_EVT_DISCONNECTED:
		k_sem_reset(&lte_ready_sem);
		break;
	default:
		break;
	}
}

static const char *getAppStateString(app_state_function_t state)
{
#ifdef CONFIG_LWM2M
	IF_RETURN_STRING(state, appStateLwm2m);
	IF_RETURN_STRING(state, appStateInitLwm2mClient);
#endif
#ifdef CONFIG_BLUEGRASS
	IF_RETURN_STRING(state, appStateAwsSendSensorData);
	IF_RETURN_STRING(state, appStateAwsConnect);
	IF_RETURN_STRING(state, appStateAwsDisconnect);
	IF_RETURN_STRING(state, appStateAwsResolveServer);
	IF_RETURN_STRING(state, appStateAwsInitShadow);
	IF_RETURN_STRING(state, appStateLteConnectedAws);
	IF_RETURN_STRING(state, appStateCommissionDevice);
	IF_RETURN_STRING(state, appStateWaitFota);
#endif
	IF_RETURN_STRING(state, appStateWaitForLte);
	IF_RETURN_STRING(state, appStateLteConnected);
	return "appStateUnknown";
}

static void appSetNextState(app_state_function_t next)
{
	MAIN_LOG_DBG("%s->%s", getAppStateString(appState),
		     getAppStateString(next));
	appState = next;
}

static void appStateWaitForLte(void)
{
#ifdef CONFIG_BLUEGRASS
	aws_svc_set_status(AWS_STATUS_DISCONNECTED);
#endif

#ifdef CONFIG_MODEM_HL7800
	if (!lteIsReady()) {
		/* Wait for LTE ready evt */
		k_sem_take(&lte_ready_sem, K_FOREVER);
	}
#endif

#ifdef CONFIG_LCZ_MEMFAULT_HTTP_TRANSPORT
#ifdef CONFIG_LCZ_MEMFAULT_METRICS
	/* force metric data flush to report anything thats ready at this point */
	memfault_metrics_heartbeat_debug_trigger();
#endif
	lcz_memfault_post_data();
#endif

#if defined(CONFIG_LWM2M)
	appSetNextState(appStateInitLwm2mClient);
#elif defined(CONFIG_BLUEGRASS)
	if (commissioned && setAwsCredentials() == 0) {
		appSetNextState(appStateLteConnectedAws);
	} else {
		appSetNextState(appStateCommissionDevice);
	}
#else
	appSetNextState(appStateLteConnected);
#endif
}

static void appStateLteConnected(void)
{
	k_sleep(K_SECONDS(1));
}

#ifdef CONFIG_BLUEGRASS
static void appStateAwsSendSensorData(void)
{
	/* If decommissioned then disconnect. */
	if (!commissioned || !awsConnected() || start_fota) {
		appSetNextState(appStateAwsDisconnect);
		lcz_led_turn_off(CLOUD_LED);
		return;
	}

	/* Process messages until there is an error. */
	awsMsgHandler();

	if (k_msgq_num_used_get(&cloudQ) != 0) {
		MAIN_LOG_WRN("%u unsent messages",
			     k_msgq_num_used_get(&cloudQ));
	}
}

/* This function will throw away sensor data if it can't send it.
 * Subscription failures can occur even when the return value was success.
 * An AWS disconnect callback is used to send a message to unblock this queue.
 * This allows the UI (green LED) to be updated immediately.
 */
static void awsMsgHandler(void)
{
	int rc = 0;
	FwkMsg_t *pMsg;
	bool freeMsg;
	int fota_task_id = 0;

#ifdef CONFIG_BOARD_MG100
#ifndef CONFIG_SD_CARD_LOG
	struct sdcard_status sd_log_disabled_status = { -1, -1, -1 };
#endif
#endif

	while (rc == 0 && !start_fota) {
		lcz_led_turn_on(CLOUD_LED);
		/* Remove sensor/gateway data from queue and send it to cloud.
		 * Block if there are not any messages.
		 */
		rc = -EINVAL;
		Framework_Receive(cloudMsgReceiver.pQueue, &pMsg, K_FOREVER);
		freeMsg = true;

#ifdef CONFIG_LCZ_MEMFAULT_MQTT_TRANSPORT
		lcz_memfault_publish_data(awsGetMqttClient());
#endif

		/* BL654 data is sent to the gateway topic.  If Bluegrass is enabled,
		 * then sensor data (BT510) is sent to individual topics.  It also allows
		 * AWS to configure sensors.
		 */
		switch (pMsg->header.msgCode) {
		case FMC_BL654_SENSOR_EVENT: {
			BL654SensorMsg_t *pBmeMsg = (BL654SensorMsg_t *)pMsg;
#if defined(CONFIG_SD_CARD_LOG) && defined(CONFIG_BL654_SENSOR)
			sdCardLogBL654Data(pBmeMsg);
#endif
			rc = awsPublishBl654SensorData(pBmeMsg->temperatureC,
						       pBmeMsg->humidityPercent,
						       pBmeMsg->pressurePa);
		} break;

		case FMC_AWS_HEARTBEAT: {
			/* Periodically send the RSSI as a heartbeat. */
#ifdef CONFIG_MODEM_HL7800
			lteInfo = lteGetStatus();
#ifdef CONFIG_BOARD_MG100
			batteryInfo = batteryGetStatus();
			motionInfo = motionGetStatus();
#ifdef CONFIG_SD_CARD_LOG
			sdcardInfo = sdCardLogGetStatus();
#else
			sdcardInfo = &sd_log_disabled_status;
#endif
			rc = awsPublishPinnacleData(lteInfo->rssi,
						    lteInfo->sinr, batteryInfo,
						    motionInfo, sdcardInfo);
#else
			rc = awsPublishPinnacleData(lteInfo->rssi,
						    lteInfo->sinr);
#endif
#endif
#if CONFIG_AWS_HEARTBEAT_SECONDS != 0
			k_delayed_work_submit(
				&heartbeat,
				K_SECONDS(CONFIG_AWS_HEARTBEAT_SECONDS));
#endif
		} break;

		case FMC_AWS_DECOMMISSION:
		case FMC_AWS_DISCONNECTED:
			/* Message is used to unblock queue. */
			break;

		case FMC_FOTA_START:
			start_fota = true;
#ifdef CONFIG_COAP_FOTA
			fota_task_id = FWK_ID_COAP_FOTA_TASK;
#elif CONFIG_HTTP_FOTA
			fota_task_id = FWK_ID_HTTP_FOTA_TASK;
#endif
			FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_RESERVED,
						      fota_task_id,
						      FMC_FOTA_START_ACK);
			break;

		default:
			rc = Bluegrass_MsgHandler(pMsg, &freeMsg);
			break;
		}

		if (freeMsg) {
			BufferPool_Free(pMsg);
		}

		/* A publish error will most likely result in an immediate disconnect.
		 * A disconnect due to a subscription error may be delayed.
		 *
		 * When the permissions change on a sensor topic
		 * (sensor enabled in Bluegrass) the first subscription will result in
		 * a disconnect. The second attempt will work.
		 */
		lcz_led_turn_off(CLOUD_LED);
		if (rc == 0) {
			k_sleep(K_MSEC(
				CONFIG_AWS_DATA_SEND_LED_OFF_DURATION_MILLISECONDS));
		}
	}
}

/* The shadow init is only sent once after the very first connect.*/
static void appStateAwsInitShadow(void)
{
	int rc = 0;

	if (initShadow) {
#if defined(CONFIG_BOARD_MG100) || defined(CONFIG_BOARD_PINNACLE_100_DVK)
		awsGenerateGatewayTopics(lteInfo->IMEI);
#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
		awsGenerateGatewayTopics(bt_id_addr);
#endif
		/* Fill in base shadow info and publish */
		awsSetShadowAppFirmwareVersion(APP_VERSION_STRING);
		awsSetShadowKernelVersion(KERNEL_VERSION_STRING);
#ifdef CONFIG_MODEM_HL7800
		awsSetShadowIMEI(lteInfo->IMEI);
		awsSetShadowICCID(lteInfo->ICCID);
		awsSetShadowRadioFirmwareVersion(lteInfo->radio_version);
		awsSetShadowRadioSerialNumber(lteInfo->serialNumber);
#endif

		MAIN_LOG_INF("Send persistent shadow data");
		rc = awsPublishShadowPersistentData();
	}

	if (rc != 0) {
		LOG_ERR("Could not publish shadow (%d)", rc);
		appSetNextState(appStateAwsDisconnect);
		k_sleep(WAIT_TIME_BEFORE_RETRY_TICKS);
	} else {
		initShadow = false;
		appSetNextState(appStateAwsSendSensorData);
		Bluegrass_ConnectedCallback();
		k_delayed_work_submit(&heartbeat, K_NO_WAIT);
#ifdef CONFIG_CONTACT_TRACING
		ct_ble_publish_dummy_data_to_aws();
		/* Try to send stashed entries immediately on re-connect */
		ct_ble_check_stashed_log_entries();
#endif
	}
}

void awsDisconnectCallback(void)
{
	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_RESERVED, FWK_ID_CLOUD,
				      FMC_AWS_DISCONNECTED);
}

static void appStateAwsConnect(void)
{
	if ((devCertSet != true) || (devKeySet != true)) {
		appSetNextState(appStateCommissionDevice);
		return;
	}

#ifdef CONFIG_MODEM_HL7800
	if (!lteIsReady()) {
		appSetNextState(appStateWaitForLte);
		return;
	}
#endif

	aws_svc_set_status(AWS_STATUS_CONNECTING);

	if (awsConnect() != 0) {
		MAIN_LOG_ERR("Could not connect to AWS");
		aws_svc_set_status(AWS_STATUS_CONNECTION_ERR);

		/* wait some time before trying to re-connect */
		k_sleep(WAIT_TIME_BEFORE_RETRY_TICKS);
		return;
	}

	aws_svc_set_status(AWS_STATUS_CONNECTED);

	appSetNextState(appStateAwsInitShadow);
}

static bool areCertsSet(void)
{
	return (devCertSet && devKeySet);
}

static void appStateAwsDisconnect(void)
{
	awsDisconnect();

	aws_svc_set_status(AWS_STATUS_DISCONNECTED);

	FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_RESERVED,
					   FMC_AWS_DISCONNECTED);

	Bluegrass_DisconnectedCallback();

	if (start_fota) {
		appSetNextState(appStateWaitFota);
	} else {
		appSetNextState(appStateAwsConnect);
	}
}

static void appStateWaitFota(void)
{
	FwkMsg_t *pMsg;
	bool fota_busy = true;
	start_fota = false;
	while (fota_busy) {
		/* wait for a message from another task */
		Framework_Receive(cloudMsgReceiver.pQueue, &pMsg, K_FOREVER);

		switch (pMsg->header.msgCode) {
		case FMC_FOTA_DONE:
			/* FOTA is done for now, re-connect to AWS */
			fota_busy = false;
			appSetNextState(appStateAwsConnect);
			break;

		default:
			/* discard other messages */
			break;
		}

		BufferPool_Free(pMsg);
	}
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
	aws_svc_set_status(AWS_STATUS_NOT_PROVISIONED);
	allowCommissioning = true;

	k_sem_take(&rx_cert_sem, K_FOREVER);
	if (setAwsCredentials() == 0) {
		appSetNextState(appStateLteConnectedAws);
	}
}

static void decommission(void)
{
	nvStoreCommissioned(false);
	nvStoreAwsEnableCustom(false);
	devCertSet = false;
	devKeySet = false;
	commissioned = false;
	allowCommissioning = true;
	initShadow = true;
	appSetNextState(appStateAwsDisconnect);

	/* If the device is deleted from AWS it must be decommissioned
	 * in the BLE app before it is reprovisioned.
	 */
#ifdef CONFIG_SENSOR_TASK
	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_RESERVED, FWK_ID_SENSOR_TASK,
				      FMC_AWS_DECOMMISSION);
#endif

	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_RESERVED, FWK_ID_CLOUD,
				      FMC_AWS_DECOMMISSION);

	printk("Device is decommissioned\n");
}

static void set_commissioned(void)
{
	nvStoreCommissioned(true);
	commissioned = true;
	allowCommissioning = false;
	aws_svc_set_status(AWS_STATUS_DISCONNECTED);
	k_sem_give(&rx_cert_sem);
	printk("Device is commissioned\n");
}

void awsSvcEvent(enum aws_svc_event event)
{
	switch (event) {
	case AWS_SVC_EVENT_SETTINGS_SAVED:
		set_commissioned();
		break;
	case AWS_SVC_EVENT_SETTINGS_CLEARED:
		decommission();
		break;
	}
}

static void heartbeat_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_CLOUD, FWK_ID_CLOUD,
				      FMC_AWS_HEARTBEAT);
}

#endif /* CONFIG_BLUEGRASS */

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
		Framework_Receive(cloudMsgReceiver.pQueue, &pMsg, K_FOREVER);

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

static void configure_leds(void)
{
#if defined(CONFIG_BOARD_MG100)
	struct lcz_led_configuration c[] = {
		{ BLUE_LED, LED2_DEV, LED2, LED_ACTIVE_HIGH },
		{ GREEN_LED, LED3_DEV, LED3, LED_ACTIVE_HIGH },
		{ RED_LED, LED1_DEV, LED1, LED_ACTIVE_HIGH }
	};
#elif defined(CONFIG_BOARD_PINNACLE_100_DVK)
	struct lcz_led_configuration c[] = {
		{ BLUE_LED, LED1_DEV, LED1, LED_ACTIVE_HIGH },
		{ GREEN_LED, LED2_DEV, LED2, LED_ACTIVE_HIGH },
		{ RED_LED, LED3_DEV, LED3, LED_ACTIVE_HIGH },
		{ GREEN_LED2, LED4_DEV, LED4, LED_ACTIVE_HIGH }
	};
#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
	struct lcz_led_configuration c[] = {
		{ BLUE_LED1, LED1_DEV, LED1, LED_ACTIVE_LOW },
		{ BLUE_LED2, LED2_DEV, LED2, LED_ACTIVE_LOW },
		{ BLUE_LED3, LED3_DEV, LED3, LED_ACTIVE_LOW },
		{ BLUE_LED4, LED4_DEV, LED4, LED_ACTIVE_LOW }
	};
#else
#error "Unsupported board selected"
#endif
	lcz_led_init(c, ARRAY_SIZE(c));
}

#ifndef CONFIG_CONTACT_TRACING
static int adv_on_button_isr(void)
{
	single_peripheral_start_advertising();
	return 0;
}
#endif

static void configure_button(void)
{
#ifdef CONFIG_CONTACT_TRACING
	static const button_config_t BUTTON_CONFIG[] = {
		{ 0, 0, ct_adv_on_button_isr }
	};
#else
	static const button_config_t BUTTON_CONFIG[] = {
		{ 0, 0, adv_on_button_isr }
	};
#endif

	button_initialize(BUTTON_CONFIG, ARRAY_SIZE(BUTTON_CONFIG), NULL);
}

/* Override weak implementation in laird_power.c */
void power_measurement_callback(uint8_t integer, uint8_t decimal)
{
#ifdef CONFIG_BOARD_MG100
	uint16_t Voltage;

	Voltage = (integer * BATTERY_MV_PER_V) + decimal;
	BatteryCalculateRemainingCapacity(Voltage);
#else
	power_svc_set_voltage(integer, decimal);
#endif
}

static void reset_reason_handler(void)
{
#ifndef CONFIG_LCZ_MEMFAULT
	uint32_t reset_reason = lbt_get_and_clear_nrf52_reset_reason_register();

	LOG_WRN("reset reason: %s (%08X)",
		lbt_get_nrf52_reset_reason_string_from_register(reset_reason),
		reset_reason);
#endif
}

static char *get_app_type(void)
{
#ifdef CONFIG_LWM2M
	return "LwM2M";
#elif defined CONFIG_CONTACT_TRACING
	return "Contact Tracing";
#else
	return "AWS";
#endif
}

#if defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
static inline int bt_addr_le_to_str_compact(const bt_addr_le_t *addr,
					    char *str, size_t len)
{
	return snprintk(str, len, "%02x%02x%02x%02x%02x%02x%02x", addr->type,
			addr->a.val[5], addr->a.val[4], addr->a.val[3],
			addr->a.val[2], addr->a.val[1], addr->a.val[0]);
}
#endif

/******************************************************************************/
/* Shell                                                                      */
/******************************************************************************/
#ifdef CONFIG_SHELL
#ifdef CONFIG_BLUEGRASS
static int shell_decommission(const struct shell *shell, size_t argc,
			      char **argv)
{
	int rc = 0;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!appReady) {
		printk("App is not ready\n");
		rc = APP_ERR_NOT_READY;
		goto done;
	}

	aws_svc_save_clear_settings(false);
	decommission();
done:
	return rc;
}
#endif /* CONFIG_BLUEGRASS */

static int shell_ver_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int rc = 0;

	shell_print(shell, APP_VERSION_STRING);

	return rc;
}

SHELL_STATIC_SUBCMD_SET_CREATE(oob_cmds,
#ifdef CONFIG_BLUEGRASS
			       SHELL_CMD(reset, NULL,
					 "Factory reset (decommission) device",
					 shell_decommission),
#endif /* CONFIG_BLUEGRASS */
			       SHELL_CMD(ver, NULL, "Firmware version",
					 shell_ver_cmd),
			       SHELL_SUBCMD_SET_END /* Array terminated. */
);
SHELL_CMD_REGISTER(oob, &oob_cmds, "OOB Demo commands", NULL);
#endif /* CONFIG_SHELL */

/******************************************************************************/
/* Interrupt Service Routines                                                 */
/******************************************************************************/
/* The cloud (AWS) queue isn't checked in all states.  Therefore, it needs to
 * be periodically checked so that other tasks don't overfill it
 */
static void cloud_fifo_monitor_isr(struct k_timer *timer_id)
{
	ARG_UNUSED(timer_id);

	uint32_t numUsed = k_msgq_num_used_get(cloudMsgReceiver.pQueue);
	if (numUsed > CONFIG_CLOUD_PURGE_THRESHOLD) {
		size_t flushed = Framework_Flush(FWK_ID_CLOUD);
		if (flushed > 0) {
			LOG_WRN("Flushed %u cloud messages", flushed);
		}
	}
}
