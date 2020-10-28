/**
 * @file app_version.h
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __APP_VERSION_H__
#define __APP_VERSION_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Application Firmware Version                                               */
/******************************************************************************/
#define APP_VERSION_MAJOR 3
#define APP_VERSION_MINOR 0
#define APP_VERSION_PATCH 1
#define APP_VERSION_STRING                                                     \
	STRINGIFY(APP_VERSION_MAJOR)                                           \
	"." STRINGIFY(APP_VERSION_MINOR) "." STRINGIFY(APP_VERSION_PATCH)

#ifdef __cplusplus
}
#endif

#endif /* __APP_VERSION_H__ */
