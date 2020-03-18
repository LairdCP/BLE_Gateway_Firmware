/*
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __AWS_H__
#define __AWS_H__

#include <json.h>

#define CA_TAG 1
#define DEVICE_CERT_TAG 2

/* AWS Root CA obtained from 
*  https://docs.aws.amazon.com/iot/latest/developerguide/managing-device-certs.html 
*/
static const unsigned char aws_root_ca[] =
	"-----BEGIN CERTIFICATE-----\n"
	"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
	"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
	"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
	"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
	"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
	"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
	"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
	"IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
	"VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
	"93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
	"jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
	"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
	"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
	"U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
	"N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
	"o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
	"5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
	"rqXRfboQnoZsG4q5WTP468SQvvG5\n"
	"-----END CERTIFICATE-----\n";

#define AWS_DEFAULT_ENDPOINT "a3273rvo818l4w-ats.iot.us-east-1.amazonaws.com"

#define SERVER_PORT_STR "8883"

#define APP_SLEEP_MSECS 500

#define PUBLISH_TIMEOUT_TICKS K_SECONDS(5)

#define DNS_RETRIES 1
#define APP_CONNECT_TRIES 1

#define APP_MQTT_BUFFER_SIZE 1024

#define DEFAULT_MQTT_CLIENTID "mg100"

#define SHADOW_STATE_NULL "{\"state\":null}"

#define SHADOW_REPORTED_START "{\"state\":{\"reported\":{"
#define SHADOW_REPORTED_END "}}}"

#define SHADOW_TEMPERATURE "\"temperature\":"
#define SHADOW_HUMIDITY "\"humidity\":"
#define SHADOW_PRESSURE "\"pressure\":"
#define SHADOW_RADIO_RSSI "\"radio_rssi\":"
#define SHADOW_RADIO_SINR "\"radio_sinr\":"

#define AWS_RX_THREAD_STACK_SIZE 4096
#define AWS_RX_THREAD_PRIORITY K_PRIO_COOP(15)

#define GATEWAY_TOPIC NULL

struct shadow_persistent_values {
	const char *firmware_version;
	const char *os_version;
	const char *radio_version;
	const char *IMEI;
	const char *ICCID;
	const char *radio_sn;
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
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, radio_version,
			    JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, IMEI,
			    JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, ICCID,
			    JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct shadow_persistent_values, radio_sn,
			    JSON_TOK_STRING),
};

static const struct json_obj_descr shadow_state_reported_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct shadow_state_reported, reported,
			      shadow_persistent_values_descr),
};

static const struct json_obj_descr shadow_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct shadow_reported_struct, state,
			      shadow_state_reported_descr),
};

int awsInit(void);
int awsSetCredentials(const u8_t *cert, const u8_t *key);
void awsSetRootCa(const char *cred);
void awsSetEndpoint(const char *ep);
void awsSetClientId(const char *id);
int awsGetServerAddr(void);
int awsConnect(void);
bool awsConnected(void);
void awsDisconnect(void);
int awsKeepAlive(void);
int awsSendData(char *data, u8_t *topic);
int awsPublishShadowPersistentData(void);
int awsSetShadowKernelVersion(const char *version);
int awsSetShadowIMEI(const char *imei);
int awsSetShadowICCID(const char *iccid);
int awsSetShadowRadioSerialNumber(const char *sn);
int awsSetShadowRadioFirmwareVersion(const char *version);
int awsSetShadowAppFirmwareVersion(const char *version);
int awsPublishBl654SensorData(float temperature, float humidity,
			      float pressure);
int awsPublishPinnacleData(int radioRssi, int radioSinr);
int awsSubscribe(void);

#endif /* __AWS_H__ */
