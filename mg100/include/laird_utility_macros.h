/**
 * @file laird_utility_macros.h
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LAIRD_UTILITY_MACROS_H__
#define __LAIRD_UTILITY_MACROS_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
#define SWITCH_CASE_RETURN_STRING(val)                                         \
	case val: {                                                            \
		return #val;                                                   \
	}

#define PREFIXED_SWITCH_CASE_RETURN_STRING(prefix, val)                        \
	case prefix##_##val: {                                                 \
		return #val;                                                   \
	}

#define POSTFIXED_SWITCH_CASE_RETURN_STRING(val, postfix)                      \
	case val##_##postfix: {                                                \
		return #val;                                                   \
	}

/* For state function pointers */
#define IF_RETURN_STRING(x, val)                                               \
	if ((x) == val) {                                                      \
		return #val;                                                   \
	}

#ifdef __cplusplus
}
#endif

#endif /* __LAIRD_UTILITY_MACROS_H__ */
