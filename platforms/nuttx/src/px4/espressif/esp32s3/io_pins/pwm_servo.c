/****************************************************************************
 *
 *   ESP32-S3 brushed motor PWM via direct LEDC register access.
 *   Board-specific output units: 0..BOARD_PWM_DRIVE_FULL_SCALE = 0..100% duty.
 *   Unlike a servo/ESC backend, these values are not pulse widths in us.
 *
 ****************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <drivers/drv_pwm_output.h>
#include <px4_arch/io_timer.h>

#include "xtensa.h"
#include "hardware/esp32s3_system.h"
#include "esp32s3_ledc_pwm.h"

#define PWM_CHANNEL_MASK ((1U << DIRECT_PWM_OUTPUT_CHANNELS) - 1U)

static uint32_t g_reload = LEDC_RELOAD_MAX;
static uint32_t g_initialized_mask;
static uint32_t g_enabled_mask;
static uint16_t g_values[DIRECT_PWM_OUTPUT_CHANNELS];
static bool g_ledc_clk_enabled;

/* All helpers are called with interrupts locked. */
static void ledc_enable_clk(void)
{
	if (!g_ledc_clk_enabled) {
		ledc_setbits(SYSTEM_LEDC_CLK_EN, SYSTEM_PERIP_CLK_EN0_REG);
		ledc_resetbits(SYSTEM_LEDC_RST, SYSTEM_PERIP_RST_EN0_REG);
		putreg32(LEDC_CLK_RES_APB | LEDC_CLK_EN, LEDC_CONF_REG);
		g_ledc_clk_enabled = true;
	}
}

static void setup_channel_duty(unsigned channel)
{
	const unsigned hw_channel = timer_io_channels[channel].timer_channel;
	const uint16_t value = (g_enabled_mask & (1U << channel)) ? g_values[channel] : 0;
	uint32_t config = io_timers[0].base;
	uint32_t duty = 0;

	if (value >= BOARD_PWM_DRIVE_FULL_SCALE) {
		/* Constant high avoids the LEDC maximum-resolution duty overflow. */
		config |= LEDC_IDLE_LV_CH0;

	} else if (value > 0) {
		duty = ((uint32_t)value * g_reload + BOARD_PWM_DRIVE_FULL_SCALE / 2) / BOARD_PWM_DRIVE_FULL_SCALE;
		config |= LEDC_SIG_OUT_EN_CH0;
	}

	/* Keep the output enabled during normal duty updates. Zero/disabled uses
	 * the low idle level, without stopping the shared timer or other motors.
	 */
	LEDC_SET_CHAN_REG(hw_channel, LEDC_CH0_DUTY_REG, duty << 4);
	LEDC_SET_CHAN_REG(hw_channel, LEDC_CH0_CONF1_REG,
			 LEDC_DUTY_START_CH0 | LEDC_DUTY_INC_CH0 |
			 (1U << LEDC_DUTY_NUM_CH0_S) | (1U << LEDC_DUTY_CYCLE_CH0_S));
	LEDC_SET_CHAN_REG(hw_channel, LEDC_CH0_CONF0_REG, config | LEDC_PARA_UP_CH0);
}

static int setup_timer_frequency(unsigned frequency)
{
	/* Continuous PWM only. In particular, reject OneShot's rate=0. */
	if (frequency < 10 || frequency > 40000) {
		return -ERANGE;
	}

	unsigned resolution = LEDC_RELOAD_MAX_BIT_LEN;
	uint32_t divider = 0;

	for (; resolution > 0; --resolution) {
		const uint64_t denominator = (uint64_t)frequency * (1U << resolution);
		divider = ((uint64_t)LEDC_CLK_APB_FREQ * 256U + denominator / 2) / denominator;

		if (divider >= LEDC_CLK_DIV_MIN && divider <= LEDC_CLK_DIV_MAX) {
			break;
		}
	}

	if (resolution == 0) {
		return -ERANGE;
	}

	g_reload = 1U << resolution;
	const unsigned timer = io_timers[0].base;
	const uint32_t config = (resolution << LEDC_TIMER0_DUTY_RES_S) | (divider << LEDC_CLK_DIV_TIMER0_S);
	LEDC_SET_TIMER_REG(timer, LEDC_TIMER0_CONF_REG, config | LEDC_TIMER0_RST | LEDC_TIMER0_PARA_UP);
	LEDC_SET_TIMER_REG(timer, LEDC_TIMER0_CONF_REG, config | LEDC_TIMER0_PARA_UP);

	/* Recalculate duty counts if the timer resolution changed. */
	for (unsigned channel = 0; channel < DIRECT_PWM_OUTPUT_CHANNELS; ++channel) {
		if (g_initialized_mask & (1U << channel)) {
			setup_channel_duty(channel);
		}
	}

	return OK;
}

int up_pwm_servo_set(unsigned channel, uint16_t value)
{
	if (channel >= DIRECT_PWM_OUTPUT_CHANNELS) {
		return -EINVAL;
	}

	irqstate_t flags = px4_enter_critical_section();

	if (!(g_initialized_mask & (1U << channel))) {
		px4_leave_critical_section(flags);
		return -EINVAL;
	}

	if (value != PWM_IGNORE_THIS_CHANNEL) {
		g_values[channel] = value > BOARD_PWM_DRIVE_FULL_SCALE ? BOARD_PWM_DRIVE_FULL_SCALE : value;
		setup_channel_duty(channel);
	}

	px4_leave_critical_section(flags);
	return OK;
}

uint16_t up_pwm_servo_get(unsigned channel)
{
	irqstate_t flags = px4_enter_critical_section();
	uint16_t value = 0;

	if (channel < DIRECT_PWM_OUTPUT_CHANNELS && (g_enabled_mask & (1U << channel))) {
		value = g_values[channel];
	}

	px4_leave_critical_section(flags);
	return value;
}

int up_pwm_servo_init(uint32_t channel_mask)
{
	channel_mask &= PWM_CHANNEL_MASK;

	if (channel_mask == 0) {
		return 0;
	}

	irqstate_t flags = px4_enter_critical_section();
	ledc_enable_clk();

	if (g_initialized_mask == 0) {
		setup_timer_frequency(400);
	}

	for (unsigned channel = 0; channel < DIRECT_PWM_OUTPUT_CHANNELS; ++channel) {
		if ((channel_mask & (1U << channel)) && !(g_initialized_mask & (1U << channel))) {
			g_values[channel] = 0;
			LEDC_SET_CHAN_REG(timer_io_channels[channel].timer_channel, LEDC_CH0_HPOINT_REG, 0);
			setup_channel_duty(channel);
		}
	}

	g_initialized_mask |= channel_mask;
	px4_leave_critical_section(flags);
	/* PWMOut consumes this return value as a channel mask, not a status. */
	return channel_mask;
}

void up_pwm_servo_deinit(uint32_t channel_mask)
{
	irqstate_t flags = px4_enter_critical_section();
	channel_mask = channel_mask ? channel_mask & g_initialized_mask : g_initialized_mask;

	if (channel_mask != 0) {
		up_pwm_servo_arm(false, channel_mask);
	}

	g_initialized_mask &= ~channel_mask;
	px4_leave_critical_section(flags);
}

int up_pwm_servo_set_rate_group_update(unsigned group, unsigned rate)
{
	if (group != 0) {
		return -EINVAL;
	}

	irqstate_t flags = px4_enter_critical_section();
	int ret = g_initialized_mask ? setup_timer_frequency(rate) : -EINVAL;
	px4_leave_critical_section(flags);
	return ret;
}

void up_pwm_update(unsigned channels_mask)
{
	/* Continuous LEDC PWM latches channel updates at the next period. */
	(void)channels_mask;
}

uint32_t up_pwm_servo_get_rate_group(unsigned group)
{
	return group == 0 ? PWM_CHANNEL_MASK : 0;
}

void up_pwm_servo_arm(bool armed, uint32_t channel_mask)
{
	irqstate_t flags = px4_enter_critical_section();
	channel_mask = channel_mask ? channel_mask & g_initialized_mask : g_initialized_mask;

	if (armed) {
		g_enabled_mask |= channel_mask;

	} else {
		g_enabled_mask &= ~channel_mask;
	}

	for (unsigned channel = 0; channel < DIRECT_PWM_OUTPUT_CHANNELS; ++channel) {
		if (channel_mask & (1U << channel)) {
			if (!armed) {
				g_values[channel] = 0;
			}

			setup_channel_duty(channel);
		}
	}

	px4_leave_critical_section(flags);
}
