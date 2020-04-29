/**
 * @file FrameworkIds.h
 * @brief The message task/receiver IDs in the system.
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __FRAMEWORK_IDS__
#define __FRAMEWORK_IDS__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
typedef enum FwkIdEnumeration {
	/* Reserved for framework (DO NOT DELETE) */
	FWK_ID_RESERVED = 0,

	/* Application */
	FWK_ID_SENSOR_TASK,
	FWK_ID_AWS,

	/* Reserved for framework (DO NOT DELETE, and it must be LAST) */
	FRAMEWORK_MAX_MSG_RECEIVERS
} FwkId_t;

#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_IDS__ */
