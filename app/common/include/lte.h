/**
 * @file lte.h
 * @brief Abstraction layer between hl7800/network and application.
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LTE_H__
#define __LTE_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
enum lte_event { LTE_EVT_READY, LTE_EVT_DISCONNECTED };

/* Callback function for LTE events */
typedef void (*lte_event_function_t)(enum lte_event event);

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Init LTE modem and update modem information
 *
 * @retval 0 on success, lte_init_status enum on failure.
 */
int lte_init(void);

/**
 * @brief Init network
 *
 * @retval 0 on success, lte_init_status enum on failure.
 */
int lte_network_init(void);

/**
* @brief Accessor
 */
bool lte_ready(void);

/**
 * @brief Accessor
 */
bool lte_connected(void);

/**
 * @brief Callback from LTE module that can be implemented in application.
 *
 * @param event
 */
void lte_event_callback(enum lte_event event);

#ifdef __cplusplus
}
#endif

#endif /* __LTE_H__ */
