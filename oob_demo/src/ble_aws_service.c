/* ble_aws_service.c - BLE AWS Service
 *
 * Copyright (c) 2019 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(oob_aws_svc);

#include <errno.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <mbedtls/sha256.h>

#include "ble_aws_service.h"
#include "nv.h"
#include "aws.h"
#include "laird_bluetooth.h"
#include "oob_common.h"

#define AWS_SVC_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define AWS_SVC_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define AWS_SVC_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define AWS_SVC_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

enum { SAVE_SETTINGS = 1, CLEAR_SETTINGS };

static struct bt_uuid_128 aws_svc_uuid =
	BT_UUID_INIT_128(0xb5, 0xa9, 0x34, 0xf2, 0x59, 0x7c, 0xd7, 0xbc, 0x14,
			 0x4a, 0xa9, 0x55, 0xf0, 0x03, 0x72, 0xae);

static struct bt_uuid_128 aws_cliend_id_uuid =
	BT_UUID_INIT_128(0xb5, 0xa9, 0x34, 0xf2, 0x59, 0x7c, 0xd7, 0xbc, 0x14,
			 0x4a, 0xa9, 0x55, 0xf1, 0x03, 0x72, 0xae);

static struct bt_uuid_128 aws_endpoint_uuid =
	BT_UUID_INIT_128(0xb5, 0xa9, 0x34, 0xf2, 0x59, 0x7c, 0xd7, 0xbc, 0x14,
			 0x4a, 0xa9, 0x55, 0xf2, 0x03, 0x72, 0xae);

static struct bt_uuid_128 aws_root_ca_uuid =
	BT_UUID_INIT_128(0xb5, 0xa9, 0x34, 0xf2, 0x59, 0x7c, 0xd7, 0xbc, 0x14,
			 0x4a, 0xa9, 0x55, 0xf3, 0x03, 0x72, 0xae);

static struct bt_uuid_128 aws_client_cert_uuid =
	BT_UUID_INIT_128(0xb5, 0xa9, 0x34, 0xf2, 0x59, 0x7c, 0xd7, 0xbc, 0x14,
			 0x4a, 0xa9, 0x55, 0xf4, 0x03, 0x72, 0xae);

static struct bt_uuid_128 aws_client_key_uuid =
	BT_UUID_INIT_128(0xb5, 0xa9, 0x34, 0xf2, 0x59, 0x7c, 0xd7, 0xbc, 0x14,
			 0x4a, 0xa9, 0x55, 0xf5, 0x03, 0x72, 0xae);

static struct bt_uuid_128 aws_save_clear_uuid =
	BT_UUID_INIT_128(0xb5, 0xa9, 0x34, 0xf2, 0x59, 0x7c, 0xd7, 0xbc, 0x14,
			 0x4a, 0xa9, 0x55, 0xf6, 0x03, 0x72, 0xae);

static struct bt_uuid_128 aws_status_uuid =
	BT_UUID_INIT_128(0xb5, 0xa9, 0x34, 0xf2, 0x59, 0x7c, 0xd7, 0xbc, 0x14,
			 0x4a, 0xa9, 0x55, 0xf7, 0x03, 0x72, 0xae);

static char client_id_value[AWS_CLIENT_ID_MAX_LENGTH + 1];
static char endpoint_value[AWS_ENDPOINT_MAX_LENGTH + 1];
static char root_ca_value[AWS_ROOT_CA_MAX_LENGTH + 1];
static u8_t root_ca_sha256[SHA256_SIZE];
static char client_cert_value[AWS_CLIENT_CERT_MAX_LENGTH + 1];
static u8_t client_cert_sha256[SHA256_SIZE];
static char client_key_value[AWS_CLIENT_KEY_MAX_LENGTH + 1];
static u8_t client_key_sha256[SHA256_SIZE];

static u8_t save_clear_value;

static struct bt_gatt_ccc_cfg status_ccc_cfg[BT_GATT_CCC_MAX] = {};
static u8_t status_notify;
static enum aws_status status_value;

static bool isClientCertStored = false;
static bool isClientKeyStored = false;
static u32_t lastCredOffset;

static u16_t svc_status_index;

static aws_svc_event_function_t eventCallbackFunc = NULL;

static void onAwsSvcEvent(enum aws_svc_event event)
{
	if (eventCallbackFunc != NULL) {
		eventCallbackFunc(event);
	}
}

static bool isCommissioned(void)
{
	if (status_value == AWS_STATUS_NOT_PROVISIONED) {
		return false;
	} else {
		return true;
	}
}

static ssize_t read_client_id(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, void *buf,
			      u16_t len, u16_t offset)
{
	u16_t valueLen;

	const char *value = attr->user_data;
	valueLen = strlen(value);
	if (valueLen > AWS_CLIENT_ID_MAX_LENGTH) {
		valueLen = AWS_CLIENT_ID_MAX_LENGTH;
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, valueLen);
}

static ssize_t write_client_id(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, const void *buf,
			       u16_t len, u16_t offset, u8_t flags)
{
	u8_t *value = attr->user_data;

	if (isCommissioned()) {
		/* if we are commissioned, do not allow writing */
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
	}

	if (offset + len > (sizeof(client_id_value) - 1)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	/* null terminate the value that was written */
	*(value + offset + len) = 0;

	return len;
}

static ssize_t write_endpoint(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, const void *buf,
			      u16_t len, u16_t offset, u8_t flags)
{
	u8_t *value = attr->user_data;

	if (isCommissioned()) {
		/* if we are commissioned, do not allow writing */
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
	}

	if (offset + len > (sizeof(endpoint_value) - 1)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	/* null terminate the value that was written */
	*(value + offset + len) = 0;

	return len;
}

static ssize_t read_endpoint(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr, void *buf,
			     u16_t len, u16_t offset)
{
	u16_t valueLen;

	const char *value = attr->user_data;
	valueLen = strlen(value);
	if (valueLen > AWS_ENDPOINT_MAX_LENGTH) {
		valueLen = AWS_ENDPOINT_MAX_LENGTH;
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, valueLen);
}

static ssize_t write_credential(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf, u16_t len, u16_t offset,
				u8_t flags)
{
	char *value = attr->user_data;
	u32_t credOffset = 0;
	u8_t *data = (u8_t *)buf;
	u16_t credMaxSize;

	if (isCommissioned()) {
		/* if we are commissioned, do not allow writing */
		AWS_SVC_LOG_ERR(
			"Write not permitted, device is already commissioned");
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
	}

	if (len <= 4) {
		/* There must be a least 5 bytes to contain valid data */
		AWS_SVC_LOG_ERR(
			"Invalid length, data must be at least 5 bytes (4 byte offset + 1 byte data)");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_PDU);
	}

	if (value == root_ca_value) {
		credMaxSize = sizeof(root_ca_value) - 1;
	} else if (value == client_cert_value) {
		credMaxSize = sizeof(client_cert_value) - 1;
	} else if (value == client_key_value) {
		credMaxSize = sizeof(client_key_value) - 1;
	} else {
		credMaxSize = 0;
	}

	if (offset == 0) {
		credOffset = data[3] << 24;
		credOffset |= data[2] << 16;
		credOffset |= data[1] << 8;
		credOffset |= data[0];
		lastCredOffset = credOffset;
	} else {
		credOffset = lastCredOffset;
	}

	if ((value + offset + credOffset - value) > credMaxSize) {
		AWS_SVC_LOG_ERR(
			"Invalid offset, data will overrun destination");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	AWS_SVC_LOG_DBG(
		"Writing cred to 0x%08x, offset 0x%04x, len: %d, cred offset 0x%08x",
		(u32_t)value, offset, len, credOffset);

	if (offset == 0) {
		/* This was not a long write.
        *  skip first 4 bytes of data (address offest) and adjust 
        *  length by 4 bytes */
		memcpy(value + offset + credOffset, data + 4, len - 4);
		/* null terminate the value that was written */
		*(value + offset + credOffset + (len - 4)) = 0;

	} else {
		/* This was a long write, the data did not contain a credOffset */
		memcpy(value + offset + credOffset, data, len);
		/* null terminate the value that was written */
		*(value + offset + credOffset + len) = 0;
	}

	return len;
}

static ssize_t read_root_ca(struct bt_conn *conn,
			    const struct bt_gatt_attr *attr, void *buf,
			    u16_t len, u16_t offset)
{
	mbedtls_sha256(root_ca_value, strlen(root_ca_value), root_ca_sha256,
		       false);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &root_ca_sha256,
				 sizeof(root_ca_sha256));
}

static ssize_t read_client_cert(struct bt_conn *conn,
				const struct bt_gatt_attr *attr, void *buf,
				u16_t len, u16_t offset)
{
	mbedtls_sha256(client_cert_value, strlen(client_cert_value),
		       client_cert_sha256, false);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &client_cert_sha256,
				 sizeof(client_cert_sha256));
}

static ssize_t read_client_key(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, void *buf,
			       u16_t len, u16_t offset)
{
	mbedtls_sha256(client_key_value, strlen(client_key_value),
		       client_key_sha256, false);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &client_key_sha256, sizeof(client_key_sha256));
}

static ssize_t write_save_clear(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf, u16_t len, u16_t offset,
				u8_t flags)
{
	u8_t *value = attr->user_data;

	if (offset + len > sizeof(save_clear_value)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);

	if (save_clear_value == SAVE_SETTINGS) {
		if (isCommissioned()) {
			/* if we are commissioned, do not allow writing */
			return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
		}
		aws_svc_save_clear_settings(true);
		onAwsSvcEvent(AWS_SVC_EVENT_SETTINGS_SAVED);
	} else if (save_clear_value == CLEAR_SETTINGS) {
		aws_svc_save_clear_settings(false);
		onAwsSvcEvent(AWS_SVC_EVENT_SETTINGS_CLEARED);
	}

	return len;
}

static ssize_t read_status(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, void *buf,
			   u16_t len, u16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 sizeof(status_value));
}

static void status_cfg_changed(const struct bt_gatt_attr *attr, u16_t value)
{
	status_notify = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

/* AWS Service Declaration */
static struct bt_gatt_attr aws_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&aws_svc_uuid),
	BT_GATT_CHARACTERISTIC(&aws_cliend_id_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_client_id, write_client_id,
			       client_id_value),
	BT_GATT_CHARACTERISTIC(&aws_endpoint_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_endpoint, write_endpoint, endpoint_value),
	BT_GATT_CHARACTERISTIC(&aws_root_ca_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_root_ca, write_credential, root_ca_value),
	BT_GATT_CHARACTERISTIC(&aws_client_cert_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_client_cert, write_credential,
			       client_cert_value),
	BT_GATT_CHARACTERISTIC(&aws_client_key_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_client_key, write_credential,
			       client_key_value),
	BT_GATT_CHARACTERISTIC(&aws_save_clear_uuid.uuid, BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE, NULL, write_save_clear,
			       &save_clear_value),
	BT_GATT_CHARACTERISTIC(
		&aws_status_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, read_status, NULL, &status_value),
	BT_GATT_CCC(status_ccc_cfg, status_cfg_changed),
};

static struct bt_gatt_service aws_svc = BT_GATT_SERVICE(aws_attrs);

void aws_svc_set_client_id(const char *id)
{
	if (id) {
		strncpy(client_id_value, id, sizeof(client_id_value));
	}
}

void aws_svc_set_endpoint(const char *ep)
{
	if (ep) {
		strncpy(endpoint_value, ep, sizeof(endpoint_value));
	}
}

void aws_svc_set_root_ca(const char *cred)
{
	if (cred) {
		strncpy(root_ca_value, cred, sizeof(root_ca_value));
	}
}

void aws_svc_set_client_cert(const char *cred)
{
	if (cred) {
		strncpy(client_cert_value, cred, sizeof(client_cert_value));
	}
}

void aws_svc_set_client_key(const char *cred)
{
	if (cred) {
		strncpy(client_key_value, cred, sizeof(client_key_value));
	}
}

void aws_svc_set_status(struct bt_conn *conn, enum aws_status status)
{
	bool notify = false;

	if (status != status_value) {
		notify = true;
		status_value = status;
	}
	if ((conn != NULL) && status_notify && notify) {
		bt_gatt_notify(conn, &aws_svc.attrs[svc_status_index],
			       &status, sizeof(status));
	}
}

int aws_svc_init(const char *clientId)
{
	int rc;

	rc = nvReadAwsEndpoint(endpoint_value, sizeof(endpoint_value));
	if (rc <= 0) {
		/* Setting does not exist, init it */
		rc = nvStoreAwsEndpoint(AWS_DEFAULT_ENDPOINT,
					strlen(AWS_DEFAULT_ENDPOINT) + 1);
		if (rc <= 0) {
			AWS_SVC_LOG_ERR("Could not write AWS endpoint (%d)",
					rc);
			return AWS_SVC_ERR_INIT_ENDPOINT;
		}
		aws_svc_set_endpoint(AWS_DEFAULT_ENDPOINT);
	}
	awsSetEndpoint(endpoint_value);

	rc = nvReadAwsClientId(client_id_value, sizeof(client_id_value));
	if (rc <= 0) {
		/* Setting does not exist, init it */
		snprintk(client_id_value, sizeof(client_id_value), "%s_%s",
			 DEFAULT_MQTT_CLIENTID, clientId);

		rc = nvStoreAwsClientId(client_id_value,
					strlen(client_id_value) + 1);
		if (rc <= 0) {
			AWS_SVC_LOG_ERR("Could not write AWS client ID (%d)",
					rc);
			return AWS_SVC_ERR_INIT_CLIENT_ID;
		}
	}
	awsSetClientId(client_id_value);

	rc = nvReadAwsRootCa(root_ca_value, sizeof(root_ca_value));
	if (rc <= 0) {
		/* Setting does not exist, init it */
		rc = nvStoreAwsRootCa((u8_t *)aws_root_ca,
				      strlen(aws_root_ca) + 1);
		if (rc <= 0) {
			AWS_SVC_LOG_ERR("Could not write AWS client ID (%d)",
					rc);
			return AWS_SVC_ERR_INIT_CLIENT_ID;
		}
		aws_svc_set_root_ca(aws_root_ca);
	}
	awsSetRootCa(root_ca_value);

	rc = nvReadDevCert(client_cert_value, sizeof(client_cert_value));
	if (rc > 0) {
		isClientCertStored = true;
	}

	rc = nvReadDevKey(client_key_value, sizeof(client_key_value));
	if (rc > 0) {
		isClientKeyStored = true;
	}

	bt_gatt_service_register(&aws_svc);

	size_t gatt_size = (sizeof(aws_attrs)/sizeof(aws_attrs[0]));
	svc_status_index = lbt_find_gatt_index(&aws_status_uuid.uuid,
					       aws_attrs, gatt_size);

	return AWS_SVC_ERR_NONE;
}

bool aws_svc_client_cert_is_stored(void)
{
	return isClientCertStored;
}

bool aws_svc_client_key_is_stored(void)
{
	return isClientKeyStored;
}

const char *aws_svc_get_client_cert(void)
{
	return (const char *)client_cert_value;
}

const char *aws_svc_get_client_key(void)
{
	return (const char *)client_key_value;
}

int aws_svc_save_clear_settings(bool save)
{
	int rc = 0;

	if (save) {
		rc = nvStoreAwsEndpoint(endpoint_value,
					strlen(endpoint_value) + 1);
		if (rc < 0) {
			goto exit;
		}
		rc = nvStoreAwsClientId(client_id_value,
					strlen(client_id_value) + 1);
		if (rc < 0) {
			goto exit;
		}
		rc = nvStoreAwsRootCa(root_ca_value, strlen(root_ca_value) + 1);
		if (rc < 0) {
			goto exit;
		}
		rc = nvStoreDevCert(client_cert_value,
				    strlen(client_cert_value) + 1);
		if (rc < 0) {
			goto exit;
		} else if (rc > 0) {
			isClientCertStored = true;
		}
		rc = nvStoreDevKey(client_key_value,
				   strlen(client_key_value) + 1);
		if (rc < 0) {
			goto exit;
		} else if (rc > 0) {
			isClientKeyStored = true;
		}
		AWS_SVC_LOG_INF("Saved AWS settings");
	} else {
		AWS_SVC_LOG_INF("Cleared AWS settings");
		nvDeleteAwsClientId();
		nvDeleteAwsEndpoint();
		nvDeleteAwsRootCa();
		nvDeleteDevCert();
		nvDeleteDevKey();
		isClientCertStored = true;
		isClientKeyStored = true;
	}
exit:
	return rc;
}

void aws_svc_set_event_callback(aws_svc_event_function_t func)
{
	eventCallbackFunc = func;
}
