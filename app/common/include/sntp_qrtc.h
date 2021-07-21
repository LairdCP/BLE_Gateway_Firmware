/**
 * @file sntp_qrtc.h
 * @brief SNTP QRTC time syncronisation.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SNTP_QRTC_H__
#define __SNTP_QRTC_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
enum sntp_qrtc_event { SNTP_QRTC_UPDATING, SNTP_QRTC_UPDATED, SNTP_QRTC_UPDATE_FAILED };

/* Callback function for SNTP QRTC events */
typedef void (*sntp_qrtc_event_function_t)(enum sntp_qrtc_event event);

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/

/**
 * @brief Init SNTP QRTC system
 */
void sntp_qrtc_init(void);

/**
 * @brief Update QRTC from SNTP server
 *
 * @retval false if failed (no internet connection), true if succesfully submitted
 */
bool sntp_qrtc_update_time(void);

/**
 * @brief Update QRTC from SNTP server after a short delay
 */
void sntp_qrtc_start_delay(void);

/**
 * @brief Stop pending delayed QRTC update from SNTP server
 */
void sntp_qrtc_stop(void);

/**
 * @brief Callback from ethernet driver that can be implemented in application.
 * @note Called from the SNTP QRTC update thread context
 */
void sntp_qrtc_event(enum sntp_qrtc_event event);

#ifdef __cplusplus
}
#endif

#endif /* __ETHERNET_NETWORK_H__ */
