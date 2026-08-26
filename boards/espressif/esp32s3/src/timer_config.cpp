#include <px4_arch/io_timer_hw_description.h>

constexpr io_timers_t io_timers[MAX_IO_TIMERS] = {
	initIOTimer(Timer::Timer0),
};

constexpr timer_io_channels_t timer_io_channels[MAX_TIMER_IO_CHANNELS] = {
	initIOTimerChannel(io_timers, {Timer::Timer0, 0}, {BOARD_PWM_CHANNEL0_PIN}),
	initIOTimerChannel(io_timers, {Timer::Timer0, 1}, {BOARD_PWM_CHANNEL1_PIN}),
	initIOTimerChannel(io_timers, {Timer::Timer0, 2}, {BOARD_PWM_CHANNEL2_PIN}),
	initIOTimerChannel(io_timers, {Timer::Timer0, 3}, {BOARD_PWM_CHANNEL3_PIN}),
};
