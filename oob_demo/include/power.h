/*
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef POWER_H
#define POWER_H

//=============================================================================
// Includes
//=============================================================================

#include <zephyr/types.h>

//=============================================================================
// Board definitions
//=============================================================================

/* Port and pin number of the voltage measurement enabling functionality */
#define MEASURE_ENABLE_PORT		DT_NORDIC_NRF_GPIO_0_LABEL
#define MEASURE_ENABLE_PIN		28

/* Measurement time between readings */
#define POWER_TIMER_PERIOD		K_MSEC(15000)

//=============================================================================
// Global Function Prototypes
//=============================================================================

/**
 * @brief Init the power measuring system
 */
void power_init(void);

/**
 * @brief Enables or disables the power measurement system
 * @param true to enable, false to disable
 */
void power_mode_set(bool enable);

#endif /* POWER_H */
