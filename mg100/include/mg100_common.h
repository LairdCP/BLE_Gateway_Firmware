/**
 * @file mg100_common.h
 * @brief Configuration and utility functions for the Out-of-Box demo.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __MG100_COMMON_H__
#define __MG100_COMMON_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Application Firmware Version                                               */
/******************************************************************************/
#define APP_VERSION_MAJOR 2
#define APP_VERSION_MINOR 0
#define APP_VERSION_PATCH 0
#define APP_VERSION_STRING                                                     \
	STRINGIFY(APP_VERSION_MAJOR)                                           \
	"." STRINGIFY(APP_VERSION_MINOR) "." STRINGIFY(APP_VERSION_PATCH)

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define WAIT_TIME_BEFORE_RETRY_TICKS K_SECONDS(10)

/* Green LED is turned on when connected to AWS.
 * Green LED is flashed off when data is sent.
 */
#define DATA_SEND_LED_ON_TIME_TICKS K_MSEC(60)
#define DATA_SEND_LED_OFF_TIME_TICKS K_MSEC(30)

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

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
void strncpy_replace_underscore_with_space(char *restrict s1,
					   const char *restrict s2,
					   size_t size);

char *replace_word(const char *s, const char *oldW, const char *newW,
		   char *dest, int destSize);

char *strncat_max(char *d1, const char *s1, size_t max_str_len);

/* Prints list of threads using k_thread_foreach */
void print_thread_list(void);

void print_json(const char *fmt, size_t size, const char *buffer);

#ifdef __cplusplus
}
#endif

#endif /* __MG100_COMMON_H__ */
