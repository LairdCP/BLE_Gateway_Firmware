/*
 * Copyright (c) 2019 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BLE_AWS_SERVICE_H__
#define __BLE_AWS_SERVICE_H__

#define AWS_CLIENT_ID_MAX_LENGTH 32
#define AWS_ENDPOINT_MAX_LENGTH 256
#define AWS_ROOT_CA_MAX_LENGTH 2048
#define AWS_CLIENT_CERT_MAX_LENGTH 2048
#define AWS_CLIENT_KEY_MAX_LENGTH 2048

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
};

enum aws_svc_event {
    AWS_SVC_EVENT_SETTINGS_SAVED,
    AWS_SVC_EVENT_SETTINGS_CLEARED,
};

/**
 * @brief Callback function prototype for AWS service events
 * 
 */
typedef void (*aws_svc_event_function_t)(enum aws_svc_event event);

int aws_svc_init(const char *clientId);
void aws_svc_set_client_id(const char *id);
void aws_svc_set_endpoint(const char *ep);
void aws_svc_set_status(struct bt_conn *conn, enum aws_status status);
void aws_svc_set_root_ca(const char *cred);
void aws_svc_set_client_cert(const char *cred);
void aws_svc_set_client_key(const char *cred);
bool aws_svc_client_cert_is_stored(void);
bool aws_svc_client_key_is_stored(void);
const char *aws_svc_get_client_cert(void);
const char *aws_svc_get_client_key(void);
int aws_svc_save_clear_settings(bool save);

/**
 * @brief Set the AWS service callback
 * 
 * @param func Callback function
 */
void aws_svc_set_event_callback(aws_svc_event_function_t func);


#endif