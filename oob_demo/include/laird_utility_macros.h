/* laird_utility_macros.h
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LAIRD_UTILITY_MACROS_H
#define LAIRD_UTILITY_MACROS_H

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

#endif
