/**
 * @file bluegrass.h
 * @brief Bluegrass is Laird Connectivity's AWS interface.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __BLUEGRASS_H__
#define __BLUEGRASS_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#include "FrameworkIncludes.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Initialize sensor task.  The sensor task will process messages from
 * the BT510.
 *
 * @param pQ is a pointer to the queue that the Sensor Task will receive
 * messages from to send to AWS.
 */
void Bluegrass_Initialize(FwkQueue_t *pQ);

/**
 * @brief Subscribe and post to topics for individual sensors.  Post sensor
 * list to gateway topic.
 *
 * @param pMsg is a message containing an AWS interaction.
 * @param pFreeMsg is set to true if the message can be freed.
 *
 * @retval zero for success
 */
int Bluegrass_MsgHandler(FwkMsg_t *pMsg, bool *pFreeMsg);

/**
 * @brief The gateway shadow must be processed on connection.
 * The delta topic must be subscribed to.
 */
void Bluegrass_ConnectedCallback(void);

/**
 * @brief The sensor task can discard data if the connection to AWS is lost.
 */
void Bluegrass_DisconnectedCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* __BLUEGRASS_H__ */
