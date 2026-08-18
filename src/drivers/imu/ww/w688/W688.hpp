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

#pragma once

#include "WW_W688_registers.hpp"

#include <drivers/drv_hrt.h>
#include <lib/drivers/accelerometer/PX4Accelerometer.hpp>
#include <lib/drivers/device/spi.h>
#include <lib/drivers/gyroscope/PX4Gyroscope.hpp>
#include <lib/geo/geo.h>
#include <lib/perf/perf_counter.h>
#include <mathlib/mathlib.h>
#include <px4_platform_common/i2c_spi_buses.h>

using namespace WW_W688;

class W688 : public device::SPI, public I2CSPIDriver<W688>
{
public:
	W688(const I2CSPIDriverConfig &config);
	~W688() override;

	static void print_usage();

	void RunImpl();

	int init() override;
	void print_status() override;

private:
	void exit_and_cleanup() override;

	struct register_config_t {
		Register reg;
		uint8_t value;
	};

	static constexpr uint32_t SAMPLE_INTERVAL_US{1116}; // 896 Hz, datasheet-real ODR
	static constexpr uint32_t RESET_DELAY_US{20 * 1000};
	static constexpr uint32_t CONFIG_RETRY_DELAY_US{10 * 1000};
	static constexpr uint32_t CONFIG_CHECK_INTERVAL_US{100 * 1000};
	static constexpr uint32_t TEMPERATURE_UPDATE_INTERVAL_US{1 * 1000 * 1000};
	static constexpr uint8_t IMU_BURST_LENGTH{12};
	static constexpr uint8_t WRITE_VERIFY_RETRIES{5};

	int probe() override;

	bool Reset();
	bool Configure();
	int ReadData(const hrt_abstime &timestamp_sample);
	void UpdateTemperature();

	bool RegisterCheck(const register_config_t &reg_cfg);
	bool RegisterReadBuffer(Register reg, uint8_t *data, uint8_t length);
	uint8_t RegisterRead(Register reg);
	void RegisterWrite(Register reg, uint8_t value);
	bool RegisterWriteVerified(Register reg, uint8_t value);

	PX4Accelerometer _px4_accel;
	PX4Gyroscope _px4_gyro;

	perf_counter_t _bad_register_perf{perf_alloc(PC_COUNT, MODULE_NAME": bad register")};
	perf_counter_t _bad_transfer_perf{perf_alloc(PC_COUNT, MODULE_NAME": bad transfer")};
	perf_counter_t _reset_perf{perf_alloc(PC_COUNT, MODULE_NAME": reset")};

	hrt_abstime _reset_timestamp{0};
	hrt_abstime _last_config_check_timestamp{0};
	hrt_abstime _last_temperature_update{0};
	int _failure_count{0};

	enum class STATE : uint8_t {
		RESET,
		WAIT_FOR_RESET,
		CONFIGURE,
		READ,
	} _state{STATE::RESET};

	uint8_t _checked_register{0};
	static constexpr uint8_t size_register_cfg{5};
	register_config_t _register_cfg[size_register_cfg] {
		{ Register::CTRL1, CTRL1_DEFAULT },
		{ Register::CTRL2, CTRL2_DEFAULT },
		{ Register::CTRL3, CTRL3_DEFAULT },
		{ Register::CTRL5, CTRL5_DEFAULT },
		{ Register::CTRL7, CTRL7_DEFAULT },
	};
};
