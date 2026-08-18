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

#include "W689.hpp"

#include <cstring>
#include <lib/parameters/param.h>
#include <lib/sensor_calibration/Utilities.hpp>

using namespace time_literals;
using matrix::Vector3f;

static constexpr float W689_ACCEL_LSB_PER_G = 2220.f;
static constexpr float W689_TEMPERATURE_SCALE = 1.f / 512.f;
static constexpr float W689_TEMPERATURE_OFFSET = 23.f;

static constexpr int16_t combine_be(uint8_t msb, uint8_t lsb)
{
	return static_cast<int16_t>((uint16_t(msb) << 8u) | uint16_t(lsb));
}

W689::W689(const I2CSPIDriverConfig &config) :
	SPI(config),
	I2CSPIDriver(config),
	_px4_accel(get_device_id(), config.rotation),
	_px4_gyro(get_device_id(), config.rotation)
{
}

W689::~W689()
{
	perf_free(_bad_register_perf);
	perf_free(_bad_transfer_perf);
	perf_free(_reset_perf);
}

int W689::init()
{
	int ret = SPI::init();

	if (ret != PX4_OK) {
		DEVICE_DEBUG("SPI::init failed (%i)", ret);
		return ret;
	}

	return Reset() ? PX4_OK : PX4_ERROR;
}

bool W689::Reset()
{
	_state = STATE::RESET;
	ScheduleClear();
	ScheduleNow();
	return true;
}

void W689::exit_and_cleanup()
{
	I2CSPIDriverBase::exit_and_cleanup();
}

void W689::print_status()
{
	I2CSPIDriverBase::print_status();

	perf_print_counter(_bad_register_perf);
	perf_print_counter(_bad_transfer_perf);
	perf_print_counter(_reset_perf);
}

int W689::probe()
{
	if (!PrepareSPIMode()) {
		return PX4_ERROR;
	}

	const uint8_t who_am_i = RegisterRead(Register::WHO_AM_I, SPI_SPEED_INIT);

	if (who_am_i != WHO_AM_I_VALUE) {
		DEVICE_DEBUG("unexpected WHO_AM_I 0x%02x", who_am_i);
		return PX4_ERROR;
	}

	return PX4_OK;
}

void W689::RunImpl()
{
	const hrt_abstime now = hrt_absolute_time();

	switch (_state) {
	case STATE::RESET:
		perf_count(_reset_perf);
		_failure_count = 0;
		_checked_register = 0;
		_reset_timestamp = now;
		_temperature_update_timestamp = 0;

		if (HardwareReset()) {
			_state = STATE::WAIT_FOR_RESET;
			ScheduleDelayed(20_ms);

		} else {
			ScheduleDelayed(100_ms);
		}

		break;

	case STATE::WAIT_FOR_RESET: {
			if (hrt_elapsed_time(&_reset_timestamp) < 200_ms) {
				ScheduleDelayed(20_ms);
				break;
			}

			PrepareSPIMode();
			const uint8_t who_am_i = RegisterRead(Register::WHO_AM_I, SPI_SPEED_INIT);

			if (who_am_i == WHO_AM_I_VALUE) {
				_state = STATE::CONFIGURE;
				ScheduleNow();

			} else if (hrt_elapsed_time(&_reset_timestamp) > 500_ms) {
				DEVICE_DEBUG("reset timed out, retrying");
				_state = STATE::RESET;
				ScheduleDelayed(100_ms);

			} else {
				ScheduleDelayed(20_ms);
			}
		}
		break;

	case STATE::CONFIGURE:
		if (Configure()) {
			_state = STATE::READ;
			_last_config_check_timestamp = now;
			ScheduleOnInterval(SAMPLE_INTERVAL_US, SAMPLE_INTERVAL_US);

		} else if (hrt_elapsed_time(&_reset_timestamp) > 1500_ms) {
			DEVICE_DEBUG("configure failed, resetting");
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
				SelectBank(BANK_MAIN);

				if (RegisterCheck(_register_cfg[_checked_register])) {
					_checked_register = (_checked_register + 1) % size_register_cfg;
					_last_config_check_timestamp = now;

				} else {
					perf_count(_bad_register_perf);
					Reset();
					return;
				}
			}
		}
		break;
	}
}

bool W689::HardwareReset()
{
	if (!PrepareSPIMode()) {
		return false;
	}

	if (RegisterRead(Register::WHO_AM_I, SPI_SPEED_INIT) != WHO_AM_I_VALUE) {
		return false;
	}

	RegisterWrite(Register::PWR_CTRL, 0x00, SPI_SPEED_INIT);
	px4_udelay(2000);

	SelectBank(BANK_MAIN, SPI_SPEED_INIT);
	RegisterWrite(Register::COM_CFG, COM_CFG_BIT::BOOT, SPI_SPEED_INIT);
	RegisterWrite(Register::RESET, SOFT_RESET_CMD, SPI_SPEED_INIT);
	RegisterWrite(Register::RESET, SOFT_RESET_CMD, SPI_SPEED_INIT);

	return true;
}

bool W689::Configure()
{
	SelectBank(BANK_MAIN);

	RegisterWrite(Register::PWR_CTRL, PWR_CTRL_BIT::ACC_GYR_ENABLE);
	px4_udelay(5000);
	RegisterWrite(Register::PWR_CTRL, PWR_CTRL_BIT::ACC_GYR_ENABLE);
	px4_usleep(250_ms);

	RegisterWrite(Register::INT1_OUT_SEL1, 0x00);
	RegisterWrite(Register::INT1_OUT_SEL2, 0x00);
	RegisterWrite(Register::INT2_OUT_SEL1, 0x00);
	RegisterWrite(Register::INT2_OUT_SEL2, 0x00);
	RegisterWrite(Register::AOI1_CTRL, 0x00);
	RegisterWrite(Register::AOI1_VTH, 0xFF);
	RegisterWrite(Register::AOI1_TTH, 0xFF);
	RegisterWrite(Register::FIFO_CONFIG, FIFO_BYPASS);
	RegisterWrite(Register::FIFO_MODE, FIFO_BYPASS);

	RegisterWrite(Register::ACC_CONF, ACC_CONF_DEFAULT);
	px4_udelay(2000);
	RegisterWrite(Register::ACC_RANGE, ACC_RANGE_16G);

	RegisterWrite(Register::GYR_CONF, GYR_CONF_DEFAULT);
	RegisterWrite(Register::GYR_CONF, GYR_CONF_DEFAULT);
	px4_udelay(2000);
	RegisterWrite(Register::GYR_RANGE, GYR_RANGE_2000DPS);
	RegisterWrite(Register::GYR_RANGE, GYR_RANGE_2000DPS);

	RegisterWrite(Register::COM_CFG, COM_CFG_DEFAULT);
	px4_udelay(2000);

	SelectBank(BANK_DIG_CTRL);
	uint8_t dig_ctrl = RegisterRead(Register::AOI1_CTRL);
	dig_ctrl &= ~0x03u;
	RegisterWrite(Register::AOI1_CTRL, dig_ctrl);
	SelectBank(BANK_MAIN);

	for (const register_config_t &reg_cfg : _register_cfg) {
		if (!RegisterCheck(reg_cfg)) {
			return false;
		}
	}

	_px4_accel.set_range(16.f * CONSTANTS_ONE_G);
	_px4_accel.set_scale(CONSTANTS_ONE_G / W689_ACCEL_LSB_PER_G);

	_px4_gyro.set_range(math::radians(2000.f));
	_px4_gyro.set_scale(math::radians(2000.f / 32768.f)); // 32768 LSB @ 2000 dps

	return EnsureDefaultCalibration();
}

bool W689::EnsureDefaultCalibration()
{
	bool changed = false;
	bool success = true;

	const uint32_t accel_device_id = _px4_accel.get_device_id();

	if (calibration::FindCurrentCalibrationIndex("ACC", accel_device_id) < 0) {
		const int8_t index = calibration::FindAvailableCalibrationIndex("ACC", accel_device_id);

		if (index < 0) {
			return false;
		}

		success &= calibration::SetCalibrationParam("ACC", "ID", index, static_cast<int32_t>(accel_device_id));
		success &= calibration::SetCalibrationParam("ACC", "PRIO", index, static_cast<int32_t>(50));
		success &= calibration::SetCalibrationParamsVector3f("ACC", "OFF", index, Vector3f{0.f, 0.f, 0.f});
		success &= calibration::SetCalibrationParamsVector3f("ACC", "SCALE", index, Vector3f{1.f, 1.f, 1.f});
		success &= calibration::SetCalibrationParam("ACC", "ROT", index, static_cast<int32_t>(-1));
		changed = true;
	}

	const uint32_t gyro_device_id = _px4_gyro.get_device_id();

	if (calibration::FindCurrentCalibrationIndex("GYRO", gyro_device_id) < 0) {
		const int8_t index = calibration::FindAvailableCalibrationIndex("GYRO", gyro_device_id);

		if (index < 0) {
			return false;
		}

		success &= calibration::SetCalibrationParam("GYRO", "ID", index, static_cast<int32_t>(gyro_device_id));
		success &= calibration::SetCalibrationParam("GYRO", "PRIO", index, static_cast<int32_t>(50));
		success &= calibration::SetCalibrationParamsVector3f("GYRO", "OFF", index, Vector3f{0.f, 0.f, 0.f});
		success &= calibration::SetCalibrationParam("GYRO", "ROT", index, static_cast<int32_t>(-1));
		changed = true;
	}

	if (changed && success) {
		param_notify_changes();
	}

	return success;
}

bool W689::PrepareSPIMode()
{
	SelectBank(BANK_MAIN, SPI_SPEED_INIT);
	RegisterWrite(Register::RESET, SPI_PREPARE_CMD, SPI_SPEED_INIT);
	px4_udelay(2000);

	SelectBank(BANK_I2C_SPI, SPI_SPEED_INIT);
	RegisterWrite(Register::SPI_I2C_CFG, I2C_DISABLE, SPI_SPEED_INIT);
	RegisterWrite(Register::SPI_I2C_CFG, I2C_DISABLE, SPI_SPEED_INIT);
	px4_udelay(2000);

	SelectBank(BANK_MAIN, SPI_SPEED_INIT);
	RegisterWrite(Register::RESET, SPI_PREPARE_CLEAR, SPI_SPEED_INIT);
	px4_udelay(2000);

	return true;
}

int W689::ReadData(const hrt_abstime &timestamp_sample)
{
	const uint8_t status = RegisterRead(Register::STATUS);

	if ((status & (STATUS_BIT::ACC_CONF_ERR | STATUS_BIT::GYR_CONF_ERR)) != 0) {
		return -1;
	}

	UpdateTemperature(status, timestamp_sample);

	if ((status & (STATUS_BIT::ACC_DRDY | STATUS_BIT::GYR_DRDY)) != (STATUS_BIT::ACC_DRDY | STATUS_BIT::GYR_DRDY)) {
		return 0;
	}

	struct TransferBuffer {
		uint8_t cmd;
		uint8_t data[IMU_BURST_LENGTH];
	} buffer{};

	static_assert(sizeof(TransferBuffer) == (1 + IMU_BURST_LENGTH), "W689 burst size mismatch");

	buffer.cmd = static_cast<uint8_t>(Register::ACC_XH) | DIR_READ;
	set_frequency(SPI_SPEED);

	if (transfer((uint8_t *)&buffer, (uint8_t *)&buffer, sizeof(buffer)) != PX4_OK) {
		perf_count(_bad_transfer_perf);
		return -1;
	}

	const int16_t accel_x = combine_be(buffer.data[0], buffer.data[1]);
	const int16_t accel_y = combine_be(buffer.data[2], buffer.data[3]);
	const int16_t accel_z = combine_be(buffer.data[4], buffer.data[5]);
	const int16_t gyro_x = combine_be(buffer.data[6], buffer.data[7]);
	const int16_t gyro_y = combine_be(buffer.data[8], buffer.data[9]);
	const int16_t gyro_z = combine_be(buffer.data[10], buffer.data[11]);

	_px4_accel.update(timestamp_sample, accel_x, accel_y, accel_z);
	_px4_gyro.update(timestamp_sample, gyro_x, gyro_y, gyro_z);

	return 1;
}

void W689::UpdateTemperature(uint8_t status, const hrt_abstime &timestamp_sample)
{
	if ((status & STATUS_BIT::TMP_DRDY) == 0) {
		return;
	}

	if (_temperature_update_timestamp != 0 &&
	    (timestamp_sample - _temperature_update_timestamp) < TEMPERATURE_INTERVAL_US) {
		return;
	}

	_temperature_update_timestamp = timestamp_sample;

	uint8_t temperature_data[2] {};

	if (!RegisterReadBuffer(Register::TEMP_H, temperature_data, sizeof(temperature_data))) {
		return;
	}

	const int16_t raw_temperature = combine_be(temperature_data[0], temperature_data[1]);
	const float temperature = static_cast<float>(raw_temperature) * W689_TEMPERATURE_SCALE + W689_TEMPERATURE_OFFSET;

	if (PX4_ISFINITE(temperature) && temperature > -60.f && temperature < 125.f) {
		_px4_accel.set_temperature(temperature);
		_px4_gyro.set_temperature(temperature);
	}
}

bool W689::RegisterCheck(const register_config_t &reg_cfg)
{
	return RegisterRead(reg_cfg.reg) == reg_cfg.value;
}

bool W689::RegisterReadBuffer(Register reg, uint8_t *data, uint8_t length, uint32_t frequency)
{
	if (length == 0 || length > IMU_BURST_LENGTH) {
		return false;
	}

	uint8_t buffer[1 + IMU_BURST_LENGTH] {};
	buffer[0] = static_cast<uint8_t>(reg) | DIR_READ;

	set_frequency(frequency);

	if (transfer(buffer, buffer, length + 1) != PX4_OK) {
		perf_count(_bad_transfer_perf);
		return false;
	}

	memcpy(data, &buffer[1], length);
	return true;
}

uint8_t W689::RegisterRead(Register reg, uint32_t frequency)
{
	uint8_t value = 0;
	RegisterReadBuffer(reg, &value, 1, frequency);
	return value;
}

void W689::RegisterWrite(Register reg, uint8_t value, uint32_t frequency)
{
	uint8_t buffer[2] { static_cast<uint8_t>(reg), value };

	set_frequency(frequency);

	if (transfer(buffer, buffer, sizeof(buffer)) != PX4_OK) {
		perf_count(_bad_transfer_perf);
	}

	px4_udelay(50);
}

bool W689::RegisterWriteVerified(Register reg, uint8_t value, uint32_t frequency)
{
	for (int retry = 0; retry < WRITE_VERIFY_RETRIES; retry++) {
		RegisterWrite(reg, value, frequency);

		if (RegisterRead(reg, frequency) == value) {
			return true;
		}
	}

	return false;
}

void W689::SelectBank(uint8_t bank, uint32_t frequency)
{
	RegisterWrite(Register::BANK_SEL, bank, frequency);
	px4_udelay(2000);
}
