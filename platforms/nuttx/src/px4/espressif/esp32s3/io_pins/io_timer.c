/****************************************************************************
 *
 *   ESP32-S3 io_timer helpers (mirrors esp32/io_pins/io_timer.c).
 *
 ****************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <systemlib/px4_macros.h>

#include <arch/board/board.h>
#include <drivers/drv_pwm_output.h>

#include <px4_arch/io_timer.h>

uint32_t io_timer_get_group(unsigned timer)
{
	if (timer == 0) {
#if defined(BOARD_PWM_TIM0_CHANNELS)
		return (1U << BOARD_PWM_TIM0_CHANNELS) - 1U;
#endif
	}

	return 0;
}

uint32_t io_timer_channel_get_gpio_output(unsigned channel)
{
	return timer_io_channels[channel].gpio_out | GPIO_OUTPUT | GPIO_PULLUP;
}
