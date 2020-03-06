/*
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#define APP_VERSION_MAJOR 0
#define APP_VERSION_MINOR 8
#define APP_VERSION_PATCH 0
#define APP_VERSION_STRING                                                     \
	STRINGIFY(APP_VERSION_MAJOR)                                           \
	"." STRINGIFY(APP_VERSION_MINOR) "." STRINGIFY(APP_VERSION_PATCH)

#define DATA_SEND_TIME_SECONDS 30

#define RETRY_AWS_ACTION_TIMEOUT_SECONDS 30

#define DEFAULT_LED_ON_TIME_FOR_1_SECOND_BLINK K_MSEC(100)
#define DEFAULT_LED_OFF_TIME_FOR_1_SECOND_BLINK K_MSEC(900)

#define SHA256_SIZE 32

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
