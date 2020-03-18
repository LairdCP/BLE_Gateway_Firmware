/*
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 0
#define APP_VERSION_PATCH 0
#define APP_VERSION_STRING                                                     \
	STRINGIFY(APP_VERSION_MAJOR)                                           \
	"." STRINGIFY(APP_VERSION_MINOR) "." STRINGIFY(APP_VERSION_PATCH)

#define SCAN_FOR_BL654_SENSOR 1
#define SCAN_FOR_BT510 1

#define WAIT_FOR_DISCONNECT_POLL_RATE_TICKS K_SECONDS(1)

#define BL654_SENSOR_SEND_TO_AWS_RATE_TICKS K_SECONDS(90)

/* When using PSM the data rate is controlled by the modem.
 *
 * This is tied to CONFIG_MODEM_HL7800_PSM_PERIODIC_TAU
 * and CONFIG_MODEM_HL7800_PSM_ACTIVE_TIME.
 * It is adjusted for the time to bring the cell network up, connect to AWS, 
 * and time to close the AWS connection.
 */
#define PSM_ENABLED_SEND_DATA_WINDOW_TICKS K_SECONDS(12)

#define PSM_DISABLED_SEND_DATA_RATE_TICKS K_SECONDS(30)

/* Green LED is turned on when connected to AWS.
 * Green LED is turned off when data is sent. 
 */
#define DATA_SEND_LED_OFF_TIME_TICKS K_MSEC(100)
#define SEND_DATA_TO_DISCONNECT_DELAY_TICKS K_SECONDS(1)

#define DEFAULT_LED_ON_TIME_FOR_1_SECOND_BLINK K_MSEC(100)
#define DEFAULT_LED_OFF_TIME_FOR_1_SECOND_BLINK K_MSEC(900)

#define SHA256_SIZE 32

#define JSON_LOG_ENABLED 1
#define JSON_LOG_TOPIC 0
#define JSON_LOG_MQTT_RX_DATA 1

enum SENSOR_TYPES {
	SENSOR_TYPE_TEMPERATURE = 0,
	SENSOR_TYPE_HUMIDITY,
	SENSOR_TYPE_PRESSURE,
	SENSOR_TYPE_DEW_POINT,

	SENSOR_TYPE_MAX
};

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

void strncpy_replace_underscore_with_space(char *restrict s1,
					   const char *restrict s2,
					   size_t size);

char *replace_word(const char *s, const char *oldW, const char *newW,
		   char *dest, int destSize);

char *strncat_max(char *d1, const char *s1, size_t max_str_len);

/* Prints list of threads using k_thread_foreach */
void print_thread_list(void);

void print_json(const char *fmt, size_t size, const char *buffer);
