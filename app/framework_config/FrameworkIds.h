/**
 * @file FrameworkIds.h
 * @brief The message task/receiver IDs in the system.
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __FRAMEWORK_IDS__
#define __FRAMEWORK_IDS__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "Framework.h"

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
enum FwkIdEnum {
	/* Reserved for framework (DO NOT DELETE) */
	__FWK_ID_RESERVED = FWK_ID_RESERVED,

	/* Application */
	FWK_ID_CLOUD = FWK_ID_APP_START,
#ifdef CONFIG_SENSOR_TASK
	FWK_ID_SENSOR_TASK,
#endif
#ifdef CONFIG_COAP_FOTA
	FWK_ID_COAP_FOTA_TASK,
#endif
#ifdef CONFIG_HTTP_FOTA
	FWK_ID_HTTP_FOTA_TASK,
#endif

	/* Reserved for framework (DO NOT DELETE, and it must be LAST) */
	__FRAMEWORK_MAX_MSG_RECEIVERS
};
BUILD_ASSERT(__FRAMEWORK_MAX_MSG_RECEIVERS <= CONFIG_FWK_MAX_MSG_RECEIVERS,
	     "Adjust number of message receivers in Kconfig");

#define FWK_ID_CONTROL_TASK FWK_ID_CLOUD

#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_IDS__ */
