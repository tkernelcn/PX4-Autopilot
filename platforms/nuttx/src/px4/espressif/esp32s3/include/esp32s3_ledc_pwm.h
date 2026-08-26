/****************************************************************************
 *
 *   ESP32-S3 LEDC register definitions for PX4 register-level PWM.
 *   Lives in the PX4 tree; NuttX is not modified.
 *
 ****************************************************************************/

#pragma once

#include <nuttx/arch.h>
#include "hardware/esp32s3_soc.h"

#ifndef BIT
#  define BIT(n) (1UL << (n))
#endif

/* Channel register block stride */
#define LEDC_CH0_CONF0_REG   (DR_REG_LEDC_BASE + 0x0)
#define LEDC_CH0_HPOINT_REG  (DR_REG_LEDC_BASE + 0x4)
#define LEDC_CH0_DUTY_REG    (DR_REG_LEDC_BASE + 0x8)
#define LEDC_CH0_CONF1_REG   (DR_REG_LEDC_BASE + 0xc)
#define LEDC_CH1_CONF0_REG   (DR_REG_LEDC_BASE + 0x14)

#define LEDC_TIMER0_CONF_REG (DR_REG_LEDC_BASE + 0xa0)
#define LEDC_TIMER1_CONF_REG (DR_REG_LEDC_BASE + 0xa8)

#define LEDC_CONF_REG        (DR_REG_LEDC_BASE + 0xd0)

#define LEDC_PARA_UP_CH0     BIT(4)
#define LEDC_SIG_OUT_EN_CH0  BIT(2)
#define LEDC_DUTY_START_CH0  BIT(31)

#define LEDC_TIMER0_RST      BIT(23)
#define LEDC_TIMER0_PARA_UP  BIT(25)
#define LEDC_TIMER0_DUTY_RES_S  0
#define LEDC_CLK_DIV_TIMER0_S 4

#define LEDC_CLK_EN          BIT(31)
#define LEDC_CLK_RES_APB     1

#define LEDC_RELOAD_MAX           16384
#define LEDC_RELOAD_MAX_BIT_LEN   14
#define LEDC_CLK_APB_FREQ         (80UL * 1000000UL)

#define LEDC_TIMER_REG(r, n) \
	((r) + (n) * (LEDC_TIMER1_CONF_REG - LEDC_TIMER0_CONF_REG))

#define LEDC_CHAN_REG(r, n) \
	((r) + (n) * (LEDC_CH1_CONF0_REG - LEDC_CH0_CONF0_REG))

#define ledc_setbits(bs, a)   modifyreg32((a), 0, (bs))
#define ledc_resetbits(bs, a) modifyreg32((a), (bs), 0)

#define LEDC_SET_TIMER_BITS(t, r, b) ledc_setbits((b), LEDC_TIMER_REG((r), (t)))
#define LEDC_SET_TIMER_REG(t, r, v)  putreg32((v), LEDC_TIMER_REG((r), (t)))

#define LEDC_SET_CHAN_BITS(c, r, b)  ledc_setbits((b), LEDC_CHAN_REG((r), (c)))
#define LEDC_SET_CHAN_REG(c, r, v)   putreg32((v), LEDC_CHAN_REG((r), (c)))
