/**
 * @file lcd.h
 * @brief Displays demo text on a connected display
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCD_H__
#define __LCD_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Function Prototypes                                                 */
/******************************************************************************/
/**
 * @brief Sets up the connected LCD ready to display details
 */
void lcd_display_init(void);

/**
 * @brief Updates text on the display with attribute data
 */
void lcd_display_update_details(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H__ */
