/**
 * @file ethernet_network.h
 * @brief Abstraction layer between ethernet network driver and application.
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __ETHERNET_NETWORK_H__
#define __ETHERNET_NETWORK_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
enum ethernet_event { ETHERNET_EVT_READY, ETHERNET_EVT_DISCONNECTED };

/* Callback function for ethernet events */
typedef void (*ethernet_event_function_t)(enum ethernet_event event);

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Init network
 *
 * @retval 0 on success, ethernet_init_status enum on failure.
 */
int ethernet_init(void);

/**
* @brief Accessor
 */
bool ethernet_ready(void);

/**
 * @brief Accessor
 */
bool ethernet_connected(void);

/**
 * @brief Callback from ethernet driver that can be implemented in application.
 */
void ethernet_event(enum ethernet_event event);

#ifdef __cplusplus
}
#endif

#endif /* __ETHERNET_NETWORK_H__ */
