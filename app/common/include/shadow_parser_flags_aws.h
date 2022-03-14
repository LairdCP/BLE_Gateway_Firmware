/**
 * @file shadow_parser_flags_aws.h
 * @brief Flag definitions for AWS
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SHADOW_PARSER_FLAGS_H__
#define __SHADOW_PARSER_FLAGS_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
/* strstr results of topic string */
struct topic_flags {
	bool get_accepted : 1;
	bool gateway : 1;
};

#ifdef __cplusplus
}
#endif

#endif /* __SHADOW_PARSER_FLAGS_H__ */
