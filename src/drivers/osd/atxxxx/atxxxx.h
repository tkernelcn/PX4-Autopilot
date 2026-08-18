/****************************************************************************
 *
 *   Copyright (c) 2018 PX4 Development Team. All rights reserved.
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
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#pragma once

/**
 * @file atxxxx.h
 * @author Daniele Pettenuzzo
 *
 * Driver for the ATXXXX chip on the omnibus fcu connected via SPI.
 */
#include <drivers/device/spi.h>
#include <drivers/drv_hrt.h>
#include <parameters/param.h>
#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/getopt.h>
#include <px4_platform_common/module.h>
#include <px4_platform_common/module_params.h>
#include <px4_platform_common/i2c_spi_buses.h>
#include <uORB/Subscription.hpp>
#include <uORB/topics/battery_status.h>
#include <uORB/topics/estimator_status_flags.h>
#include <uORB/topics/home_position.h>
#include <uORB/topics/input_rc.h>
#include <uORB/topics/sensor_gps.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/vehicle_global_position.h>
#include <uORB/topics/vehicle_local_position.h>
#include <uORB/topics/vehicle_thrust_setpoint.h>
#include <uORB/topics/vehicle_status.h>

#define OSD_SPI_BUS_SPEED (2000000L) /*  2 MHz  */

#define DIR_READ(a) ((a) | (1 << 7))
#define DIR_WRITE(a) ((a) & 0x7f)

#define OSD_CHARS_PER_ROW	30
#define OSD_NUM_ROWS_PAL	16
#define OSD_NUM_ROWS_NTSC	13
#define OSD_ZERO_BYTE 0x00
#define OSD_PAL_TX_MODE 0x40

extern "C" __EXPORT int atxxxx_main(int argc, char *argv[]);

class OSDatxxxx : public device::SPI, public ModuleParams, public I2CSPIDriver<OSDatxxxx>
{
public:
	OSDatxxxx(const I2CSPIDriverConfig &config);
	virtual ~OSDatxxxx() = default;

	static void print_usage();

	int init() override;

	void RunImpl();

protected:
	int probe() override;

private:
	int start();

	int reset();

	int init_osd();

	int readRegister(unsigned reg, uint8_t *data, unsigned count);
	int writeRegister(unsigned reg, uint8_t data);

	int add_character_to_screen(char c, uint8_t pos_x, uint8_t pos_y);
	void add_string_to_screen_centered(const char *str, uint8_t pos_y, int max_length);
	void clear_line(uint8_t pos_x, uint8_t pos_y, int length);

	int add_battery_info(uint8_t pos_x, uint8_t pos_y);
	int add_cell_voltage(uint8_t pos_x, uint8_t pos_y);
	int add_altitude(uint8_t pos_x, uint8_t pos_y);
	int add_flighttime(float flight_time, uint8_t pos_x, uint8_t pos_y);
	int add_groundspeed(uint8_t pos_x, uint8_t pos_y);
	int add_vertical_speed(uint8_t pos_x, uint8_t pos_y);
	int add_yaw(uint8_t pos_x, uint8_t pos_y);
	int add_home_info(uint8_t pos_x, uint8_t pos_y);
	int add_gps_info(uint8_t pos_x, uint8_t pos_y);
	int add_signal_strength(uint8_t pos_x, uint8_t pos_y);
	int add_link_quality(uint8_t pos_x, uint8_t pos_y);
	int add_ekf_fusion_status(uint8_t pos_x, uint8_t pos_y);
	int add_throttle(uint8_t pos_x, uint8_t pos_y);

	static const char *get_flight_mode(uint8_t nav_state);

	int enable_screen();
	int disable_screen();

	int update_topics();
	int update_screen();
	int add_string_to_screen(const char *str, uint8_t pos_x, uint8_t pos_y);
	uint8_t get_flighttime_row() const;
	uint8_t get_gps_row() const;

	uORB::Subscription _battery_sub{ORB_ID(battery_status)};
	uORB::Subscription _estimator_status_flags_sub{ORB_ID(estimator_status_flags)};
	uORB::Subscription _home_position_sub{ORB_ID(home_position)};
	uORB::Subscription _input_rc_sub{ORB_ID(input_rc)};
	uORB::Subscription _sensor_gps_sub{ORB_ID(vehicle_gps_position)};
	uORB::Subscription _vehicle_thrust_setpoint_sub{ORB_ID(vehicle_thrust_setpoint)};
	uORB::Subscription _vehicle_attitude_sub{ORB_ID(vehicle_attitude)};
	uORB::Subscription _vehicle_global_position_sub{ORB_ID(vehicle_global_position)};
	uORB::Subscription _local_position_sub{ORB_ID(vehicle_local_position)};
	uORB::Subscription _vehicle_status_sub{ORB_ID(vehicle_status)};

	// battery
	float _battery_voltage_v{0.f};
	float _battery_discharge_mah{0.f};
	float _battery_remaining{-1.f};
	uint8_t _battery_cell_count{0};
	bool _battery_valid{false};

	// altitude (display: relative to home when valid)
	float _local_position_z{0.f};
	bool _local_position_valid{false};
	float _local_z_ned{0.f};
	bool _local_z_valid{false};
	float _vertical_speed_m_s{0.f};
	bool _vertical_speed_valid{false};
	float _ground_speed_kmh{0.f};
	bool _ground_speed_valid{false};

	// yaw
	uint16_t _yaw_deg{0};
	bool _yaw_valid{false};

	// home
	uint16_t _home_bearing_deg{0};
	float _home_distance_m{0.f};
	bool _home_valid{false};
	float _home_z_ned{0.f};
	bool _home_lpos_valid{false};

	// gps
	double _gps_lat_deg{0.0};
	double _gps_lon_deg{0.0};
	uint8_t _gps_fix_type{0};
	uint8_t _gps_satellites_used{0};
	bool _gps_data_valid{false};
	bool _gps_fix_valid{false};

	// signal strength
	float _rssi_dbm{NAN};
	bool _rssi_valid{false};
	int8_t _link_quality{-1};
	bool _link_quality_valid{false};

	// EKF fusion status: GHBFR (upper=true, lower=false)
	bool _cs_gnss_pos{false};
	bool _cs_gps_hgt{false};
	bool _cs_baro_hgt{false};
	bool _cs_opt_flow{false};
	bool _cs_rng_hgt{false};
	bool _ekf_fusion_status_valid{false};

	// throttle
	float _throttle_sp{0.f};
	bool _throttle_valid{false};
	uint64_t _throttle_timestamp{0};
	uint8_t _vehicle_type{vehicle_status_s::VEHICLE_TYPE_UNSPECIFIED};

	// flight time
	uint8_t _arming_state{0};
	uint64_t _arming_timestamp{0};

	// flight mode
	uint8_t _nav_state{0};

	DEFINE_PARAMETERS(
		(ParamInt<px4::params::OSD_ATXXXX_CFG>) _param_osd_atxxxx_cfg
	)
};
