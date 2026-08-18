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

#include <stdint.h>

namespace WW_W688
{

static constexpr uint32_t SPI_SPEED = 10 * 1000 * 1000;
static constexpr uint8_t DIR_READ = 0x80;

// Use datasheet-real ODR names. In 6DOF mode the sensor clocking is not the
// informal "8k/1k" shorthand commonly used by FC firmware.
enum class Register : uint8_t {
	WHO_AM_I   = 0x00,
	CTRL1      = 0x02,
	CTRL2      = 0x03,
	CTRL3      = 0x04,
	CTRL5      = 0x06,
	CTRL7      = 0x08,
	STATUSINT  = 0x2D,
	TEMP_L     = 0x33,
	AX_L       = 0x35,
	GX_L       = 0x3B,
	RESET_DONE = 0x4D,
	RESET      = 0x60,
};

namespace CTRL1_BIT
{
static constexpr uint8_t ADDR_AI_EN = (1u << 6);
static constexpr uint8_t BE = (1u << 5);
}

namespace CTRL5_BIT
{
static constexpr uint8_t ACC_LPF_ENABLE = (1u << 0);
static constexpr uint8_t ACC_LPF_MODE_3 = (0x03u << 1);
static constexpr uint8_t GYR_LPF_ENABLE = (1u << 4);
static constexpr uint8_t GYR_LPF_MODE_3 = (0x03u << 5);
}

namespace CTRL7_BIT
{
static constexpr uint8_t SYNCSAMPLE = (1u << 7);
}

enum class AccRange : uint8_t {
	RANGE_2G  = (0x00u << 4),
	RANGE_4G  = (0x01u << 4),
	RANGE_8G  = (0x02u << 4),
	RANGE_16G = (0x03u << 4),
};

enum class AccOdr : uint8_t {
	ODR_7174HZ           = 0x00,
	ODR_3587HZ           = 0x01,
	ODR_1793HZ           = 0x02,
	ODR_896HZ            = 0x03,
	ODR_448HZ            = 0x04,
	ODR_224HZ            = 0x05,
	ODR_112HZ            = 0x06,
	ODR_56HZ             = 0x07,
	ODR_28HZ             = 0x08,
	ODR_LOWPOWER_128HZ   = 0x0C,
	ODR_LOWPOWER_21HZ    = 0x0D,
	ODR_LOWPOWER_11HZ    = 0x0E,
	ODR_LOWPOWER_3HZ     = 0x0F,
};

enum class GyroRange : uint8_t {
	RANGE_16_DPS   = (0x00u << 4),
	RANGE_32_DPS   = (0x01u << 4),
	RANGE_64_DPS   = (0x02u << 4),
	RANGE_128_DPS  = (0x03u << 4),
	RANGE_256_DPS  = (0x04u << 4),
	RANGE_512_DPS  = (0x05u << 4),
	RANGE_1024_DPS = (0x06u << 4),
	RANGE_2048_DPS = (0x07u << 4),
};

enum class GyroOdr : uint8_t {
	ODR_7174HZ = 0x00,
	ODR_3587HZ = 0x01,
	ODR_1793HZ = 0x02,
	ODR_896HZ  = 0x03,
	ODR_448HZ  = 0x04,
	ODR_224HZ  = 0x05,
	ODR_112HZ  = 0x06,
	ODR_56HZ   = 0x07,
	ODR_28HZ   = 0x08,
};

static constexpr uint8_t STATUSINT_AVAIL = (1u << 0);
static constexpr uint8_t STATUSINT_LOCKED = (1u << 1);

static constexpr uint8_t WHO_AM_I_VALUE = 0x05;
static constexpr uint8_t SOFT_RESET_CMD = 0xB0;
static constexpr uint8_t RESET_DONE_VALUE = 0x80;

static constexpr uint8_t CTRL1_DEFAULT = CTRL1_BIT::ADDR_AI_EN | CTRL1_BIT::BE;
static constexpr uint8_t CTRL2_DEFAULT = static_cast<uint8_t>(AccRange::RANGE_16G) | static_cast<uint8_t>(AccOdr::ODR_896HZ);
static constexpr uint8_t CTRL3_DEFAULT = static_cast<uint8_t>(GyroRange::RANGE_2048_DPS) | static_cast<uint8_t>(GyroOdr::ODR_896HZ);
static constexpr uint8_t CTRL5_DEFAULT = CTRL5_BIT::ACC_LPF_ENABLE | CTRL5_BIT::ACC_LPF_MODE_3
					 | CTRL5_BIT::GYR_LPF_ENABLE | CTRL5_BIT::GYR_LPF_MODE_3;
static constexpr uint8_t CTRL7_DISABLE_ALL = 0x00;
static constexpr uint8_t CTRL7_ACC_GYR_ENABLE = 0x03;
static constexpr uint8_t CTRL7_DEFAULT = CTRL7_BIT::SYNCSAMPLE | CTRL7_ACC_GYR_ENABLE;

} // namespace WW_W688
