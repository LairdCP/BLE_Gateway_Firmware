/* mg100_common.c -
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(mg100_common);

#include "mg100_common.h"

void strncpy_replace_underscore_with_space(char *restrict s1,
					   const char *restrict s2, size_t size)
{
	memset(s1, 0, size);
	size_t i;
	for (i = 0; i < size - 1; i++) {
		if (s2[i] == '\0') {
			break;
		} else if (s2[i] == '_') {
			s1[i] = ' ';
		} else {
			s1[i] = s2[i];
		}
	}
}
