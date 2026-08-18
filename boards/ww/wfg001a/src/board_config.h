/****************************************************************************
 *
 *   Copyright (c) 2018, 2014 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file board_config.h
 *
 * WFG001A internal definitions
 */

#pragma once

/****************************************************************************************************
 * Included Files
 ****************************************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <nuttx/compiler.h>
#include <stdint.h>

/****************************************************************************************************
 * Definitions
 ****************************************************************************************************/

/* LEDs */
#define GPIO_LED1       (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_50MHz|GPIO_OUTPUT_CLEAR|GPIO_PORTC|GPIO_PIN14)
#define GPIO_LED_BLUE   GPIO_LED1

#define BOARD_OVERLOAD_LED     LED_BLUE

#define FLASH_BASED_PARAMS

/*
 * ADC channels
 */
#define ADC1_CH(n)                  (n)

#define PX4_ADC_GPIO  \
	/* PC0 */  GPIO_ADC1_IN10,   \
	/* PC1 */  GPIO_ADC1_IN11,   \
	/* PB1 */  GPIO_ADC1_IN9

#define ADC_BATTERY_VOLTAGE_CHANNEL  ADC1_CH(10)
#define ADC_BATTERY_CURRENT_CHANNEL  ADC1_CH(11)
#define ADC_RC_RSSI_CHANNEL          ADC1_CH(9)

#define ADC_CHANNELS \
	((1 << ADC_BATTERY_VOLTAGE_CHANNEL) | \
	 (1 << ADC_BATTERY_CURRENT_CHANNEL) | \
	 (1 << ADC_RC_RSSI_CHANNEL))

/* User GPIOs */
#define _MK_GPIO_INPUT(def) (((def) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_INPUT|GPIO_PULLUP))

#define GPIO_GPIO0_INPUT             _MK_GPIO_INPUT(GPIO_TIM12_CH1IN_2)
#define GPIO_GPIO1_INPUT             _MK_GPIO_INPUT(GPIO_TIM12_CH2IN_1)
#define GPIO_GPIO2_INPUT             _MK_GPIO_INPUT(GPIO_TIM8_CH3IN_2)
#define GPIO_GPIO3_INPUT             _MK_GPIO_INPUT(GPIO_TIM8_CH4IN_1)
#define GPIO_GPIO4_INPUT             _MK_GPIO_INPUT(GPIO_TIM2_CH2IN_2)
#define GPIO_GPIO5_INPUT             _MK_GPIO_INPUT(GPIO_TIM3_CH1IN_2)
#define GPIO_GPIO6_INPUT             _MK_GPIO_INPUT(GPIO_TIM4_CH1IN_1)
#define GPIO_GPIO7_INPUT             _MK_GPIO_INPUT(GPIO_TIM4_CH2IN_1)
#define GPIO_GPIO8_INPUT             _MK_GPIO_INPUT(GPIO_TIM8_CH1IN_1)
#define GPIO_GPIO9_INPUT             _MK_GPIO_INPUT(GPIO_TIM8_CH2IN_1)

#define _MK_GPIO_OUTPUT(def) (((def) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_2MHz|GPIO_OUTPUT_CLEAR))

#define GPIO_GPIO0_OUTPUT            _MK_GPIO_OUTPUT(GPIO_TIM12_CH1OUT_2)
#define GPIO_GPIO1_OUTPUT            _MK_GPIO_OUTPUT(GPIO_TIM12_CH2OUT_1)
#define GPIO_GPIO2_OUTPUT            _MK_GPIO_OUTPUT(GPIO_TIM8_CH3OUT_1)
#define GPIO_GPIO3_OUTPUT            _MK_GPIO_OUTPUT(GPIO_TIM8_CH4OUT_1)
#define GPIO_GPIO4_OUTPUT            _MK_GPIO_OUTPUT(GPIO_TIM2_CH2OUT_2)
#define GPIO_GPIO5_OUTPUT            _MK_GPIO_OUTPUT(GPIO_TIM3_CH1OUT_2)
#define GPIO_GPIO6_OUTPUT            _MK_GPIO_OUTPUT(GPIO_TIM4_CH1OUT_1)
#define GPIO_GPIO7_OUTPUT            _MK_GPIO_OUTPUT(GPIO_TIM4_CH2OUT_1)
#define GPIO_GPIO8_OUTPUT            _MK_GPIO_OUTPUT(GPIO_TIM8_CH1OUT_1)
#define GPIO_GPIO9_OUTPUT            _MK_GPIO_OUTPUT(GPIO_TIM8_CH2OUT_1)

/* USB OTG FS */
#define GPIO_OTGFS_VBUS		(GPIO_INPUT|GPIO_FLOAT|GPIO_SPEED_100MHz|GPIO_OPENDRAIN|GPIO_PORTC|GPIO_PIN5)

/* PWM outputs */
#define DIRECT_PWM_OUTPUT_CHANNELS      10
#define BOARD_HAS_PWM                   DIRECT_PWM_OUTPUT_CHANNELS
#define BOARD_NUM_IO_TIMERS             5

/* High-resolution timer */
#define HRT_TIMER                    5
#define HRT_TIMER_CHANNEL            1

#define HRT_PPM_CHANNEL              2
#define GPIO_PPM_IN                  GPIO_TIM5_CH2IN_1

#define RC_SERIAL_PORT               "/dev/ttyS1"
#define BOARD_SUPPORTS_RC_SERIAL_PORT_OUTPUT

/* This board provides a DMA pool and APIs */
#define BOARD_DMA_ALLOC_POOL_SIZE 5120

#define BOARD_HAS_ON_RESET 1

#define BOARD_ENABLE_CONSOLE_BUFFER
#define BOARD_CONSOLE_BUFFER_SIZE (1024*3)

__BEGIN_DECLS

#ifndef __ASSEMBLY__

extern void stm32_spiinitialize(void);
extern void stm32_usbinitialize(void);
extern void board_peripheral_reset(int ms);

#include <px4_platform_common/board_common.h>

#endif /* __ASSEMBLY__ */

__END_DECLS
