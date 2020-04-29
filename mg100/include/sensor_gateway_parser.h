/**
 * @file sensor_gateway_parser.h
 * @brief Process subscription data from AWS that is used to
 * enable/disable sending BT510 data to AWS.
 *
 * Copyright (c) 2020 Laird Connectivity
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
 * @brief Process JSON message from AWS.  Sends a message to sensor task to
 * whitelist sensors.
 */
void SensorGatewayParser_Run(char *pJson);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_GATEWAY_PARSER_H__ */
