/**
 * @file lcz_certs.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(certs, CONFIG_CERT_LOADER_LOG_LEVEL);

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
static char *root_ca;
static char *client;
static char *key;

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
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

	if (attr_get_uint32(ATTR_ID_commissioned, 0) == 0) {
		attr_set_signed32(ATTR_ID_certStatus, r);
		return r;
	}

	if (loaded) {
		return 0;
	}

	do {
		r = load_cert_file(ATTR_ID_rootCaName, &root_ca, "Root CA");
		if (r < 0) {
			break;
		}

		tls_credential_delete(CONFIG_APP_CA_CERT_TAG,
				      TLS_CREDENTIAL_CA_CERTIFICATE);
		r = tls_credential_add(CONFIG_APP_CA_CERT_TAG,
				       TLS_CREDENTIAL_CA_CERTIFICATE, root_ca,
				       r);
		if (r < 0) {
			LOG_ERR("Failed to register public certificate: %d", r);
			attr_set_signed32(ATTR_ID_cloudError,
					  CLOUD_ERROR_INIT_ROOT_CA);
			break;
		}

		r = load_cert_file(ATTR_ID_clientCertName, &client,
				   "Client Cert");
		if (r < 0) {
			break;
		}

		tls_credential_delete(CONFIG_APP_DEVICE_CERT_TAG,
				      TLS_CREDENTIAL_SERVER_CERTIFICATE);
		r = tls_credential_add(CONFIG_APP_DEVICE_CERT_TAG,
				       TLS_CREDENTIAL_SERVER_CERTIFICATE,
				       client, r);
		if (r < 0) {
			LOG_ERR("Failed to register device certificate: %d", r);
			attr_set_signed32(ATTR_ID_cloudError,
					  CLOUD_ERROR_INIT_CLIENT_CERT);
		}

		r = load_cert_file(ATTR_ID_clientKeyName, &key, "Key");
		if (r < 0) {
			break;
		}

		tls_credential_delete(CONFIG_APP_DEVICE_CERT_TAG,
				      TLS_CREDENTIAL_PRIVATE_KEY);
		r = tls_credential_add(CONFIG_APP_DEVICE_CERT_TAG,
				       TLS_CREDENTIAL_PRIVATE_KEY, key, r);
		if (r < 0) {
			LOG_ERR("Failed to register device key: %d", r);
			attr_set_signed32(ATTR_ID_cloudError,
					  CLOUD_ERROR_INIT_CLIENT_KEY);
			return r;
		}
	} while (0);

	if (r == 0) {
		loaded = true;
		attr_set_signed32(ATTR_ID_cloudError, CLOUD_ERROR_NONE);
	}

	attr_set_signed32(ATTR_ID_certStatus, r);

	return r;
}

int lcz_certs_unload(void)
{
	loaded = false;

	tls_credential_delete(CONFIG_APP_CA_CERT_TAG,
			      TLS_CREDENTIAL_CA_CERTIFICATE);
	tls_credential_delete(CONFIG_APP_DEVICE_CERT_TAG,
			      TLS_CREDENTIAL_SERVER_CERTIFICATE);
	tls_credential_delete(CONFIG_APP_DEVICE_CERT_TAG,
			      TLS_CREDENTIAL_PRIVATE_KEY);

	return 0;
}

bool lcz_certs_loaded(void)
{
	return loaded;
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
	size_t size;

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
			attr_set_signed32(ATTR_ID_cloudError,
					  CLOUD_ERROR_READ_CRED_FS);
			break;
		}

		/* Ensure string is null terminated. */
		*cert = k_calloc(size + 1, sizeof(char));
		if (*cert == NULL) {
			r = -ENOMEM;
			LOG_ERR("Unable to allocate memory for %s", type);
			attr_set_signed32(ATTR_ID_cloudError,
					  CLOUD_ERROR_CRED_SIZE);
			break;
		}

		r = fsu_read_abs(name, *cert, size);
		if (r != size) {
			r = -EIO;
			LOG_ERR("Unexpected file size");
			attr_set_signed32(ATTR_ID_cloudError,
					  CLOUD_ERROR_READ_CRED_FS);
			break;
		} else {
			r += 1;
		}
	} while (0);

	LOG_INF("Load %s: %d", type, r);

	return r;
}
