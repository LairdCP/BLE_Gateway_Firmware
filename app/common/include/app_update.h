/**
 * @file app_update.h
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __APP_UPDATE_H__
#define __APP_UPDATE_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Initiate application update from a file on the file system.
 * Copy file into slot 2.  Set Memfault tracking flags. Issue reset.
 *
 * @param abs_path Absolute path/name of file
 * @param delete_after_copy boolean
 * @return int 0 on success, otherwise negative error code
 */
int app_update_initiate(const char *abs_path, bool delete_after_copy);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UPDATE_H__ */