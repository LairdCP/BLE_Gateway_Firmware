/**
 * @file ble_aws_service.h
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLE_AWS_SERVICE_H__
#define __BLE_AWS_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Incluces                                                                   */
/******************************************************************************/
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
/* clang-format off */
#define AWS_CLIENT_ID_MAX_LENGTH     CONFIG_APP_AWS_CLIENT_ID_MAX_LENGTH
#define AWS_ENDPOINT_MAX_LENGTH      CONFIG_APP_AWS_ENDPOINT_MAX_LENGTH
#define AWS_ROOT_CA_MAX_LENGTH       CONFIG_APP_AWS_MAX_CREDENTIAL_SIZE
#define AWS_CLIENT_CERT_MAX_LENGTH   CONFIG_APP_AWS_MAX_CREDENTIAL_SIZE
#define AWS_CLIENT_KEY_MAX_LENGTH    CONFIG_APP_AWS_MAX_CREDENTIAL_SIZE
#define AWS_CREDENTIAL_HEADER_SIZE   4
#define AWS_TOPIC_PREFIX_MAX_LENGTH  256
/* clang-format on */

enum aws_status {
	AWS_STATUS_NOT_PROVISIONED = 0,
	AWS_STATUS_DISCONNECTED,
	AWS_STATUS_CONNECTED,
	AWS_STATUS_CONNECTION_ERR,
	AWS_STATUS_CONNECTING
};

enum aws_svc_err {
	AWS_SVC_ERR_NONE = 0,
	AWS_SVC_ERR_INIT_ENDPOINT = -1,
	AWS_SVC_ERR_INIT_CLIENT_ID = -2,
	AWS_SVC_ERR_INIT_ROOT_CA = -3,
	AWS_SVC_ERR_READ_CRED_FS = -4,
	AWS_SVC_ERR_CRED_SIZE = -5,
	AWS_SVC_ERR_INIT_TOPIC_PREFIX = -6
};

enum aws_svc_event {
	AWS_SVC_EVENT_SETTINGS_SAVED,
	AWS_SVC_EVENT_SETTINGS_CLEARED,
};

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Callback function prototype for AWS service events
 */
typedef void (*aws_svc_event_function_t)(enum aws_svc_event event);

int aws_svc_init(const char *clientId);
void aws_svc_set_client_id(const char *id);
void aws_svc_set_endpoint(const char *ep);
void aws_svc_set_status(enum aws_status status);
void aws_svc_set_root_ca(const char *cred);
void aws_svc_set_client_cert(const char *cred);
void aws_svc_set_client_key(const char *cred);
void aws_svc_set_topic_prefix(const char *prefix);
const char *aws_svc_get_topic_prefix(void);
bool aws_svc_client_cert_is_stored(void);
bool aws_svc_client_key_is_stored(void);
const char *aws_svc_get_client_cert(void);
const char *aws_svc_get_client_key(void);
int aws_svc_save_clear_settings(bool save);

void aws_svc_set_root_ca_partial(const char *cred, int offset, int len);
void aws_svc_set_client_cert_partial(const char *cred, int offset, int len);
void aws_svc_set_client_key_partial(const char *cred, int offset, int len);

/* Override weak implementation in app */
void awsSvcEvent(enum aws_svc_event event);

#ifdef __cplusplus
}
#endif

#endif /* __BLE_AWS_SERVICE_H__ */
