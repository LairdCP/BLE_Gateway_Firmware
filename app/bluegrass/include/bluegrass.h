/**
 * @file bluegrass.h
 * @brief Bluegrass is Laird Connectivity's AWS interface.
 *
 * Copyright (c) 2020-2021 Laird Connectivity
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
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Initialize Bluegrass gateway interface.
 *
 * Initialize sensor task if enabled.  The sensor task will process messages
 * from the BT510.
 */
void bluegrass_initialize(void);

/**
 * @brief Request shadow init to be sent.
 */
void bluegrass_init_shadow_request(void);

/**
 * @brief Framework message handler for gateway and sensor data.
 */
DispatchResult_t bluegrass_msg_handler(FwkMsgReceiver_t *pMsgRxer,
				       FwkMsg_t *pMsg);

/**
 * @brief Must be periodically called to process subscriptions.
 * The gateway shadow must be processed on connection.
 * The delta topic must be subscribed to.
 *
 * @retval negative error code, 0 on success
 */
int bluegrass_subscription_handler(void);

/**
 * @brief Notify other parts of system that a cloud connection has started.
 * Start heartbeat.  Init shadow if required. Send CT stashed data.
 */
void bluegrass_connected_callback(void);

/**
 * @brief The sensor task can discard data if the connection to AWS is lost.
 */
void bluegrass_disconnected_callback(void);

/**
 * @brief Accessor function
 *
 * @retval true if system is ready for publishing to AWS/Bluegrass
 */
bool bluegrass_ready_for_publish(void);

#ifdef __cplusplus
}
#endif

#endif /* __BLUEGRASS_H__ */
