/**
 * @file fota.c
 * @brief Parse structures used for Firmware Update
 *
 * @note The parsers do not process the "desired" section of the get accepted
 * data.  It is processed when the delta topic is received.
 *
 * Copyright (c) 2020-2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(fota_parser, CONFIG_SHADOW_PARSER_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <kernel.h>
#include <init.h>

#define JSMN_PARENT_LINKS
#define JSMN_HEADER
#include "jsmn.h"
#include "jsmn_json.h"

#include "shadow_parser.h"
#include "shadow_parser_flags_aws.h"

#ifdef CONFIG_COAP_FOTA
#include "coap_fota_shadow.h"
#endif

#ifdef CONFIG_HTTP_FOTA
#include "http_fota_shadow.h"
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct shadow_parser_agent agent;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void fota_shadow_parser(const char *topic, struct topic_flags *flags);

static void fota_parser(const char *topic, struct topic_flags *flags,
		       enum fota_image_type type);

#ifdef CONFIG_COAP_FOTA
static void fota_host_parser(const char *topic, struct topic_flags *flags);
static void fota_block_size_parser(const char *topic, struct topic_flags *flags);
#endif

/******************************************************************************/
/* Init                                                                       */
/******************************************************************************/
static int fota_shadow_parser_init(const struct device *device)
{
	ARG_UNUSED(device);

	agent.parser = fota_shadow_parser;
	shadow_parser_register_agent(&agent);

	return 0;
}

SYS_INIT(fota_shadow_parser_init, APPLICATION, 99);

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void fota_shadow_parser(const char *topic, struct topic_flags *flags)
{
	if (SP_FLAG(gateway)) {
		fota_parser(topic, flags, APP_IMAGE_TYPE);
		if (IS_ENABLED(CONFIG_MODEM_HL7800)) {
			fota_parser(topic, flags, MODEM_IMAGE_TYPE);
		}

#ifdef CONFIG_COAP_FOTA
		fota_host_parser(topic, flags);
		fota_block_size_parser(topic, flags);
#endif
	}
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void fota_parser(const char *topic, struct topic_flags *flags,
		       enum fota_image_type type)
{
	ARG_UNUSED(topic);
	const char *img_name;
	int location = 0;

	jsmn_reset_index();

	/* Try to find "state":{"app":{"desired":"2.1.0","switchover":10}} */
	jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	if (SP_FLAG(get_accepted)) {
		jsmn_find_type("reported", JSMN_OBJECT, NEXT_PARENT);
	}

#ifdef CONFIG_COAP_FOTA
	img_name = coap_fota_get_image_name(type);
#else
	img_name = http_fota_get_image_name(type);
#endif

	jsmn_find_type(img_name, JSMN_OBJECT, NEXT_PARENT);

	if (jsmn_index() > 0) {
		jsmn_save_index();

		location = jsmn_find_type(SHADOW_FOTA_DESIRED_STR, JSMN_STRING,
					  NEXT_PARENT);
		if (location > 0) {
#ifdef CONFIG_COAP_FOTA
			coap_fota_set_desired_version(type,
						      jsmn_string(location),
						      jsmn_strlen(location));
#else
			http_fota_set_desired_version(type,
						      jsmn_string(location),
						      jsmn_strlen(location));
#endif
		}

		jsmn_restore_index();
#ifdef CONFIG_COAP_FOTA
		location = jsmn_find_type(SHADOW_FOTA_DESIRED_FILENAME_STR,
					  JSMN_STRING, NEXT_PARENT);
		if (location > 0) {
			coap_fota_set_desired_filename(type,
						       jsmn_string(location),
						       jsmn_strlen(location));
		}
#else
		location = jsmn_find_type(SHADOW_FOTA_DOWNLOAD_HOST_STR,
					  JSMN_STRING, NEXT_PARENT);
		if (location > 0) {
			http_fota_set_download_host(type, jsmn_string(location),
						    jsmn_strlen(location));
		}

		jsmn_restore_index();
		location = jsmn_find_type(SHADOW_FOTA_DOWNLOAD_FILE_STR,
					  JSMN_STRING, NEXT_PARENT);
		if (location > 0) {
			http_fota_set_download_file(type, jsmn_string(location),
						    jsmn_strlen(location));
		}

		jsmn_restore_index();
		location = jsmn_find_type(SHADOW_FOTA_HASH_STR, JSMN_STRING,
					  NEXT_PARENT);
		if (location > 0) {
			http_fota_set_hash(type, jsmn_string(location),
					   jsmn_strlen(location));
		}
#endif

		jsmn_restore_index();
		location = jsmn_find_type(SHADOW_FOTA_SWITCHOVER_STR,
					  JSMN_PRIMITIVE, NEXT_PARENT);
		if (location > 0) {
#ifdef CONFIG_COAP_FOTA
			coap_fota_set_switchover(type,
						 jsmn_convert_uint(location));
#else
			http_fota_set_switchover(type,
						 jsmn_convert_uint(location));
#endif
		}

		jsmn_restore_index();
		location = jsmn_find_type(SHADOW_FOTA_START_STR, JSMN_PRIMITIVE,
					  NEXT_PARENT);
		if (location > 0) {
#ifdef CONFIG_COAP_FOTA
			coap_fota_set_start(type, jsmn_convert_uint(location));
#else
			http_fota_set_start(type, jsmn_convert_uint(location));
#endif
		}

		/* Don't overwrite error count when reading shadow. */
		if (!SP_FLAG(get_accepted)) {
			jsmn_restore_index();
			location = jsmn_find_type(SHADOW_FOTA_ERROR_STR,
						  JSMN_PRIMITIVE, NEXT_PARENT);
			if (location > 0) {
#ifdef CONFIG_COAP_FOTA
				coap_fota_set_error_count(
					type, jsmn_convert_uint(location));
#else
				http_fota_set_error_count(
					type, jsmn_convert_uint(location));
#endif
			}
		}
	}
}

#ifdef CONFIG_COAP_FOTA
static void fota_host_parser(const char *topic, struct topic_flags *flags)
{
	ARG_UNUSED(topic);

	jsmn_reset_index();

	/* Try to find "state":{"fwBridge":"something.com"}} */
	jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	if (SP_FLAG(get_accepted)) {
		jsmn_find_type("reported", JSMN_OBJECT, NEXT_PARENT);
	}
	int location = jsmn_find_type(SHADOW_FOTA_BRIDGE_STR, JSMN_STRING,
				      NEXT_PARENT);
	if (location > 0) {
		coap_fota_set_host(jsmn_string(location),
				   jsmn_strlen(location));
	}
}

static void fota_block_size_parser(const char *topic, struct topic_flags *flags)
{
	ARG_UNUSED(topic);
	int location;

	jsmn_reset_index();

	jsmn_find_type("state", JSMN_OBJECT, NEXT_PARENT);
	if (SP_FLAG(get_accepted)) {
		jsmn_find_type("reported", JSMN_OBJECT, NEXT_PARENT);
	}

	location = jsmn_find_type(SHADOW_FOTA_BLOCKSIZE_STR, JSMN_PRIMITIVE,
				  NEXT_PARENT);
	if (location > 0) {
		coap_fota_set_blocksize(jsmn_convert_uint(location));
	}
}
#endif /* CONFIG_COAP_FOTA */
