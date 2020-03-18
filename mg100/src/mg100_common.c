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

char *replace_word(const char *s, const char *oldW, const char *newW,
		   char *dest, int destSize)
{
	int i, cnt = 0;
	int newWlen = strlen(newW);
	int oldWlen = strlen(oldW);

	/* Counting the number of times old word
	*  occur in the string 
	*/
	for (i = 0; s[i] != '\0'; i++) {
		if (strstr(&s[i], oldW) == &s[i]) {
			cnt++;

			/* Jumping to index after the old word. */
			i += oldWlen - 1;
		}
	}

	/* Make sure new string isn't too big */
	if ((i + cnt * (newWlen - oldWlen) + 1) > destSize) {
		return NULL;
	}

	i = 0;
	while (*s) {
		/* compare the substring with the result */
		if (strstr(s, oldW) == s) {
			strcpy(&dest[i], newW);
			i += newWlen;
			s += oldWlen;
		} else {
			dest[i++] = *s++;
		}
	}

	dest[i] = '\0';
	return dest;
}

/* max_str_len is the maximum length of the resulting string. */
char *strncat_max(char *d1, const char *s1, size_t max_str_len)
{
	size_t len = strlen(d1);
	size_t n = (len < max_str_len) ? (max_str_len - len) : 0;
	return strncat(d1, s1, n); // adds null-character (n+1)
}

static void print_thread_cb(const struct k_thread *thread, void *user_data)
{
	u32_t *pc = (u32_t *)user_data;
	*pc += 1;
	/* discard const qualifier */
	struct k_thread *tid = (struct k_thread *)thread;
	printk("%02u id: (0x%08x) priority: %3d name: '%s' ", *pc, (u32_t)tid,
	       k_thread_priority_get(tid), log_strdup(k_thread_name_get(tid)));
#if 0 /* not in this zephyr version. */
	printk("state %s ", k_thread_state_str(tid));
#endif
	printk("\r\n");
}

/* Requires CONFIG_THREAD_MONITOR and CONFIG_THREAD_NAME */
void print_thread_list(void)
{
	u32_t thread_count = 0;
	k_thread_foreach(print_thread_cb, &thread_count);
	printk("Preemption is %s\r\n",
	       (CONFIG_PREEMPT_ENABLED) ? "Enabled" : "Disabled");
}

/* Log system cannot be used to print JSON buffers because they are too large.
 * String could be broken into LOG_STRDUP_MAX_STRING chunks.
 */
void print_json(const char *prefix, size_t size, const char *buffer)
{
#if JSON_LOG_ENABLED
	printk("%s size: %u data: %s\r\n", prefix, size, buffer);
#endif
}