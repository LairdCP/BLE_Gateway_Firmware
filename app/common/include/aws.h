/**
 * @file aws.h
 * @brief Amazon Web Services API
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __AWS_H__
#define __AWS_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <data/json.h>
#include <net/mqtt.h>

#ifdef CONFIG_BOARD_MG100
#include "lairdconnect_battery.h"
#include "lcz_motion.h"
#include "sdcard_log.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/

#define APP_SLEEP_MSECS 500
#define SOCKET_POLL_WAIT_TIME_MSECS 250

#define APP_CONNECT_TRIES 1

#define AWS_MQTT_ID_MAX_SIZE 128

#define SHADOW_STATE_NULL "{\"state\":null}"
#define SHADOW_REPORTED_START "{\"state\":{\"reported\":{"
#define SHADOW_REPORTED_END "}}}"

#define SHADOW_TEMPERATURE "\"temperature\":"
#define SHADOW_HUMIDITY "\"humidity\":"
#define SHADOW_PRESSURE "\"pressure\":"
#ifdef CONFIG_MODEM_HL7800
#define SHADOW_RADIO_RSSI "\"radio_rssi\":"
#define SHADOW_RADIO_SINR "\"radio_sinr\":"
#endif /* CONFIG_MODEM_HL7800 */
#ifdef CONFIG_BOARD_MG100
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
#endif /* CONFIG_BOARD_MG100 */

#define AWS_RX_THREAD_STACK_SIZE 2048
#define AWS_RX_THREAD_PRIORITY K_PRIO_COOP(15)

#define GATEWAY_TOPIC NULL

struct shadow_persistent_values {
	const char *firmware_version;
	const char *os_version;
#ifdef CONFIG_MODEM_HL7800
	const char *radio_version;
	const char *IMEI;
	const char *ICCID;
	const char *radio_sn;
#endif /* CONFIG_MODEM_HL7800 */
	bool codedPhySupported;
	bool httpFotaEnabled;
};

struct shadow_state_reported {
	struct shadow_persistent_values reported;
};

struct shadow_reported_struct {
	struct shadow_state_reported state;
};

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

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
int awsInit(void);
void awsSetRootCa(const char *cred);
void awsSetEndpoint(const char *ep);
void awsSetClientId(const char *id);
int awsGetServerAddr(void);
int awsConnect(void);
bool awsConnected(void);
bool awsPublished(void);
int awsDisconnect(void);
int awsSendData(char *data, uint8_t *topic);
int awsSendBinData(char *data, uint32_t len, uint8_t *topic);
int awsPublishShadowPersistentData(void);
int awsSetShadowKernelVersion(const char *version);
#ifdef CONFIG_MODEM_HL7800
int awsSetShadowIMEI(const char *imei);
int awsSetShadowICCID(const char *iccid);
int awsSetShadowRadioSerialNumber(const char *sn);
int awsSetShadowRadioFirmwareVersion(const char *version);
#endif /* CONFIG_MODEM_HL7800 */
int awsSetShadowAppFirmwareVersion(const char *version);
int awsPublishBl654SensorData(float temperature, float humidity,
			      float pressure);
#if defined(CONFIG_BOARD_MG100)
int awsPublishPinnacleData(int radioRssi, int radioSinr,
			   struct battery_data *battery,
			   struct motion_status *motion,
			   struct sdcard_status *sdcard);
#elif defined(CONFIG_BOARD_PINNACLE_100_DVK)
int awsPublishPinnacleData(int radioRssi, int radioSinr);
#endif
int awsSubscribe(uint8_t *topic, uint8_t subscribe);
int awsGetShadow(void);
int awsGetAcceptedSubscribe(void);
int awsGetAcceptedUnsub(void);
void awsGenerateGatewayTopics(const char *id);
void awsDisconnectCallback(void);
char *awsGetGatewayUpdateDeltaTopic(void);
struct mqtt_client *awsGetMqttClient(void);

#ifdef __cplusplus
}
#endif

#endif /* __AWS_H__ */
