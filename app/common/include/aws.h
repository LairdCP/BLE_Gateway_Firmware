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
#include <net/mqtt.h>

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

#define AWS_RX_THREAD_STACK_SIZE 2048
#define AWS_RX_THREAD_PRIORITY K_PRIO_COOP(15)

#define GATEWAY_TOPIC NULL

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
int awsInit(void);
int awsGetServerAddr(void);
int awsConnect(void);
bool awsConnected(void);
bool awsPublished(void);
int awsDisconnect(void);
int awsSendData(char *data, uint8_t *topic);
int awsSendBinData(char *data, uint32_t len, uint8_t *topic);
int awsPublishShadowPersistentData(void);
int awsPublishBl654SensorData(float temperature, float humidity,
			      float pressure);
int awsPublishHeartbeat(void);
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
