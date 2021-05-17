/**
 * @file attr_.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __ATTR_H__
#define __ATTR_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stdint.h>
#include <stddef.h>

#include "FrameworkIncludes.h"
#include "attr_table.h"

/******************************************************************************/
/* Global Constants, Macros and type Definitions                              */
/******************************************************************************/
typedef struct attr_broadcast_msg {
	FwkMsgHeader_t header;
	size_t count;
	attr_id_t list[ATTR_TABLE_WRITABLE_COUNT];
} attr_changed_msg_t;
BUILD_ASSERT(ATTR_TABLE_MAX_ID < (1 << (8 * sizeof(attr_id_t))),
	     "List element size too small");

/******************************************************************************/
/* Function Definitions                                                       */
/******************************************************************************/

/**
 * @brief Set values to alt (except items configured during production).
 *
 * @note Read only values are also reset to their alt values.  Many of
 * these values are set during initialization.
 *
 * Therefore, it is most likely that a software reset is required after
 * running this function (so that status/read-only values are repopulated).
 *
 * @retval negative error code, 0 on success
 */
int attr_factory_reset(void);

/**
 * @brief Get the type of the attribute
 *
 * @retval type of variable
 */
enum attr_type attr_get_type(attr_id_t id);

/**
 * @brief Helper function
 *
 * @retval true if id is valid, false otherwise
 */
bool attr_valid_id(attr_id_t id);

/**
 * @brief Set value.  This is the only set function that should be
 * used from the SMP interface.  It requires the writable flag to be true.
 *
 * @note SMP notifications are disabled during set.
 *
 * @param id an attribute id.
 * @param type the type of attribute
 * @param pv string representation of variable
 * @param vlen The length (without null char) of the string
 * being passed in.  If the value isn't a string, then the length is
 * not used.
 *
 * @retval negative error code, 0 on success
 */
int attr_set(attr_id_t id, enum attr_type type, void *pv, size_t vlen);

/**
 * @brief Same as set value except broadcast is disabled.
 *
 * @note Used for control points that also reflect status.
 *
 * @retval negative error code, 0 on success
 */
int attr_set_without_broadcast(attr_id_t id, enum attr_type type, void *pv,
			       size_t vlen);

/**
 * @brief Copy an attribute.  This is the only get function that should be
 * used from the SMP interface because it checks the readable flag.
 *
 * @note Sign extends up to int64 size when type is signed.
 *
 * @param id an attribute id.
 * @param pv pointer to location to copy string
 * @param vlen is the size of pv.
 *
 * @retval negative error code, size of value on return
 */
int attr_get(attr_id_t id, void *pv, size_t vlen);

/**
 * @brief Set a string
 *
 * @param id an attribute id.
 * @param pv string representation of variable
 * @param vlen The length (without nul char) of the
 * string being passed in.
 *
 * @retval negative error code, 0 on success
 */
int attr_set_string(attr_id_t id, char const *pv, size_t vlen);

/**
 * @brief Set an array
 *
 * @param id an attribute id.
 * @param pv array
 * @param vlen The length of the array.
 *
 * @retval negative error code, 0 on success
 */
int attr_set_byte_array(attr_id_t id, char const *pv, size_t vlen);

/**
 * @brief Get pointer to quasi-static item.
 *
 * @param id an attribute id.
 *
 * @retval pointer if found, pointer to empty string if not found
 */

void *attr_get_quasi_static(attr_id_t id);

/**
 * @brief Copy a string
 *
 * @param pv pointer to location to copy string
 * @param id an attribute id.
 * @param max_len is the size of pv.
 *
 * @retval negative error code, 0 on success
 */
int attr_copy_string(char *pv, attr_id_t id, size_t max_len);

/**
 * @brief Helper function for setting uint64
 *
 * @param id an attribute id.
 * @param value The value to set.
 *
 * @retval negative error code, 0 on success
 */
int attr_set_uint64(attr_id_t id, uint64_t value);

/**
 * @brief Helper function for setting int64
 *
 * @param id an attribute id.
 * @param value The value to set.
 *
 * @retval negative error code, 0 on success
 */
int attr_set_signed64(attr_id_t id, int64_t value);

/**
 * @brief Helper function for setting uint8, 16 or 32
 *
 * @param id an attribute id.
 * @param value The value to set.
 *
 * @retval negative error code, 0 on success
 */
int attr_set_uint32(attr_id_t id, uint32_t value);

/**
 * @brief Helper function for setting int8, int16, or int32
 *
 * @param id an attribute id.
 * @param value The value to set.
 *
 * @retval negative error code, 0 on success
 */
int attr_set_signed32(attr_id_t id, int32_t value);

/**
 * @brief Accessor Function for uint64
 *
 * @param pv pointer to data
 * @param id an attribute id
 *
 * @retval negative error code, 0 on success
 */
int attr_copy_uint64(uint64_t *pv, attr_id_t id);

/**
 * @brief Accessor Function for int64
 *
 * @note Sign extends when underlying type isn't int64
 *
 * @param pv pointer to data
 * @param id an attribute id
 *
 * @retval negative error code, 0 on success
 */
int attr_copy_signed64(uint64_t *pv, attr_id_t id);

/**
 * @brief Accessor Function for uint32 (uint8, uint16, bool)
 *
 * @param pv pointer to data
 * @param id an attribute id
 *
 * @retval negative error code, 0 on success
 */
int attr_copy_uint32(uint32_t *pv, attr_id_t id);

/**
 * @brief Accessor Function for int32 (int8, int16)
 *
 * @note Sign extends when underlying type isn't int32
 *
 * @param pv pointer to data
 * @param id an attribute id
 *
 * @retval negative error code, 0 on success
 */
int attr_copy_signed32(int32_t *pv, attr_id_t id);

/**
 * @brief Used to set the value of a floating point attribute
 *
 * @param id an attribute id.
 * @param value The value to set.
 *
 * @retval negative error code, 0 on success
 */
int attr_set_float(attr_id_t id, float value);

/**
 * @brief Accessor Function for float
 *
 * @param pv pointer to data
 * @param id an attribute id
 *
 * @retval negative error code, 0 on success
 */
int attr_copy_float(float *pv, attr_id_t id);

/**
 * @brief Alternate Accessor function for uint64
 *
 * @param id an attribute id
 * @param alt value
 *
 * @retval alt value if not found, invalid id, or wrong type;
 * otherwise the attribute value
 */
uint64_t attr_get_uint64(attr_id_t id, uint64_t alt);

/**
 * @brief Alternate Accessor function for int64
 *
 * @note Sign extends when underlying type isn't int64
 *
 * @param id an attribute id
 * @param alt value
 *
 * @retval alt value if not found, invalid id, or wrong type;
 * otherwise the attribute value
 */
int64_t attr_get_signed64(attr_id_t id, int64_t alt);

/**
 * @brief Alternate Accessor function for uint32
 *
 * @param id an attribute id
 * @param alt value
 *
 * @retval alt value if not found, invalid id, or wrong type;
 * otherwise the attribute value
 */
uint32_t attr_get_uint32(attr_id_t id, uint32_t alt);

/**
 * @brief Alternate Accessor function for int32
 *
 * @note Sign extends when underlying type isn't int32
 *
 * @param id an attribute id
 * @param alt value
 *
 * @retval alt value if not found, invalid id, or wrong type;
 * otherwise the attribute value
 */
int32_t attr_get_signed32(attr_id_t id, int32_t alt);

/**
 * @brief Alternate Accessor function for float
 *
 * @param id an attribute id
 * @param alt value
 *
 * @retval alt value if not found, invalid id, or wrong type;
 * otherwise the attribute value
 */
float attr_get_float(attr_id_t id, float alt);

/**
 * @brief Get the name of an attribute
 *
 * @param id an attribute id
 *
 * @retval empty string if not found
 */
const char *attr_get_name(attr_id_t id);

/**
 * @brief Get the size of an attribute
 *
 * @param id an attribute id
 *
 * @retval size of attribute, size with null if string
 */
size_t attr_get_size(attr_id_t id);

/**
 * @brief Set/Clear bit in a 32-bit attribute
 *
 * @param id an attribute id
 * @param bit location to set
 * @param value 0 for clear, any other value for set
 *
 * @retval size of attribute, size with null if string
 */
int attr_set_mask32(attr_id_t id, uint8_t bit, uint8_t value);

/**
 * @brief Set/Clear bit in an 64-bit attribute
 *
 * @param id an attribute id
 * @param bit location to set
 * @param value 0 for clear, any other value for set
 *
 * @retval size of attribute, size with null if string
 */
int attr_set_mask64(attr_id_t id, uint8_t bit, uint8_t value);

#ifdef CONFIG_ATTR_SHELL
/**
 * @brief Get the id of an attribute
 *
 * @param name of the attribute
 *
 * @retval attr_id_t id of attribute
 */
attr_id_t attr_get_id(const char *name);

/**
 * @brief Print the value of an attribute (LOG_DBG)
 *
 * @param id an attribute id
 *
 * @retval negative error code, 0 on success
 */
int attr_show(attr_id_t id);

/**
 * @brief Print all parameters to the console using system workq.
 *
 * @retval negative error code, 0 on success
 */
int attr_show_all(void);

/**
 * @brief Delete attribute file
 *
 * @note Can be used during development when layout of attributes has changed.
 *
 * @retval negative error code, 0 on success
 */
int attr_delete(void);

#endif /* CONFIG_ATTR_SHELL */

/**
 * @brief Print all parameters to the console using system workq.  The prepare
 * function is run for each attribute that it applies to.
 *
 * @param fstr pointer to file string
 * @param type the type of dump to perform
 *
 * @retval negative error code, number of parameters on success
 * If result is positive, then caller is responsbile for freeing fstr.
 */
int attr_prepare_then_dump(char **fstr, enum attr_dump type);

/**
 * @brief Set the quiet flag for an attribute.
 * Settings are saved to filesystem.
 *
 * @param id of attribute
 * @param value true to make quiet, false allows printing
 *
 * @retval negative error code, otherwise 0
 */
int attr_set_quiet(attr_id_t id, bool value);

/**
 * @brief Load attributes from a file and save them to params.txt
 *
 * @note SMP notifications are disabled during load.
 *
 * @param abs_path Absolute file name
 *
 * @retval negative error code, number of parameters on success
 */
int attr_load(const char *abs_path);

/**
 * @brief Notification callback
 *
 * @note override weak impelmentation in application
 *
 * @param id of attribute that has changed.
 *
 * @return int negative error code, zero on success
 */
int attr_notify(attr_id_t id);

/**
 * @brief Set the notify flag
 *
 * @param id of attribute
 * @param value new state
 *
 * @return int negative error code, 0 on success
 */
int attr_set_notify(attr_id_t id, bool value);

/**
 * @brief Accessor
 *
 * @param id of attribute
 *
 * @return bool true if attribute is notifiable over BLE, false if it
 * isn't or id isn't valid.
 */
bool attr_get_notify(attr_id_t id);

/**
 * @brief Disable all notifications
 *
 * @return int negative error code, 0 on success
 */
int attr_disable_notify(void);

/**
 * @brief Set an entry to its default value
 *
 * @param id of attribute
 *
 * @return int 0 on success, else negative errno.
 */
int attr_default(attr_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __ATTR_H__ */
