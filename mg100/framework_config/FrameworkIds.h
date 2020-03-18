//=============================================================================
//! @file FrameworkIds.h
//!
//! @brief The message task/receiver IDs in the system.
//!
//! @copyright Copyright 2020 Laird
//!            All Rights Reserved.
//=============================================================================

#ifndef FRAMEWORK_RECEIVER_IDS
#define FRAMEWORK_RECEIVER_IDS

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Global Constants, Macros and Type Definitions
//=============================================================================
typedef enum FwkIdEnumeration {
	// Reserved for framework (DO NOT DELETE)
	FWK_ID_RESERVED = 0,

	// Application
	FWK_ID_SENSOR_TASK,
	FWK_ID_AWS,

	// Reserved for framework (DO NOT DELETE, and it must be LAST)
	FRAMEWORK_MAX_MSG_RECEIVERS

} FwkId_t;

//=============================================================================
// Global Data Definitions
//=============================================================================
// NA

//=============================================================================
// Global Function Prototypes
//=============================================================================
// NA

#ifdef __cplusplus
}
#endif

#endif

// end
