/**
 * @file cloud.h
 * @brief API used to decouple different cloud interfaces.
 * (AWS, Bluegrass, Losant, LwM2M, ...).
 * Weak implementations in control task can be overridden.
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __CLOUD_SUB_TASK_H__
#define __CLOUD_SUB_TASK_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "FrameworkIncludes.h"

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief The control/cloud task uses the main thread.
 * This exposes a message dispatcher that can be used by a specific cloud
 * interface
 *
 * @param MsgCode of a framework message
 * @return FwkMsgHandler_t* pointer to a message handler function or NULL if
 * there isn't a handler
 */
FwkMsgHandler_t *cloud_sub_task_msg_dispatcher(FwkMsgCode_t MsgCode);


/**
 * @brief Request shadow static values to be regenerated and sent
 * Regenerate topics based on id.
 * Used when modem version changes due to firmware update.
 */
void cloud_init_shadow_request(void);

/**
 * @brief Perform steps to commission with cloud provider
 * For example, loading certificates.
 *
 * @return int 0 on success, negative errno;
 * EBUSY means function should be called again
 */
int cloud_commission(void);

/**
 * @brief Perform steps to de-commission with cloud provider
 * For example, unloading certificates.
 *
 * @return int 0 on success, negative errno;
 * EBUSY means function should be called again
 */
int cloud_decommission(void);

/**
 * @brief Application layer handler called when commissioned,
 * endpoint, or port attributes are modified.
 *
 * @return int 0 on success, negative errno
 */
int commission_handler(void);


#ifdef __cplusplus
}
#endif

#endif /* __CONTROL_TASK_H__ */
