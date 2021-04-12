/**
 * @file ble_aws_service.c
 * @brief BLE AWS Service
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(ble_aws_service, CONFIG_BLE_AWS_SERVICE_LOG_LEVEL);

#define AWS_SVC_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define AWS_SVC_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define AWS_SVC_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define AWS_SVC_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <errno.h>
#include <mbedtls/sha256.h>

#ifdef CONFIG_APP_AWS_CUSTOMIZATION
#include <fs/fs.h>
#endif

#ifdef CONFIG_CONTACT_TRACING
#include "ct_ble.h"
#endif

#include "ble_aws_service.h"
#include "nv.h"
#include "aws.h"
#include "lcz_bluetooth.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
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

#define SHA256_SIZE 32

#define AWS_DEFAULT_TOPIC_PREFIX "mg100-ct/dev/gw/"

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void aws_svc_connected(struct bt_conn *conn, uint8_t err);
static void aws_svc_disconnected(struct bt_conn *conn, uint8_t reason);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static char client_id_value[AWS_CLIENT_ID_MAX_LENGTH + 1];
static char endpoint_value[AWS_ENDPOINT_MAX_LENGTH + 1];
static char root_ca_value[AWS_ROOT_CA_MAX_LENGTH + 1];
static uint8_t root_ca_sha256[SHA256_SIZE];
static char client_cert_value[AWS_CLIENT_CERT_MAX_LENGTH + 1];
static uint8_t client_cert_sha256[SHA256_SIZE];
static char client_key_value[AWS_CLIENT_KEY_MAX_LENGTH + 1];
static uint8_t client_key_sha256[SHA256_SIZE];
static char topic_prefix_value[AWS_TOPIC_PREFIX_MAX_LENGTH + 1];

static uint8_t save_clear_value;

static uint8_t status_notify;
static enum aws_status status_value;

static bool isClientCertStored = false;
static bool isClientKeyStored = false;
static uint32_t lastCredOffset;

static uint16_t svc_status_index;

static struct bt_conn *aws_svc_conn = NULL;

static struct bt_conn_cb aws_svc_conn_callbacks = {
	.connected = aws_svc_connected,
	.disconnected = aws_svc_disconnected,
};

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
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
			      uint16_t len, uint16_t offset)
{
	uint16_t valueLen;

	const char *value = attr->user_data;
	valueLen = strlen(value);
	if (valueLen > AWS_CLIENT_ID_MAX_LENGTH) {
		valueLen = AWS_CLIENT_ID_MAX_LENGTH;
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, valueLen);
}

static ssize_t write_client_id(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, const void *buf,
			       uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;

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
			      uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;

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
			     uint16_t len, uint16_t offset)
{
	uint16_t valueLen;

	const char *value = attr->user_data;
	valueLen = strlen(value);
	if (valueLen > AWS_ENDPOINT_MAX_LENGTH) {
		valueLen = AWS_ENDPOINT_MAX_LENGTH;
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, valueLen);
}

static ssize_t write_credential(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset,
				uint8_t flags)
{
	char *value = attr->user_data;
	uint32_t credOffset = 0;
	uint8_t *data = (uint8_t *)buf;
	uint16_t credMaxSize;

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
		(uint32_t)value, offset, (len - AWS_CREDENTIAL_HEADER_SIZE),
		credOffset);

	if (offset == 0) {
		/* This was not a long write.
        *  skip first 4 bytes of data (address offest) and adjust
        *  length by 4 bytes */
		memcpy(value + offset + credOffset,
		       (data + AWS_CREDENTIAL_HEADER_SIZE),
		       (len - AWS_CREDENTIAL_HEADER_SIZE));
		/* null terminate the value that was written */
		*(value + offset + credOffset +
		  (len - AWS_CREDENTIAL_HEADER_SIZE)) = 0;

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
			    uint16_t len, uint16_t offset)
{
	mbedtls_sha256(root_ca_value, strlen(root_ca_value), root_ca_sha256,
		       false);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &root_ca_sha256,
				 sizeof(root_ca_sha256));
}

static ssize_t read_client_cert(struct bt_conn *conn,
				const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset)
{
	mbedtls_sha256(client_cert_value, strlen(client_cert_value),
		       client_cert_sha256, false);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &client_cert_sha256,
				 sizeof(client_cert_sha256));
}

static ssize_t read_client_key(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	mbedtls_sha256(client_key_value, strlen(client_key_value),
		       client_key_sha256, false);
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &client_key_sha256, sizeof(client_key_sha256));
}

static ssize_t write_save_clear(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset,
				uint8_t flags)
{
	uint8_t *value = attr->user_data;

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
		awsSvcEvent(AWS_SVC_EVENT_SETTINGS_SAVED);
	} else if (save_clear_value == CLEAR_SETTINGS) {
		aws_svc_save_clear_settings(false);
		awsSvcEvent(AWS_SVC_EVENT_SETTINGS_CLEARED);
	}

	return len;
}

static ssize_t read_status(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 sizeof(status_value));
}

static void status_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	status_notify = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

/******************************************************************************/
/* AWS Service Declaration                                                    */
/******************************************************************************/
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
	BT_GATT_CCC(status_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
};

static struct bt_gatt_service aws_svc = BT_GATT_SERVICE(aws_attrs);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
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

void aws_svc_set_root_ca_partial(const char *cred, int offset, int len)
{
	if (cred && (offset + len < sizeof(root_ca_value))) {
		if (offset == 0) {
			memset(root_ca_value, 0, sizeof(root_ca_value));
		}
		strncpy(&root_ca_value[offset], cred, len);
	}
}

void aws_svc_set_client_cert_partial(const char *cred, int offset, int len)
{
	if (cred && (offset + len < sizeof(client_cert_value))) {
		if (offset == 0) {
			memset(client_cert_value, 0, sizeof(client_cert_value));
		}
		strncpy(&client_cert_value[offset], cred, len);
	}
}

void aws_svc_set_client_key_partial(const char *cred, int offset, int len)
{
	if (cred && (offset + len < sizeof(client_key_value))) {
		if (offset == 0) {
			memset(client_key_value, 0, sizeof(client_key_value));
		}
		strncpy(&client_key_value[offset], cred, len);
	}
}

void aws_svc_set_topic_prefix(const char *prefix)
{
	if (prefix) {
		strncpy(topic_prefix_value, prefix, sizeof(topic_prefix_value));
		AWS_SVC_LOG_DBG("Set topic prefix: %s", topic_prefix_value);
#ifdef CONFIG_CONTACT_TRACING
		ct_ble_topic_builder();
#endif
	}
}

const char *aws_svc_get_topic_prefix(void)
{
	return topic_prefix_value;
}

void aws_svc_set_status(enum aws_status status)
{
	bool notify = false;

	if (status != status_value) {
		notify = true;
		status_value = status;
	}
	if ((aws_svc_conn != NULL) && status_notify && notify) {
		bt_gatt_notify(aws_svc_conn, &aws_svc.attrs[svc_status_index],
			       &status, sizeof(status));
	}
}

#ifdef CONFIG_APP_AWS_CUSTOMIZATION
static int read_cred_from_fs(char *file_path, uint8_t *dst, size_t dst_size)
{
	int rc;
	struct fs_dirent file_info;
	struct fs_file_t file;

	/* get file info */
	rc = fs_stat(file_path, &file_info);
	if (rc >= 0) {
		AWS_SVC_LOG_DBG("file '%s' size %u", log_strdup(file_info.name),
				file_info.size);
	} else {
		AWS_SVC_LOG_ERR("Failed to get file [%s] info: %d",
				log_strdup(file_path), rc);
		goto done;
	}

	if (file_info.size > dst_size) {
		AWS_SVC_LOG_ERR("File too large src: %d dst: %d",
				file_info.size, dst_size);
		rc = AWS_SVC_ERR_CRED_SIZE;
		goto done;
	}

	rc = fs_open(&file, file_path, FS_O_READ);
	if (rc < 0) {
		AWS_SVC_LOG_ERR("%s open err: %d", log_strdup(file_path), rc);
		goto done;
	}

	rc = fs_read(&file, dst, dst_size);
	if (rc < 0) {
		AWS_SVC_LOG_ERR("could not read %s [%d]", log_strdup(file_path),
				rc);
	} else if (rc < file_info.size) {
		AWS_SVC_LOG_WRN("Did not read entire file %s",
				log_strdup(file_path));
	}

	fs_close(&file);

done:
	return rc;
}
#endif /* CONFIG_APP_AWS_CUSTOMIZATION */

int aws_svc_init(const char *clientId)
{
	int rc = AWS_SVC_ERR_NONE;
#ifdef CONFIG_APP_AWS_CUSTOMIZATION
	bool read_from_fs = false;
#endif

	rc = nvReadAwsEndpoint(endpoint_value, sizeof(endpoint_value));
	if (rc <= 0) {
		/* Setting does not exist, init it */
		rc = nvStoreAwsEndpoint(AWS_DEFAULT_ENDPOINT,
					strlen(AWS_DEFAULT_ENDPOINT) + 1);
		if (rc <= 0) {
			AWS_SVC_LOG_ERR("Could not write AWS endpoint (%d)",
					rc);
			rc = AWS_SVC_ERR_INIT_ENDPOINT;
			goto done;
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
			rc = AWS_SVC_ERR_INIT_CLIENT_ID;
			goto done;
		}
	}
	awsSetClientId(client_id_value);

#ifdef CONFIG_APP_AWS_CUSTOMIZATION
	rc = nvReadAwsEnableCustom(&read_from_fs);
	if (rc <= 0) {
		AWS_SVC_LOG_ERR("Could not read setting (%d)", rc);
		rc = AWS_SVC_ERR_READ_CRED_FS;
		goto done;
	}

	if (read_from_fs) {
		AWS_SVC_LOG_INF("Reading credentials from file system");

		read_cred_from_fs("/lfs/" CONFIG_APP_AWS_ROOT_CA_FILE_NAME,
				  root_ca_value, sizeof(root_ca_value));
		awsSetRootCa(root_ca_value);

		rc = read_cred_from_fs(
			"/lfs/" CONFIG_APP_AWS_CLIENT_CERT_FILE_NAME,
			client_cert_value, sizeof(client_cert_value));
		if (rc >= 0) {
			isClientCertStored = true;
		}

		rc = read_cred_from_fs(
			"/lfs/" CONFIG_APP_AWS_CLIENT_KEY_FILE_NAME,
			client_key_value, sizeof(client_key_value));
		if (rc >= 0) {
			isClientKeyStored = true;
		}

	} else
#endif /* CONFIG_APP_AWS_CUSTOMIZATION */
	{
		rc = nvReadAwsRootCa(root_ca_value, sizeof(root_ca_value));
		if (rc <= 0) {
			/* Setting does not exist, init it */
			rc = nvStoreAwsRootCa((uint8_t *)aws_root_ca,
					      strlen(aws_root_ca) + 1);
			if (rc <= 0) {
				AWS_SVC_LOG_ERR(
					"Could not write AWS client ID (%d)",
					rc);
				rc = AWS_SVC_ERR_INIT_CLIENT_ID;
				goto done;
			}
			aws_svc_set_root_ca(aws_root_ca);
		}
		awsSetRootCa(root_ca_value);

		rc = nvReadDevCert(client_cert_value,
				   sizeof(client_cert_value));
		if (rc > 0) {
			isClientCertStored = true;
		}

		rc = nvReadDevKey(client_key_value, sizeof(client_key_value));
		if (rc > 0) {
			isClientKeyStored = true;
		}

#if CONFIG_CONTACT_TRACING
		rc = nvReadAwsTopicPrefix(topic_prefix_value,
					  sizeof(topic_prefix_value));
		if (rc <= 0) {
			/* Setting does not exist, init it */
			rc = nvStoreAwsTopicPrefix(
				AWS_DEFAULT_TOPIC_PREFIX,
				strlen(AWS_DEFAULT_TOPIC_PREFIX) + 1);
			if (rc <= 0) {
				AWS_SVC_LOG_ERR(
					"Could not write AWS topic prefix (%d)",
					rc);
				return AWS_SVC_ERR_INIT_TOPIC_PREFIX;
			}
			aws_svc_set_topic_prefix(AWS_DEFAULT_TOPIC_PREFIX);
		}
#endif
	}

	bt_gatt_service_register(&aws_svc);

	size_t gatt_size = (sizeof(aws_attrs) / sizeof(aws_attrs[0]));
	svc_status_index = lbt_find_gatt_index(&aws_status_uuid.uuid, aws_attrs,
					       gatt_size);

	bt_conn_cb_register(&aws_svc_conn_callbacks);

	rc = AWS_SVC_ERR_NONE;
done:
	return rc;
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
#ifdef CONFIG_CONTACT_TRACING
		rc = nvStoreAwsTopicPrefix(topic_prefix_value,
					   strlen(topic_prefix_value) + 1);
		if (rc < 0) {
			goto exit;
		}
#endif
		AWS_SVC_LOG_INF("Saved AWS settings");
	} else {
		AWS_SVC_LOG_INF("Cleared AWS settings");
		nvDeleteAwsClientId();
		nvDeleteAwsEndpoint();
		nvDeleteAwsRootCa();
		nvDeleteDevCert();
		nvDeleteDevKey();
#ifdef CONFIG_CONTACT_TRACING
		nvStoreAwsTopicPrefix(AWS_DEFAULT_TOPIC_PREFIX,
				      strlen(AWS_DEFAULT_TOPIC_PREFIX) + 1);
#endif
		isClientCertStored = true;
		isClientKeyStored = true;
	}
exit:
	return rc;
}

static void aws_svc_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		return;
	}

	if (!lbt_peripheral_role(conn)) {
		return;
	}

	aws_svc_conn = bt_conn_ref(conn);
}

static void aws_svc_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (!lbt_peripheral_role(conn)) {
		return;
	}

	if (aws_svc_conn) {
		bt_conn_unref(aws_svc_conn);
		aws_svc_conn = NULL;
	}
}

__weak void awsSvcEvent(enum aws_svc_event event)
{
	ARG_UNUSED(event);

	return;
}