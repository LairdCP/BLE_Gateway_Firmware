//=============================================================================
//!
//! @file "ShadowBuilder.c"
//!
//! @copyright Copyright 2020 Laird
//!            All Rights Reserved.
//=============================================================================

//=============================================================================
// Includes
//=============================================================================
#include <string.h>

#include "Framework.h"
#include "ToString.h"
#include "ShadowBuilder.h"

//=============================================================================
// Local Constant, Macro and Type Definitions
//=============================================================================

#define JSON_APPEND_CHAR(c) JsonAppendChar(pJsonMsg, (u8_t)(c))

#define JSON_APPEND_VALUE_STRING(s)                                            \
	do {                                                                   \
		JSON_APPEND_CHAR('"');                                         \
		JsonAppendString(pJsonMsg, (s));                               \
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
		JSON_APPEND_STRING(str);                                       \
	} while (0)

#define JSON_APPEND_STRING(s) JsonAppendString(pJsonMsg, (s))

//=============================================================================
// Global Data Definitions
//=============================================================================

//=============================================================================
// Local Data Definitions
//=============================================================================
static char str[MAXIMUM_LENGTH_OF_TO_STRING_OUTPUT];

//=============================================================================
// Local Function Prototypes
//=============================================================================
static void JsonAppendString(JsonMsg_t *pJsonMsg, const char *restrict pString);
static void JsonAppendChar(JsonMsg_t *pJsonMsg, char Character);

//=============================================================================
// Global Function Definitions
//=============================================================================
void ShadowBuilder_Start(JsonMsg_t *pJsonMsg, bool ClearBuffer)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	if (ClearBuffer) {
		memset(pJsonMsg->buffer, 0, JSON_OUT_BUFFER_SIZE);
	}
	pJsonMsg->size = 0;
	JSON_APPEND_CHAR('{');
}

void ShadowBuilder_Finalize(JsonMsg_t *pJsonMsg)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pJsonMsg->buffer[pJsonMsg->size - 1] == ',');
	pJsonMsg->buffer[pJsonMsg->size - 1] = '}';
}

void ShadowBuilder_AddUint32(JsonMsg_t *pJsonMsg, const char *restrict pKey,
			     u32_t Value)
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
			       s32_t Value)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pKey != NULL);
	FRAMEWORK_ASSERT(strlen(pKey) > 0);

	JSON_APPEND_KEY(pKey);
	memset(str, 0, MAXIMUM_LENGTH_OF_TO_STRING_OUTPUT);
	s32_t v = Value;
	if (Value < 0) {
		JSON_APPEND_CHAR('-');
		v *= -1;
	}
	ToString_Dec(str, (u32_t)v);
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
	if (IsNotString) {
		FRAMEWORK_ASSERT(strlen(pValue) > 0); // string can be null
	}

	JSON_APPEND_KEY(pKey);
	if (IsNotString) {
		JSON_APPEND_STRING(pValue); // us32_t, s32_t, float, ...
	} else {
		JSON_APPEND_VALUE_STRING(pValue);
	}
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddVersion(JsonMsg_t *pJsonMsg, const char *restrict pKey,
			      u8_t Major, u8_t Minor, u8_t Build)
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
	FRAMEWORK_ASSERT(pJsonMsg->buffer[pJsonMsg->size - 1] == ',');

	pJsonMsg->buffer[pJsonMsg->size - 1] = '}';
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
	FRAMEWORK_ASSERT(pJsonMsg->buffer[pJsonMsg->size - 1] == ',');

	pJsonMsg->buffer[pJsonMsg->size - 1] = ']';
	JSON_APPEND_CHAR(',');
}

void ShadowBuilder_AddSensorTableArrayEntry(JsonMsg_t *pJsonMsg,
					    const char *restrict pAddrStr,
					    u32_t Epoch, bool Whitelisted)
{
	FRAMEWORK_ASSERT(pJsonMsg != NULL);
	FRAMEWORK_ASSERT(pAddrStr != NULL);
	FRAMEWORK_ASSERT(strlen(pAddrStr) > 0);

	JSON_APPEND_CHAR('[');
	JSON_APPEND_VALUE_STRING(pAddrStr);
	JSON_APPEND_CHAR(',');
	JSON_APPEND_U32(Epoch);
	JSON_APPEND_CHAR(',');
	JSON_APPEND_STRING(Whitelisted ? "true" : "false");
	JSON_APPEND_CHAR(']');
	JSON_APPEND_CHAR(',');
}

//=============================================================================
// Local Function Definitions
//=============================================================================
static void JsonAppendChar(JsonMsg_t *pJsonMsg, char Character)
{
	if ((pJsonMsg->size) < JSON_OUT_BUFFER_SIZE - 1) {
		pJsonMsg->buffer[pJsonMsg->size++] = Character;
	} else { // buffer too small
		FRAMEWORK_ASSERT(false);
	}

	FRAMEWORK_ASSERT(pJsonMsg->size < JSON_OUT_BUFFER_SIZE);
}

static void JsonAppendString(JsonMsg_t *pJsonMsg, const char *restrict pString)
{
	size_t length = strlen(pString);
	size_t i = 0;
	while (i < length && (pJsonMsg->size < JSON_OUT_BUFFER_SIZE - 1)) {
		// Escape character handling
		switch (pString[i]) {
		case '"':
		case '\\':
			pJsonMsg->buffer[pJsonMsg->size++] = '\\';
			pJsonMsg->buffer[pJsonMsg->size++] = pString[i++];
			break;

		case '\b':
			pJsonMsg->buffer[pJsonMsg->size++] = '\\';
			pJsonMsg->buffer[pJsonMsg->size++] = 'b';
			i += 1;
			break;

		case '\f':
			pJsonMsg->buffer[pJsonMsg->size++] = '\\';
			pJsonMsg->buffer[pJsonMsg->size++] = 'f';
			i += 1;
			break;

		case '\n':
			pJsonMsg->buffer[pJsonMsg->size++] = '\\';
			pJsonMsg->buffer[pJsonMsg->size++] = 'n';
			i += 1;
			break;

		case '\r':
			pJsonMsg->buffer[pJsonMsg->size++] = '\\';
			pJsonMsg->buffer[pJsonMsg->size++] = 'r';
			i += 1;
			break;

		case '\t':
			pJsonMsg->buffer[pJsonMsg->size++] = '\\';
			pJsonMsg->buffer[pJsonMsg->size++] = 't';
			i += 1;
			break;

		default:
			pJsonMsg->buffer[pJsonMsg->size++] = pString[i++];
			break;
		}
	}

	FRAMEWORK_ASSERT(i == length); // buffer too small
	FRAMEWORK_ASSERT(pJsonMsg->size < JSON_OUT_BUFFER_SIZE);
}

// end
