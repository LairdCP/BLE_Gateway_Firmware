/**
 * @file shadow_builder.c
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_INF
LOG_MODULE_REGISTER(shadow_builder);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <string.h>

#include "FrameworkIncludes.h"
#include "to_string.h"
#include "shadow_builder.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define JSON_APPEND_CHAR(c) JsonAppendChar(pJsonMsg, (uint8_t)(c))
#define JSON_APPEND_STRING(s) JsonAppendString(pJsonMsg, (s), true)

#define JSON_APPEND_VALUE_STRING(s)                                            \
	do {                                                                   \
		JSON_APPEND_CHAR('"');                                         \
		JSON_APPEND_STRING(s);                                         \
		JSON_APPEND_CHAR('"');                                         \
	} while (0)

#define JSON_APPEND_KEY(s)                                                     \
	do {                                                                   \
		JSON_APPEND_VALUE_STRING(s);                                   \
		JSON_APPEND_CHAR(':');                                         \
	} while (0)

#define JSON_APPEND_U32(c)                                                     \
	do {                                                                   \
		memset(str, 0, MAXIMUM_LENGTH_OF_TO_STRING_OUTPUT);            \
		ToString_Dec(str, (c));                                        \
		JSON_APPEND_STRING(str);                                       \
	} while (0)

#define JSON_APPEND_HEX8(c)                                                    \
	do {                                                                   \
		memset(str, 0, MAXIMUM_LENGTH_OF_TO_STRING_OUTPUT);            \
		ToString_Hex8(str, (c));                                       \
		JSON_APPEND_VALUE_STRING(str);                                 \
	} while (0)

#define JSON_APPEND_HEX16(c)                                                   \
	do {                                                                   \
		memset(str, 0, MAXIMUM_LENGTH_OF_TO_STRING_OUTPUT);            \
		ToString_Hex16(str, (c));                                      \
		JSON_APPEND_VALUE_STRING(str);                                 \
	} while (0)

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static char str[MAXIMUM_LENGTH_OF_TO_STRING_OUTPUT];

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void JsonAppendString(JsonMsg_t *pJsonMsg, const char *restrict pString,
			     bool EscapeQuoteChar);
static void JsonAppendChar(JsonMsg_t *pJsonMsg, char Character);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void ShadowBuilder_Start(JsonMsg_t *pJsonMsg, bool ClearBuffer)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pJsonMsg->size != 0);
	if (ClearBuffer) {
		memset(pJsonMsg->buffer, 0, pJsonMsg->size);
	}
	pJsonMsg->length = 0;
	JSON_APPEND_CHAR('{');
}

void ShadowBuilder_Finalize(JsonMsg_t *pJsonMsg)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pJsonMsg->buffer[pJsonMsg->length - 1] == ',');
	pJsonMsg->buffer[pJsonMsg->length - 1] = '}';
}

void ShadowBuilder_AddUint32(JsonMsg_t *pJsonMsg, const char *restrict pKey,
			     uint32_t Value)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);

	JSON_APPEND_KEY(pKey);
	memset(str, 0, MAXIMUM_LENGTH_OF_TO_STRING_OUTPUT);
	ToString_Dec(str, Value);
	JSON_APPEND_STRING(str);
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddSigned32(JsonMsg_t *pJsonMsg, const char *restrict pKey,
			       int32_t Value)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);

	JSON_APPEND_KEY(pKey);
	memset(str, 0, MAXIMUM_LENGTH_OF_TO_STRING_OUTPUT);
	int32_t v = Value;
	if (Value < 0) {
		JSON_APPEND_CHAR('-');
		v *= -1;
	}
	ToString_Dec(str, (uint32_t)v);
	JSON_APPEND_STRING(str);
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddPair(JsonMsg_t *pJsonMsg, const char *restrict pKey,
			   const char *restrict pValue, bool IsNotString)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(pValue != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);
	/* strings are allowed to be empty, but numbers aren't */
	if (IsNotString) {
		FRAMEWORK_ASSERT(strlen(pValue) > 0);
	}

	JSON_APPEND_KEY(pKey);
	if (IsNotString) {
		JSON_APPEND_STRING(pValue); /* uint32_t, int32_t, float, ... */
	} else {
		JSON_APPEND_VALUE_STRING(pValue);
	}
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddVersion(JsonMsg_t *pJsonMsg, const char *restrict pKey,
			      uint8_t Major, uint8_t Minor, uint8_t Build)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);

	JSON_APPEND_KEY(pKey);
	JSON_APPEND_CHAR('"');
	JSON_APPEND_U32(Major);
	JSON_APPEND_CHAR('.');
	JSON_APPEND_U32(Minor);
	JSON_APPEND_CHAR('.');
	JSON_APPEND_U32(Build);
	JSON_APPEND_CHAR('"');
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddNull(JsonMsg_t *pJsonMsg, const char *restrict pKey)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);

	JSON_APPEND_KEY(pKey);
	JSON_APPEND_STRING("null");
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddTrue(JsonMsg_t *pJsonMsg, const char *restrict pKey)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);

	JSON_APPEND_KEY(pKey);
	JSON_APPEND_STRING("true");
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddFalse(JsonMsg_t *pJsonMsg, const char *restrict pKey)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);

	JSON_APPEND_KEY(pKey);
	JSON_APPEND_STRING("false");
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_StartGroup(JsonMsg_t *pJsonMsg, const char *restrict pKey)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);

	JSON_APPEND_KEY(pKey);
	JSON_APPEND_CHAR('{');
}

void ShadowBuilder_EndGroup(JsonMsg_t *pJsonMsg)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pJsonMsg->buffer[pJsonMsg->length - 1] == ',');

	pJsonMsg->buffer[pJsonMsg->length - 1] = '}';
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_StartArray(JsonMsg_t *pJsonMsg, const char *restrict pKey)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);

	JSON_APPEND_KEY(pKey);
	JSON_APPEND_CHAR('[');
}

void ShadowBuilder_EndArray(JsonMsg_t *pJsonMsg)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pJsonMsg->buffer[pJsonMsg->length - 1] == ',');

	pJsonMsg->buffer[pJsonMsg->length - 1] = ']';
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddSensorTableArrayEntry(JsonMsg_t *pJsonMsg,
					    const char *restrict pAddrStr,
					    uint32_t Epoch, bool Greenlisted)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pAddrStr != NULL);
	FRAMEWORK_ASSERT(strlen(pAddrStr) > 0);

	JSON_APPEND_CHAR('[');
	JSON_APPEND_VALUE_STRING(pAddrStr);
	JSON_APPEND_CHAR(',');
	JSON_APPEND_U32(Epoch);
	JSON_APPEND_CHAR(',');
	JSON_APPEND_STRING(Greenlisted ? "true" : "false");
	JSON_APPEND_CHAR(']');
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddEventLogEntry(JsonMsg_t *pJsonMsg, SensorLogEvent_t *p)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);

	JSON_APPEND_CHAR('[');
	JSON_APPEND_HEX8(p->recordType);
	JSON_APPEND_CHAR(',');
	JSON_APPEND_U32(p->epoch);
	JSON_APPEND_CHAR(',');
	JSON_APPEND_HEX16(p->data);
	JSON_APPEND_CHAR(']');
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddString(JsonMsg_t *pJsonMsg, const char *restrict pKey,
			     const char *restrict pStr)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);
	FRAMEWORK_ASSERT(pStr != NULL);

	JSON_APPEND_KEY(pKey);
	JsonAppendString(pJsonMsg, pStr, false);
	JSON_APPEND_CHAR(',');
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void JsonAppendChar(JsonMsg_t *pJsonMsg, char Character)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	/* Leave room for NULL terminator */
	if (pJsonMsg->length < (pJsonMsg->size - 1)) {
		pJsonMsg->buffer[pJsonMsg->length++] = Character;
	} else { /* buffer too small */
		FRAMEWORK_ASSERT(false);
	}
}

static void JsonAppendString(JsonMsg_t *pJsonMsg, const char *restrict pString,
			     bool EscapeQuoteChar)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	if (pJsonMsg == NULL) {
		return;
	}

	size_t length = strlen(pString);
	size_t i = 0;
	/* Leave room for NULL terminator and an escaped char. */
	while (i < length && (pJsonMsg->length < (pJsonMsg->size - 2))) {
		/* Escape character handling */
		switch (pString[i]) {
		case '"':
			if (EscapeQuoteChar) {
				pJsonMsg->buffer[pJsonMsg->length++] = '\\';
			}
			pJsonMsg->buffer[pJsonMsg->length++] = pString[i++];
			break;

		case '\\':
			pJsonMsg->buffer[pJsonMsg->length++] = '\\';
			pJsonMsg->buffer[pJsonMsg->length++] = pString[i++];
			break;

		case '\b':
			pJsonMsg->buffer[pJsonMsg->length++] = '\\';
			pJsonMsg->buffer[pJsonMsg->length++] = 'b';
			i += 1;
			break;

		case '\f':
			pJsonMsg->buffer[pJsonMsg->length++] = '\\';
			pJsonMsg->buffer[pJsonMsg->length++] = 'f';
			i += 1;
			break;

		case '\n':
			pJsonMsg->buffer[pJsonMsg->length++] = '\\';
			pJsonMsg->buffer[pJsonMsg->length++] = 'n';
			i += 1;
			break;

		case '\r':
			pJsonMsg->buffer[pJsonMsg->length++] = '\\';
			pJsonMsg->buffer[pJsonMsg->length++] = 'r';
			i += 1;
			break;

		case '\t':
			pJsonMsg->buffer[pJsonMsg->length++] = '\\';
			pJsonMsg->buffer[pJsonMsg->length++] = 't';
			i += 1;
			break;

		default:
			pJsonMsg->buffer[pJsonMsg->length++] = pString[i++];
			break;
		}
	}
}
