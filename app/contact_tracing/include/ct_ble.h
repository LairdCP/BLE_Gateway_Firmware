/**
 * @file ct_ble.h
 * @brief Contact Tracing
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __CT_BLE_H__
#define __CT_BLE_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#include "sensor_state.h"

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief Function for initialising the BLE portion of the Contract Tracing app.
 *
 */
void ct_ble_initialize(void);

/**
 * @brief Button handler that will cause connectable advertising.
 *
 * @retval negative error code, 0 on success
 */
int ct_adv_on_button_isr(void);

/**
 * @brief Returns state of machine looking for CT sensors
 *
 * @retval sensor state
 */
enum sensor_state ct_ble_get_state(void);

/**
 * @brief SMP echo command used for testing.
 */
void ct_ble_smp_echo_test(void);

/**
 * @brief Checks state to determine if next request should be sent.
 *
 * @retval true if next request can be sent, false otherwise.
 */
bool ct_ble_send_next_smp_request(uint32_t new_off);

/**
 * @brief Setter
 *
 * @param nwkId network ID used in advertisements.  Saved in nv.
 */
void ct_ble_set_network_id(uint16_t nwkId);

/**
 * @brief Determine if entries that weren't able to be sent can now be
 * sent to AWS.
 */
void ct_ble_check_stashed_log_entries(void);

/**
 * @brief Accessor
 *
 * @retval true if logs are being published to AWS, false otherwise
 */
bool ct_ble_is_publishing_log(void);

/**
 * @brief Accessors used with CT shell.
 */
bool ct_ble_get_log_transfer_active_flag(void);
bool ct_ble_is_connected_to_sensor(void);
bool ct_ble_is_connected_to_central(void);
uint32_t ct_ble_get_num_connections(void);
uint32_t ct_ble_get_num_ct_dl_starts(void);
uint32_t ct_ble_get_num_download_completes(void);
uint32_t ct_ble_get_num_scan_results(void);
uint32_t ct_ble_get_num_ct_scan_results(void);

/**
 * @brief Build topics used by CT.  Reads AWS topic prefix.
 */
void ct_ble_topic_builder(void);

/**
 * @brief Accessor
 *
 * @retval log topic string
 */
char *ct_ble_get_log_topic(void);

/**
 * @brief Publish dummy data to AWS.  Used to generate data on first connection
 * because there might not always be sensor data to send.
 *
 * @retval negative error code, 0 on success
 */
int ct_ble_publish_dummy_data_to_aws(void);

#ifdef __cplusplus
}
#endif

#endif /* __CT_BLE_H__ */
