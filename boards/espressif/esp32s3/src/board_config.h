/****************************************************************************
 *
 *   Copyright (c) 2013-2016 PX4 Development Team. All rights reserved.
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
 *    without specific written permission.
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
 * ESP32-S3 WROOM board definitions (espressif_esp32s3_default).
 */

#pragma once

#include <px4_platform_common/px4_config.h>
#include <nuttx/compiler.h>
#include <stdint.h>

/* LEDs — GPIO6 = state (blink), GPIO7 = armed (solid) */

#define GPIO_LED_BLUE                (GPIO_OUTPUT|7)
#define GPIO_LED_GREEN               (GPIO_OUTPUT|6)

#define BOARD_HAS_CONTROL_STATUS_LEDS 1
#define BOARD_ARMED_LED        LED_BLUE
#define BOARD_ARMED_STATE_LED  LED_GREEN

#define HRT_TIMER                    2  /* Timer Group 1 Timer 0 (avoid TG0 / SYSTIMER) */

/*
 * Defer PX4 I2C general-call during px4_platform_init(); bus attach is done
 * by sensor drivers in rc.board_sensors.
 */
#define BOARD_I2C_LATEINIT 1

#define BOARD_ENABLE_MTD_PARAMS 1

#define BOARD_DATAMAN_STACK_SIZE 2560
#define BOARD_WQ_MANAGER_STACK_SIZE 2560
#define BOARD_NAVIGATOR_STACK_SIZE 4096
#define BOARD_MAVLINK_MAIN_STACK_SIZE 4096
#define BOARD_MAVLINK_RECEIVER_STACK_EXTRA 2048
#define BOARD_MAVLINK_SHELL_STACK_SIZE 8192

#define BOARD_SPI_BUS_MAX_BUS_ITEMS 4

/* SPI2 BMI088: SPI::CS{n} is the GPIO number (per-device CS via CONFIG_ESP32S3_SPI_UDCS). */
#define BOARD_SPI2_CS_BMI088_ACCEL  1
#define BOARD_SPI2_CS_BMI088_GYRO   38
#define BOARD_SPI2_DRDY_BMI088      40

#define ADC_BATTERY_VOLTAGE_CHANNEL   4
#define ADC_BATTERY_CURRENT_CHANNEL  2

#define ADC_CHANNELS \
	((1 << ADC_BATTERY_VOLTAGE_CHANNEL) | \
	 (1 << ADC_BATTERY_CURRENT_CHANNEL))

#define CONFIG_ESP32S3_ADC_VOL_3100 1
#define CONFIG_ESP32S3_ADC1_CHANNEL4 1

#define ADC_V5_V_FULL_SCALE (7.17f)

#define GPIO_HEATER_OUTPUT (GPIO_OUTPUT | 46)
#define HEATER_OUTPUT_EN(on_true) px4_arch_gpiowrite(GPIO_HEATER_OUTPUT, (on_true))

/* PWM outputs (LEDC — see platforms/nuttx/.../esp32s3/io_pins/pwm_servo.c) */
#define BOARD_PWM_TIM0_CHANNELS        4
#define BOARD_PWM_CHANNEL0_PIN         10
#define BOARD_PWM_CHANNEL1_PIN         9
#define BOARD_PWM_CHANNEL2_PIN         37
#define BOARD_PWM_CHANNEL3_PIN         13

#define BOARD_NUM_IO_TIMERS            1
#define DIRECT_PWM_OUTPUT_CHANNELS     4
#define BOARD_HAS_PWM                  DIRECT_PWM_OUTPUT_CHANNELS

/* @todo Wire brick/USB valid GPIOs and replace hard-coded always-present values. */
#define BOARD_ADC_USB_CONNECTED      1
#define BOARD_ADC_BRICK_VALID        1
#define BOARD_ADC_USB_VALID          1

__BEGIN_DECLS

#ifndef __ASSEMBLY__

extern void esp32s3_spiinitialize(void);
extern void board_peripheral_reset(int ms);

#include <px4_platform_common/board_common.h>

#endif /* __ASSEMBLY__ */

__END_DECLS
