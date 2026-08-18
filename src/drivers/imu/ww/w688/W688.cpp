/****************************************************************************
 *
 *   Copyright (c) 2026 PX4 Development Team. All rights reserved.
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
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 ****************************************************************************/

#include "W688.hpp"

#include <cstring>

using namespace time_literals;

static constexpr int16_t combine(uint8_t msb, uint8_t lsb)
{
	return (msb << 8u) | lsb;
}

W688::W688(const I2CSPIDriverConfig &config) :
	SPI(config),
	I2CSPIDriver(config),
	_px4_accel(get_device_id(), config.rotation),
	_px4_gyro(get_device_id(), config.rotation)
{
}

W688::~W688()
{
	perf_free(_bad_register_perf);
	perf_free(_bad_transfer_perf);
	perf_free(_reset_perf);
}

int W688::init()
{
	int ret = SPI::init();

	if (ret != PX4_OK) {
		DEVICE_DEBUG("SPI::init failed (%i)", ret);
		return ret;
	}

	return Reset() ? PX4_OK : PX4_ERROR;
}

bool W688::Reset()
{
	_state = STATE::RESET;
	ScheduleClear();
	ScheduleNow();
	return true;
}

void W688::exit_and_cleanup()
{
	I2CSPIDriverBase::exit_and_cleanup();
}

void W688::print_status()
{
	I2CSPIDriverBase::print_status();

	perf_print_counter(_bad_register_perf);
	perf_print_counter(_bad_transfer_perf);
	perf_print_counter(_reset_perf);
}

int W688::probe()
{
	set_frequency(SPI_SPEED);

	const uint8_t who_am_i = RegisterRead(Register::WHO_AM_I);

	if (who_am_i != WHO_AM_I_VALUE) {
		DEVICE_DEBUG("unexpected WHO_AM_I 0x%02x", who_am_i);
		return PX4_ERROR;
	}

	return PX4_OK;
}

void W688::RunImpl()
{
	const hrt_abstime now = hrt_absolute_time();

	switch (_state) {
	case STATE::RESET:
		perf_count(_reset_perf);
		RegisterWrite(Register::RESET, SOFT_RESET_CMD);
		_reset_timestamp = now;
		_failure_count = 0;
		_state = STATE::WAIT_FOR_RESET;
		ScheduleDelayed(20_ms);
		break;

	case STATE::WAIT_FOR_RESET: {
			const uint8_t who_am_i = RegisterRead(Register::WHO_AM_I);
			const uint8_t reset_done = RegisterRead(Register::RESET_DONE);

			if ((who_am_i == WHO_AM_I_VALUE) && (reset_done == RESET_DONE_VALUE)) {
				_state = STATE::CONFIGURE;
				ScheduleNow();

			} else if (hrt_elapsed_time(&_reset_timestamp) > 500_ms) {
				PX4_DEBUG("reset timed out, retrying");
				_state = STATE::RESET;
				ScheduleDelayed(100_ms);

			} else {
				ScheduleDelayed(10_ms);
			}
		}
		break;

	case STATE::CONFIGURE:
		if (Configure()) {
			_state = STATE::READ;
			_last_config_check_timestamp = now;
			_last_temperature_update = now;
			ScheduleOnInterval(SAMPLE_INTERVAL_US, SAMPLE_INTERVAL_US);

		} else if (hrt_elapsed_time(&_reset_timestamp) > 1000_ms) {
			PX4_DEBUG("configure failed, resetting");
			_state = STATE::RESET;
			ScheduleDelayed(100_ms);

		} else {
			ScheduleDelayed(CONFIG_RETRY_DELAY_US);
		}

		break;

	case STATE::READ: {
			const int read_result = ReadData(now);

			if (read_result > 0) {
				if (_failure_count > 0) {
					_failure_count--;
				}

			} else if (read_result < 0) {
				_failure_count++;

				if (_failure_count > 10) {
					Reset();
					return;
				}
			}

			if (hrt_elapsed_time(&_last_config_check_timestamp) >= CONFIG_CHECK_INTERVAL_US) {
				if (RegisterCheck(_register_cfg[_checked_register])) {
					_checked_register = (_checked_register + 1) % size_register_cfg;
					_last_config_check_timestamp = now;

				} else {
					perf_count(_bad_register_perf);
					Reset();
					return;
				}
			}

			if (hrt_elapsed_time(&_last_temperature_update) >= TEMPERATURE_UPDATE_INTERVAL_US) {
				UpdateTemperature();
				_last_temperature_update = now;
			}
		}
		break;
	}
}

bool W688::Configure()
{
	if (!RegisterWriteVerified(Register::CTRL7, CTRL7_DISABLE_ALL)) {
		return false;
	}

	px4_udelay(2000);

	if (!RegisterWriteVerified(Register::CTRL1, CTRL1_DEFAULT)) {
		return false;
	}

	if (!RegisterWriteVerified(Register::CTRL2, CTRL2_DEFAULT)) {
		return false;
	}

	if (!RegisterWriteVerified(Register::CTRL3, CTRL3_DEFAULT)) {
		return false;
	}

	if (!RegisterWriteVerified(Register::CTRL5, CTRL5_DEFAULT)) {
		return false;
	}

	if (!RegisterWriteVerified(Register::CTRL7, CTRL7_DEFAULT)) {
		return false;
	}

	_px4_accel.set_range(16.f * CONSTANTS_ONE_G);
	_px4_accel.set_scale(CONSTANTS_ONE_G / 2048.f); // 2048 LSB/g @ 16g

	_px4_gyro.set_range(math::radians(2048.f));
	_px4_gyro.set_scale(math::radians(1.f / 16.f)); // 16 LSB/dps @ 2048 dps

	UpdateTemperature();

	return true;
}

int W688::ReadData(const hrt_abstime &timestamp_sample)
{
	const uint8_t statusint = RegisterRead(Register::STATUSINT);

	if ((statusint & STATUSINT_AVAIL) == 0) {
		return 0;
	}

	if ((statusint & STATUSINT_LOCKED) == 0) {
		px4_udelay(2);
	}

	struct TransferBuffer {
		uint8_t cmd;
		uint8_t data[IMU_BURST_LENGTH];
	} buffer{};

	static_assert(sizeof(TransferBuffer) == (1 + IMU_BURST_LENGTH), "W688 burst size mismatch");

	buffer.cmd = static_cast<uint8_t>(Register::AX_L) | DIR_READ;
	set_frequency(SPI_SPEED);

	if (transfer((uint8_t *)&buffer, (uint8_t *)&buffer, sizeof(buffer)) != PX4_OK) {
		perf_count(_bad_transfer_perf);
		return -1;
	}

	const int16_t accel_x = combine(buffer.data[1], buffer.data[0]);
	const int16_t accel_y = combine(buffer.data[3], buffer.data[2]);
	const int16_t accel_z = combine(buffer.data[5], buffer.data[4]);
	const int16_t gyro_x = combine(buffer.data[7], buffer.data[6]);
	const int16_t gyro_y = combine(buffer.data[9], buffer.data[8]);
	const int16_t gyro_z = combine(buffer.data[11], buffer.data[10]);

	_px4_accel.update(timestamp_sample, accel_x, accel_y, accel_z);
	_px4_gyro.update(timestamp_sample, gyro_x, gyro_y, gyro_z);

	return 1;
}

void W688::UpdateTemperature()
{
	uint8_t buffer[2] {};

	if (RegisterReadBuffer(Register::TEMP_L, buffer, sizeof(buffer))) {
		const int16_t raw_temperature = combine(buffer[1], buffer[0]);
		const float temperature = raw_temperature / 256.f;
		_px4_accel.set_temperature(temperature);
		_px4_gyro.set_temperature(temperature);
	}
}

bool W688::RegisterCheck(const register_config_t &reg_cfg)
{
	return RegisterRead(reg_cfg.reg) == reg_cfg.value;
}

bool W688::RegisterReadBuffer(Register reg, uint8_t *data, uint8_t length)
{
	if (length == 0 || length > IMU_BURST_LENGTH) {
		return false;
	}

	uint8_t buffer[1 + IMU_BURST_LENGTH] {};
	buffer[0] = static_cast<uint8_t>(reg) | DIR_READ;

	set_frequency(SPI_SPEED);

	if (transfer(buffer, buffer, length + 1) != PX4_OK) {
		perf_count(_bad_transfer_perf);
		return false;
	}

	memcpy(data, &buffer[1], length);
	return true;
}

uint8_t W688::RegisterRead(Register reg)
{
	uint8_t value = 0;
	RegisterReadBuffer(reg, &value, 1);
	return value;
}

void W688::RegisterWrite(Register reg, uint8_t value)
{
	uint8_t buffer[2] { static_cast<uint8_t>(reg), value };
	set_frequency(SPI_SPEED);

	if (transfer(buffer, buffer, sizeof(buffer)) != PX4_OK) {
		perf_count(_bad_transfer_perf);
	}
}

bool W688::RegisterWriteVerified(Register reg, uint8_t value)
{
	for (int retry = 0; retry < WRITE_VERIFY_RETRIES; retry++) {
		RegisterWrite(reg, value);

		if (RegisterRead(reg) == value) {
			return true;
		}
	}

	return false;
}
