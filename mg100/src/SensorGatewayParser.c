/* SensorGatewayParser.c - Use jsmn to parse JSON from AWS that controls
 * gateway functionality.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_INF
LOG_MODULE_REGISTER(SensorGatewayParser);

//=============================================================================
// Includes
//=============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define JSMN_PARENT_LINKS
#include "jsmn.h"
#include "SensorBt510.h"
#include "Framework.h"

//=============================================================================
// Local Constant, Macro and Type Definitions
//=============================================================================

#define NUMBER_OF_JSMN_TOKENS 256

#define CHILD_ARRAY_SIZE 3
#define CHILD_ARRAY_INDEX 0
#define ARRAY_NAME_INDEX 1
#define ARRAY_EPOCH_INDEX 2
#define ARRAY_WLIST_INDEX 3
#define JSMN_NO_CHILDREN 0

//=============================================================================
// Global Data Definitions
//=============================================================================

//=============================================================================
// Local Data Definitions
//=============================================================================
static jsmn_parser jsmn;
static jsmntok_t tokens[NUMBER_OF_JSMN_TOKENS];
static int tokensFound;
static int nextParent;
static int jsonIndex;
static int sensorsFound;
static int expectedSensors;

//=============================================================================
// Local Function Prototypes
//=============================================================================
static void FindType(const char *pJson, const char *s, jsmntype_t Type,
		     int Parent);
static void ParseArray(const char *pJson);

//=============================================================================
// Global Function Definitions
//=============================================================================
void SensorGatewayParser_Run(char *pJson)
{
	jsmn_init(&jsmn);

	expectedSensors = 0;
	sensorsFound = 0;
	tokensFound = jsmn_parse(&jsmn, pJson, strlen(pJson), tokens,
				 NUMBER_OF_JSMN_TOKENS);
	LOG_DBG("jsmn tokens required: %d", tokensFound);

	// The first thing found should be the JSON object { }
	// As long as the JSON index != 0, the parsing will continue.
	if (tokensFound > 1 && (tokens[0].type == JSMN_OBJECT)) {
		jsonIndex = 1;
		nextParent = 0;
		// Now try to find {"state": { "desired": {"bt510": {"sensors":
		FindType(pJson, "state", JSMN_OBJECT, nextParent);
		FindType(pJson, "desired", JSMN_OBJECT, nextParent);
		FindType(pJson, "bt510", JSMN_OBJECT, nextParent);
		FindType(pJson, "sensors", JSMN_ARRAY, nextParent);
	}

	if (jsonIndex != 0) {
		// Backup one token to get the number of arrays (sensors).
		expectedSensors = tokens[jsonIndex - 1].size;
		if (expectedSensors > BT510_SENSOR_TABLE_SIZE) {
			jsonIndex = 0;
		}
	}

	ParseArray(pJson);
}

//=============================================================================
// Local Function Definitions
//=============================================================================
// This function updates the global index to the next token when an item + type
// is found.  Otherwise, the index is set to zero.
//
static void FindType(const char *pJson, const char *s, jsmntype_t Type,
		     int Parent)
{
	if (jsonIndex == 0) {
		return;
	}

	// Analyze a pair of tokens of the form <string>, <type>
	size_t i = jsonIndex;
	jsonIndex = 0;
	for (; ((i + 1) < tokensFound); i++) {
		int length = tokens[i].end - tokens[i].start;
		if ((tokens[i].type == JSMN_STRING) &&
		    ((int)strlen(s) == length) &&
		    (strncmp(pJson + tokens[i].start, s, length) == 0) &&
		    (tokens[i + 1].type == Type) &&
		    ((Parent == 0) || (tokens[i].parent == Parent))) {
			LOG_DBG("Found '%s' at index %d with parent %d", s, i,
				tokens[i].parent);
			nextParent = i + 1;
			jsonIndex = i + 2;
			break;
		}
	}
}

// Parse the elements in the anonymous array into a c-structure.
// ["addrString", epoch, whitelist (boolean)]
// The epoch isn't used.
static void ParseArray(const char *pJson)
{
	if (jsonIndex == 0) {
		return;
	}

	SensorWhitelistMsg_t *pMsg =
		BufferPool_TryToTake(sizeof(SensorWhitelistMsg_t));
	if (pMsg == NULL) {
		return;
	}

	size_t i = jsonIndex;
	jsonIndex = 0;
	for (; ((i + CHILD_ARRAY_SIZE) < tokensFound) &&
	       (sensorsFound < expectedSensors);
	     i++) {
		int addrLength = tokens[i].end - tokens[i].start;
		if ((tokens[i + CHILD_ARRAY_INDEX].type == JSMN_ARRAY) &&
		    (tokens[i + CHILD_ARRAY_INDEX].size == CHILD_ARRAY_SIZE) &&
		    (tokens[i + ARRAY_NAME_INDEX].type == JSMN_STRING) &&
		    (tokens[i + ARRAY_NAME_INDEX].size == JSMN_NO_CHILDREN) &&
		    (tokens[i + ARRAY_EPOCH_INDEX].type == JSMN_PRIMITIVE) &&
		    (tokens[i + ARRAY_EPOCH_INDEX].size == JSMN_NO_CHILDREN) &&
		    (tokens[i + ARRAY_WLIST_INDEX].type == JSMN_PRIMITIVE) &&
		    (tokens[i + ARRAY_WLIST_INDEX].size == JSMN_NO_CHILDREN)) {
			LOG_DBG("Found array at %i", jsonIndex);
			strncpy(pMsg->sensors[sensorsFound].addrString,
				&pJson[tokens[i + ARRAY_NAME_INDEX].start],
				MAX(addrLength, BT510_ADDR_STR_LEN));
			// The 't' in true is used to determine true/false.
			pMsg->sensors[sensorsFound].whitelist =
				(pJson[tokens[i + ARRAY_WLIST_INDEX].start] ==
				 't');
			sensorsFound += 1;
			jsonIndex = i + CHILD_ARRAY_SIZE + 1;
		}
	}

	pMsg->header.msgCode = FMC_WHITELIST_REQUEST;
	pMsg->header.rxId = FWK_ID_SENSOR_TASK;
	pMsg->sensorCount = sensorsFound;
	FRAMEWORK_MSG_SEND(pMsg);

	LOG_INF("Found %d sensors in desired list from AWS", sensorsFound);
}

// end