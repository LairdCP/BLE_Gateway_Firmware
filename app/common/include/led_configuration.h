/**
 * @file led_configuration.h
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LED_CONFIGURATION_H__
#define __LED_CONFIGURATION_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include "lcz_led.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Board definitions                                                          */
/******************************************************************************/

#if defined(CONFIG_BOARD_MG100) || defined(CONFIG_BOARD_PINNACLE_100_DVK)
/* Pinnacle 100 DVK or MG100 */
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

enum led_type_index {
	BLUETOOTH_PERIPHERAL_LED = BLUE_LED,
	NETWORK_LED = RED_LED,
	CLOUD_LED = GREEN_LED,
#ifdef CONFIG_BOARD_PINNACLE_100_DVK
	NET_MGMT_LED = GREEN_LED2
#endif
};

#ifdef CONFIG_BOARD_PINNACLE_100_DVK
BUILD_ASSERT(CONFIG_LCZ_NUMBER_OF_LEDS > GREEN_LED2, "LED object too small");
#else
BUILD_ASSERT(CONFIG_LCZ_NUMBER_OF_LEDS > RED_LED, "LED object too small");
#endif
#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP) ||                               \
	defined(CONFIG_BOARD_BL5340_DVK_CPUAPP_NS) ||                          \
	defined(CONFIG_BOARD_BL5340PA_DVK_CPUAPP) ||                           \
	defined(CONFIG_BOARD_BL5340PA_DVK_CPUAPP_NS)
/* BL5340 DVK */
#define LED_NODE "tca9538"

#define LED1_DEV LED_NODE
#define LED1 4
#define LED2_DEV LED_NODE
#define LED2 5
#define LED3_DEV LED_NODE
#define LED3 6
#define LED4_DEV LED_NODE
#define LED4 7

#define HAS_SECOND_BLUETOOTH_LED

enum led_index {
	BLUE_LED1 = 0,
	BLUE_LED2,
	BLUE_LED3,
	BLUE_LED4,
};

enum led_type_index {
	BLUETOOTH_PERIPHERAL_LED = BLUE_LED1,
	BLUETOOTH_LED = BLUE_LED2,
	NETWORK_LED = BLUE_LED3,
	CLOUD_LED = BLUE_LED4
};
BUILD_ASSERT(CONFIG_LCZ_NUMBER_OF_LEDS > BLUE_LED4, "LED object too small");
#else
#error "Unsupported board selected"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __LED_CONFIGURATION_H__ */
