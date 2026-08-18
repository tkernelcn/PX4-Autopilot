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

namespace WW_W689
{

static constexpr uint32_t SPI_SPEED_INIT = 1 * 1000 * 1000;
static constexpr uint32_t SPI_SPEED = 10 * 1000 * 1000;
static constexpr uint8_t DIR_READ = 0x80;

enum class Register : uint8_t {
	WHO_AM_I      = 0x01,
	COM_CFG       = 0x04,
	INT1_OUT_SEL1 = 0x05,
	INT1_OUT_SEL2 = 0x06,
	INT2_OUT_SEL1 = 0x07,
	INT2_OUT_SEL2 = 0x08,
	STATUS        = 0x0B,
	ACC_XH        = 0x0C,
	FIFO_CONFIG   = 0x1C,
	FIFO_MODE     = 0x1D,
	TEMP_H        = 0x22,
	TEMP_L        = 0x23,
	AOI1_CTRL     = 0x30,
	AOI1_VTH      = 0x32,
	AOI1_TTH      = 0x33,
	ACC_CONF      = 0x40,
	ACC_RANGE     = 0x41,
	GYR_CONF      = 0x42,
	GYR_RANGE     = 0x43,
	RESET         = 0x4A,
	SPI_I2C_CFG   = 0x6F,
	PWR_CTRL      = 0x7D,
	BANK_SEL      = 0x7F,
};

namespace STATUS_BIT
{
static constexpr uint8_t ACC_DRDY = (1u << 0);
static constexpr uint8_t GYR_DRDY = (1u << 1);
static constexpr uint8_t TMP_DRDY = (1u << 2);
static constexpr uint8_t ACC_CONF_ERR = (1u << 4);
static constexpr uint8_t GYR_CONF_ERR = (1u << 5);
}

namespace COM_CFG_BIT
{
static constexpr uint8_t ADDR_AUTO = (1u << 4);
static constexpr uint8_t BDU = (1u << 6);
static constexpr uint8_t BOOT = (1u << 7);
}

namespace PWR_CTRL_BIT
{
static constexpr uint8_t ACC_GYR_ENABLE = (1u << 3) | (1u << 2) | (1u << 1);
}

static constexpr uint8_t WHO_AM_I_VALUE = 0x6A;
static constexpr uint8_t SOFT_RESET_CMD = 0xA5;
static constexpr uint8_t SPI_PREPARE_CMD = 0x66;
static constexpr uint8_t SPI_PREPARE_CLEAR = 0x00;
static constexpr uint8_t BANK_MAIN = 0x00;
static constexpr uint8_t BANK_I2C_SPI = 0x83;
static constexpr uint8_t BANK_DIG_CTRL = 0x8C;
static constexpr uint8_t I2C_DISABLE = 0xB4;

static constexpr uint8_t FIFO_BYPASS = 0x00;
static constexpr uint8_t ACC_CONF_DEFAULT = (1u << 7) | (2u << 4) | 0x0C; // high performance, avg4, 1600 Hz
static constexpr uint8_t ACC_RANGE_16G = 0x03;
static constexpr uint8_t GYR_CONF_DEFAULT = (1u << 7) | 0x0C; // high performance, avg1, 1600 Hz
static constexpr uint8_t GYR_RANGE_2000DPS = 0x00;
static constexpr uint8_t COM_CFG_DEFAULT = COM_CFG_BIT::BDU | COM_CFG_BIT::ADDR_AUTO;

} // namespace WW_W689
