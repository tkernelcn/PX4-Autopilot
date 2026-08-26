/****************************************************************************
 *
 *   ESP32-S3 PWM via direct LEDC register access (PX4 layer, no NuttX driver).
 *   Architecture follows boards/espressif/esp32/io_pins/pwm_servo.c.
 *
 ****************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <stdbool.h>
#include <stdint.h>

#include <arch/board/board.h>
#include <drivers/drv_pwm_output.h>

#include <px4_arch/io_timer.h>

#include "esp32s3_gpio.h"
#include "xtensa.h"
#include "hardware/esp32s3_system.h"
#include "hardware/esp32s3_gpio_sigmap.h"
#include "esp32s3_ledc_pwm.h"

#define b16HALF  0x00008000U
#define b16toi(a) ((a) >> 16)

static uint32_t g_reload = LEDC_RELOAD_MAX;
static uint32_t g_timer_rate = 400;
static bool g_ledc_clk_enabled;

static void ledc_enable_clk(void)
{
	if (g_ledc_clk_enabled) {
		return;
	}

	irqstate_t flags = px4_enter_critical_section();
	ledc_setbits(SYSTEM_LEDC_CLK_EN, SYSTEM_PERIP_CLK_EN0_REG);
	ledc_resetbits(SYSTEM_LEDC_RST, SYSTEM_PERIP_RST_EN0_REG);
	putreg32(LEDC_CLK_RES_APB, LEDC_CONF_REG);
	ledc_setbits(LEDC_CLK_EN, LEDC_CONF_REG);
	g_ledc_clk_enabled = true;
	px4_leave_critical_section(flags);
}

static void setup_timer_frequency(unsigned timer, unsigned frequency)
{
	uint32_t reload;
	uint8_t shift;
	float prescaler;
	uint32_t integral_prescaler;
	uint32_t fractional_prescaler;
	uint64_t pwmclk = LEDC_CLK_APB_FREQ;

	LEDC_SET_TIMER_BITS(timer, LEDC_TIMER0_CONF_REG, LEDC_TIMER0_RST);

	for (reload = LEDC_RELOAD_MAX, shift = LEDC_RELOAD_MAX_BIT_LEN;
	     reload > 1;
	     reload >>= 1, shift--) {
		if (reload * frequency <= pwmclk) {
			break;
		}
	}

	prescaler = (float)pwmclk / (float)frequency / (float)reload;
	integral_prescaler = (uint32_t)prescaler;

	if (integral_prescaler == 0) {
		integral_prescaler = 1;
	}

	fractional_prescaler = (uint32_t)((prescaler - (float)integral_prescaler) * 256.f);
	g_reload = reload;
	g_timer_rate = frequency;

	irqstate_t flags = px4_enter_critical_section();

	uint32_t regval = ((uint32_t)shift << LEDC_TIMER0_DUTY_RES_S) |
			  (fractional_prescaler << LEDC_CLK_DIV_TIMER0_S) |
			  (integral_prescaler << (LEDC_CLK_DIV_TIMER0_S + 8));

	LEDC_SET_TIMER_REG(timer, LEDC_TIMER0_CONF_REG, regval);
	LEDC_SET_TIMER_BITS(timer, LEDC_TIMER0_CONF_REG, LEDC_TIMER0_PARA_UP);

	px4_leave_critical_section(flags);
}

static void setup_channel_duty(unsigned channel, uint16_t value)
{
	uint32_t regval = b16toi((uint32_t)value * g_reload + b16HALF);

	irqstate_t flags = px4_enter_critical_section();

	LEDC_SET_CHAN_REG(channel, LEDC_CH0_CONF0_REG, 0);
	LEDC_SET_CHAN_REG(channel, LEDC_CH0_CONF1_REG, 0);
	LEDC_SET_CHAN_REG(channel, LEDC_CH0_CONF0_REG, io_timers[0].base);
	LEDC_SET_CHAN_REG(channel, LEDC_CH0_HPOINT_REG, 0);
	LEDC_SET_CHAN_REG(channel, LEDC_CH0_DUTY_REG, regval << 4);
	LEDC_SET_CHAN_BITS(channel, LEDC_CH0_CONF0_REG, LEDC_SIG_OUT_EN_CH0);
	LEDC_SET_CHAN_BITS(channel, LEDC_CH0_CONF1_REG, LEDC_DUTY_START_CH0);
	LEDC_SET_CHAN_BITS(channel, LEDC_CH0_CONF0_REG, LEDC_PARA_UP_CH0);

	px4_leave_critical_section(flags);
}

int up_pwm_servo_set(unsigned channel, uint16_t value)
{
	if (channel >= DIRECT_PWM_OUTPUT_CHANNELS) {
		return PX4_ERROR;
	}

	setup_channel_duty(channel, value);
	return OK;
}

uint16_t up_pwm_servo_get(unsigned channel)
{
	(void)channel;
	return 0;
}

int up_pwm_servo_init(uint32_t channel_mask)
{
	(void)channel_mask;

	ledc_enable_clk();
	setup_timer_frequency(io_timers[0].base, 400);

	return OK;
}

void up_pwm_servo_deinit(uint32_t channel_mask)
{
	up_pwm_servo_arm(false, channel_mask);
}

int up_pwm_servo_set_rate_group_update(unsigned group, unsigned rate)
{
	if (group == 0) {
		setup_timer_frequency(io_timers[0].base, rate);
		return OK;
	}

	return ERROR;
}

void up_pwm_update(unsigned channels_mask)
{
	(void)channels_mask;
}

uint32_t up_pwm_servo_get_rate_group(unsigned group)
{
	if (group == 0) {
#if defined(BOARD_PWM_TIM0_CHANNELS)
		return (1U << BOARD_PWM_TIM0_CHANNELS) - 1U;
#endif
	}

	return 0;
}

void up_pwm_servo_arm(bool armed, uint32_t channel_mask)
{
	if (armed) {
		for (unsigned chan = 0; chan < DIRECT_PWM_OUTPUT_CHANNELS; chan++) {
			if ((channel_mask & (1U << chan)) == 0) {
				continue;
			}

			irqstate_t flags = px4_enter_critical_section();

			LEDC_SET_CHAN_REG(chan, LEDC_CH0_CONF0_REG, 0);
			LEDC_SET_CHAN_REG(chan, LEDC_CH0_CONF1_REG, 0);
			LEDC_SET_CHAN_REG(chan, LEDC_CH0_HPOINT_REG, 0);
			LEDC_SET_CHAN_BITS(chan, LEDC_CH0_CONF0_REG, LEDC_SIG_OUT_EN_CH0);
			LEDC_SET_CHAN_BITS(chan, LEDC_CH0_CONF1_REG, LEDC_DUTY_START_CH0);
			LEDC_SET_CHAN_BITS(chan, LEDC_CH0_CONF0_REG, LEDC_PARA_UP_CH0);

			px4_leave_critical_section(flags);
		}

	} else {
		irqstate_t flags = px4_enter_critical_section();
		LEDC_SET_TIMER_BITS(io_timers[0].base, LEDC_TIMER0_CONF_REG, LEDC_TIMER0_RST);
		px4_leave_critical_section(flags);
	}
}
