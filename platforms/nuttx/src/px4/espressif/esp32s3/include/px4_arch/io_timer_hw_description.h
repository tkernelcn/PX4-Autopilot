/****************************************************************************
 *
 *   ESP32-S3 IO timer description (board pins in board_config.h, not NuttX Kconfig).
 *
 ****************************************************************************/

#pragma once

#include <px4_arch/io_timer.h>
#include <px4_arch/hw_description.h>
#include <px4_platform_common/constexpr_util.h>
#include <px4_platform_common/px4_config.h>
#include <px4_platform/io_timer_init.h>
#include <arch/board/board.h>

static inline constexpr timer_io_channels_t initIOTimerChannel(const io_timers_t io_timers_conf[MAX_IO_TIMERS],
		Timer::TimerChannel timer, GPIO::GPIOPin pin)
{
	timer_io_channels_t ret{};
	ret.gpio_out = pin.pin;
	ret.timer_channel = timer.channel;
	ret.timer_index = timer.timer;
	(void)io_timers_conf;
	return ret;
}

static inline constexpr io_timers_t initIOTimer(Timer::Timer timer)
{
	io_timers_t ret{};

	switch (timer) {
	case Timer::Timer0:
		ret.base = 0;
		break;

	default:
		break;
	}

	(void)timer;
	return ret;
}
