/**
 * @file led_configuration.h
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LED_CONFIGURATION_H__
#define __LED_CONFIGURATION_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "laird_led.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Board definitions                                                          */
/******************************************************************************/

#define LED1_NODE DT_ALIAS(led0)
#define LED2_NODE DT_ALIAS(led1)
#define LED3_NODE DT_ALIAS(led2)
#ifdef CONFIG_BOARD_PINNACLE_100_DVK
#define LED4_NODE DT_ALIAS(led3)
#endif

#define LED1_DEV DT_GPIO_LABEL(LED1_NODE, gpios)
#define LED1 DT_GPIO_PIN(LED1_NODE, gpios)
#define LED2_DEV DT_GPIO_LABEL(LED2_NODE, gpios)
#define LED2 DT_GPIO_PIN(LED2_NODE, gpios)
#define LED3_DEV DT_GPIO_LABEL(LED3_NODE, gpios)
#define LED3 DT_GPIO_PIN(LED3_NODE, gpios)
#ifdef CONFIG_BOARD_PINNACLE_100_DVK
#define LED4_DEV DT_GPIO_LABEL(LED4_NODE, gpios)
#define LED4 DT_GPIO_PIN(LED4_NODE, gpios)
#endif

enum led_index {
	BLUE_LED = 0,
	GREEN_LED,
	RED_LED,
#ifdef CONFIG_BOARD_PINNACLE_100_DVK
	GREEN_LED2
#endif
};

#ifdef CONFIG_BOARD_PINNACLE_100_DVK
BUILD_ASSERT(CONFIG_NUMBER_OF_LEDS > GREEN_LED2, "LED object too small");
#else
BUILD_ASSERT(CONFIG_NUMBER_OF_LEDS > RED_LED, "LED object too small");
#endif

#ifdef __cplusplus
}
#endif

#endif /* __LED_CONFIGURATION_H__ */
