/**
 * @file sensor_gateway_parser.h
 * @brief Process subscription data from AWS.
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SENSOR_GATEWAY_PARSER_H__
#define __SENSOR_GATEWAY_PARSER_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Process JSON messages from AWS.
 *
 * @param pTopic is a pointer to the topic message was received on.
 * For gateway topic, sends a message to sensor task to greenlist sensors.
 * For sensor topic, sends a message to sensor task to configure sensor.
 * @param pJson is a pointer to JSON string.
 */
void SensorGatewayParser(const char *pTopic, const char *pJson);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_GATEWAY_PARSER_H__ */
