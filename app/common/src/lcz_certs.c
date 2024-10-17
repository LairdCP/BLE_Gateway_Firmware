/**
 * @file lcz_certs.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(certs, CONFIG_LCZ_CERTS_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <net/tls_credentials.h>

#include "attr.h"
#include "file_system_utilities.h"
#include "lcz_certs.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#ifdef CONFIG_LCZ_CERTS_PSK
#define DEVICE_CERT_TYPE TLS_CREDENTIAL_PSK_ID
#define DEVICE_KEY_TYPE TLS_CREDENTIAL_PSK
#else
#define DEVICE_CERT_TYPE TLS_CREDENTIAL_SERVER_CERTIFICATE
#define DEVICE_KEY_TYPE TLS_CREDENTIAL_PRIVATE_KEY
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static char *root_ca;

#if CONFIG_LCZ_CERTS_DEVICE_CERT_TAG != 0
static char *client;
static char *key;
#endif

static bool loaded;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int load_cert_file(attr_index_t idx, char **cert, char *type);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int lcz_certs_load(void)
{
	int r = -EPERM;

	if (loaded) {
		return 0;
	}

	do {
		r = load_cert_file(ATTR_ID_root_ca_name, &root_ca, "Root CA");
		if (r < 0) {
			break;
		}

		r = tls_credential_add(CONFIG_LCZ_CERTS_CA_CERT_TAG,
				       TLS_CREDENTIAL_CA_CERTIFICATE, root_ca,
				       r);
		if (r < 0) {
			LOG_ERR("Failed to register public certificate: %d", r);
			attr_set_signed32(ATTR_ID_cloud_error,
					  CLOUD_ERROR_INIT_ROOT_CA);
			break;
		}

#if CONFIG_LCZ_CERTS_DEVICE_CERT_TAG != 0
		r = load_cert_file(ATTR_ID_client_cert_name, &client,
				   "Client Cert");
		if (r < 0) {
			break;
		}

		r = tls_credential_add(CONFIG_LCZ_CERTS_DEVICE_CERT_TAG,
				       DEVICE_CERT_TYPE, client, r);
		if (r < 0) {
			LOG_ERR("Failed to register device certificate: %d", r);
			attr_set_signed32(ATTR_ID_cloud_error,
					  CLOUD_ERROR_INIT_CLIENT_CERT);
		}

		r = load_cert_file(ATTR_ID_client_key_name, &key, "Key");
		if (r < 0) {
			break;
		}

		r = tls_credential_add(CONFIG_LCZ_CERTS_DEVICE_CERT_TAG,
				       DEVICE_KEY_TYPE, key, r);
		if (r < 0) {
			LOG_ERR("Failed to register device key: %d", r);
			attr_set_signed32(ATTR_ID_cloud_error,
					  CLOUD_ERROR_INIT_CLIENT_KEY);
			return r;
		}
#endif /* CONFIG_LCZ_CERTS_DEVICE_CERT_TAG != 0 */
	} while (0);

	LOG_INF("Certificate Load: %d", r);

	if (r == 0) {
		loaded = true;
		attr_set_signed32(ATTR_ID_cloud_error, CLOUD_ERROR_NONE);
	}

	attr_set_signed32(ATTR_ID_cert_status, r);

	return r;
}

int lcz_certs_unload(void)
{
	int r;

	loaded = false;

	/* Return values are ignored. */

	r = tls_credential_delete(CONFIG_LCZ_CERTS_CA_CERT_TAG,
				  TLS_CREDENTIAL_CA_CERTIFICATE);
	LOG_INF("Root CA delete: %d", r);

#if CONFIG_LCZ_CERTS_DEVICE_CERT_TAG != 0
	r = tls_credential_delete(CONFIG_LCZ_CERTS_DEVICE_CERT_TAG,
				  DEVICE_CERT_TYPE);
	LOG_INF("Device cert delete: %d", r);

	r = tls_credential_delete(CONFIG_LCZ_CERTS_DEVICE_CERT_TAG,
				  DEVICE_KEY_TYPE);
	LOG_INF("Device key delete: %d", r);
#endif

	return 0;
}

bool lcz_certs_loaded(void)
{
	return loaded;
}

int lcz_certs_reload(void)
{
	(void)lcz_certs_unload();
	return lcz_certs_load();
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
/**
 * @brief negative error code, otherwise size of file + 1 (null terminator)
 */
static int load_cert_file(attr_index_t idx, char **cert, char *type)
{
	int r = -EINVAL;
	char *name;
	ssize_t size;

	k_free(*cert);

	do {
		/* The assumes the name isn't changing while loading certs.
		 * Commissioned is used as a control point.
		 */
		name = (char *)attr_get_quasi_static(idx);
		size = fsu_get_file_size_abs(name);
		if (size <= 0) {
			r = -ENOENT;
			LOG_ERR("%s file not found", type);
			attr_set_signed32(ATTR_ID_cloud_error,
					  CLOUD_ERROR_READ_CRED_FS);
			break;
		}

		/* Ensure string is null terminated. */
		*cert = k_calloc(size + 1, sizeof(char));
		if (*cert == NULL) {
			r = -ENOMEM;
			LOG_ERR("Unable to allocate memory for %s", type);
			attr_set_signed32(ATTR_ID_cloud_error,
					  CLOUD_ERROR_CRED_SIZE);
			break;
		}

		r = fsu_read_abs(name, *cert, size);
		if (r != size) {
			r = -EIO;
			LOG_ERR("Unexpected file size");
			attr_set_signed32(ATTR_ID_cloud_error,
					  CLOUD_ERROR_READ_CRED_FS);
			break;
		} else {
			r += 1;
		}
	} while (0);

	LOG_INF("Load %s: %d", type, r);

	return r;
}
