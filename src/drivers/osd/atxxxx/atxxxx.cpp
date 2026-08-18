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

/**
 * @file atxxxx.cpp
 * @author Daniele Pettenuzzo
 * @author Beat Küng <beat-kueng@gmx.net>
 *
 * Driver for the ATXXXX chip (e.g. MAX7456) on the omnibus f4 fcu connected via SPI.
 */

#include "atxxxx.h"
#include "symbols.h"

#include <lib/geo/geo.h>
#include <lib/mathlib/mathlib.h>
#include <matrix/matrix/math.hpp>

using namespace time_literals;

static constexpr uint32_t OSD_UPDATE_RATE{50_ms};	// 20 Hz

OSDatxxxx::OSDatxxxx(const I2CSPIDriverConfig &config) :
	SPI(config),
	ModuleParams(nullptr),
	I2CSPIDriver(config)
{
}

int
OSDatxxxx::init()
{
	/* do SPI init (and probe) first */
	int ret = SPI::init();

	if (ret != PX4_OK) {
		return ret;
	}

	ret = reset();

	if (ret != PX4_OK) {
		return ret;
	}

	ret = init_osd();

	if (ret != PX4_OK) {
		return ret;
	}

	// clear the screen
	int num_rows = (_param_osd_atxxxx_cfg.get() == 1 ? OSD_NUM_ROWS_NTSC : OSD_NUM_ROWS_PAL);

	for (int i = 0; i < OSD_CHARS_PER_ROW; i++) {
		for (int j = 0; j < num_rows; j++) {
			add_character_to_screen(' ', i, j);
		}
	}

	if (ret == PX4_OK) {
		start();
	}

	return ret;
}

int
OSDatxxxx::start()
{
	ScheduleOnInterval(OSD_UPDATE_RATE, 10000);

	return PX4_OK;
}

int
OSDatxxxx::probe()
{
	uint8_t data = 0;
	int ret = PX4_OK;

	ret |= writeRegister(0x00, 0x01); //disable video output
	ret |= readRegister(0x00, &data, 1);

	if (data != 1 || ret != PX4_OK) {
		PX4_ERR("probe failed (%i %i)", ret, data);
	}

	return ret;
}

int
OSDatxxxx::init_osd()
{
	int ret = PX4_OK;
	uint8_t data = OSD_ZERO_BYTE;

	if (_param_osd_atxxxx_cfg.get() == 2) {
		data |= OSD_PAL_TX_MODE;
	}

	ret |= writeRegister(0x00, data);
	ret |= writeRegister(0x04, OSD_ZERO_BYTE);

	enable_screen();

	return ret;
}

int
OSDatxxxx::readRegister(unsigned reg, uint8_t *data, unsigned count)
{
	uint8_t cmd[5] {}; // read up to 4 bytes

	cmd[0] = DIR_READ(reg);

	int ret = transfer(&cmd[0], &cmd[0], count + 1);

	if (ret != PX4_OK) {
		DEVICE_LOG("spi::transfer returned %d", ret);
		return ret;
	}

	memcpy(&data[0], &cmd[1], count);

	return ret;
}

int
OSDatxxxx::writeRegister(unsigned reg, uint8_t data)
{
	uint8_t cmd[2] {}; // write 1 byte

	cmd[0] = DIR_WRITE(reg);
	cmd[1] = data;

	int ret = transfer(&cmd[0], nullptr, 2);

	if (OK != ret) {
		DEVICE_LOG("spi::transfer returned %d", ret);
		return ret;
	}

	return ret;
}

int
OSDatxxxx::add_character_to_screen(char c, uint8_t pos_x, uint8_t pos_y)
{
	uint16_t position = (OSD_CHARS_PER_ROW * pos_y) + pos_x;
	uint8_t position_lsb = 0;
	int ret = PX4_ERROR;

	if (position > 0xFF) {
		position_lsb = static_cast<uint8_t>(position) - 0xFF;
		ret = writeRegister(0x05, 0x01); //DMAH

	} else {
		position_lsb = static_cast<uint8_t>(position);
		ret = writeRegister(0x05, 0x00); //DMAH
	}

	if (ret != 0) {
		return ret;
	}

	ret = writeRegister(0x06, position_lsb); //DMAL

	if (ret != 0) {
		return ret;
	}

	ret = writeRegister(0x07, c);

	return ret;
}

void
OSDatxxxx::add_string_to_screen_centered(const char *str, uint8_t pos_y, int max_length)
{
	int len = strlen(str);

	if (len > max_length) {
		len = max_length;
	}

	int pos = (OSD_CHARS_PER_ROW - max_length) / 2;
	int before = (max_length - len) / 2;

	for (int i = 0; i < before; ++i) {
		add_character_to_screen(' ', pos++, pos_y);
	}

	for (int i = 0; i < len; ++i) {
		add_character_to_screen(str[i], pos++, pos_y);
	}

	while (pos < (OSD_CHARS_PER_ROW + max_length) / 2) {
		add_character_to_screen(' ', pos++, pos_y);
	}
}

int
OSDatxxxx::add_string_to_screen(const char *str, uint8_t pos_x, uint8_t pos_y)
{
	int ret = PX4_OK;

	for (int i = 0; str[i] != '\0'; ++i) {
		ret |= add_character_to_screen(str[i], pos_x + i, pos_y);
	}

	return ret;
}

void
OSDatxxxx::clear_line(uint8_t pos_x, uint8_t pos_y, int length)
{
	for (int i = 0; i < length; ++i) {
		add_character_to_screen(' ', pos_x + i, pos_y);
	}
}

int
OSDatxxxx::add_battery_info(uint8_t pos_x, uint8_t pos_y)
{
	char buf[10];
	int ret = PX4_OK;

	char batt_symbol = OSD_SYMBOL_BATT_EMPTY;

	if (_battery_remaining >= 0.875f) {
		batt_symbol = OSD_SYMBOL_BATT_FULL;

	} else if (_battery_remaining >= 0.625f) {
		batt_symbol = OSD_SYMBOL_BATT_5;

	} else if (_battery_remaining >= 0.375f) {
		batt_symbol = OSD_SYMBOL_BATT_4;

	} else if (_battery_remaining >= 0.25f) {
		batt_symbol = OSD_SYMBOL_BATT_3;

	} else if (_battery_remaining >= 0.125f) {
		batt_symbol = OSD_SYMBOL_BATT_2;

	} else if (_battery_remaining >= 0.f) {
		batt_symbol = OSD_SYMBOL_BATT_1;
	}

	snprintf(buf, sizeof(buf), "%c%5.2f", batt_symbol, (double)_battery_voltage_v);
	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	ret |= add_character_to_screen('V', pos_x + 5, pos_y);

	pos_y++;
	pos_x++;

	snprintf(buf, sizeof(buf), "%5d", (int)_battery_discharge_mah);
	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	ret |= add_character_to_screen(OSD_SYMBOL_MAH, pos_x + 5, pos_y);

	return ret;
}

int
OSDatxxxx::add_cell_voltage(uint8_t pos_x, uint8_t pos_y)
{
	char buf[8];
	int ret = PX4_OK;

	if (_battery_valid && _battery_cell_count > 0) {
		float cell_voltage = _battery_voltage_v / _battery_cell_count;
		snprintf(buf, sizeof(buf), "%4.2fV", (double)cell_voltage);
	} else {
		snprintf(buf, sizeof(buf), "-----");
	}

	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	return ret;
}

int
OSDatxxxx::add_altitude(uint8_t pos_x, uint8_t pos_y)
{
	char buf[16];
	int ret = PX4_OK;

	if (_local_position_z > 99.f) {
		snprintf(buf, sizeof(buf), "%c%5u%c", OSD_SYMBOL_ALTITUDE, (unsigned)roundf(_local_position_z), OSD_SYMBOL_M);
	} else {
		snprintf(buf, sizeof(buf), "%c%5.2f%c", OSD_SYMBOL_ALTITUDE, (double)_local_position_z, OSD_SYMBOL_M);
	}

	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	return ret;
}

int
OSDatxxxx::add_flighttime(float flight_time, uint8_t pos_x, uint8_t pos_y)
{
	char buf[10];
	int ret = PX4_OK;

	snprintf(buf, sizeof(buf), "%c%5.1f", OSD_SYMBOL_FLIGHT_TIME, (double)flight_time);
	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	return ret;
}

int
OSDatxxxx::add_groundspeed(uint8_t pos_x, uint8_t pos_y)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%6.1f%c", (double)_ground_speed_kmh, OSD_SYMBOL_KMPH);
	buf[sizeof(buf) - 1] = '\0';
	return add_string_to_screen(buf, pos_x, pos_y);
}

int
OSDatxxxx::add_vertical_speed(uint8_t pos_x, uint8_t pos_y)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%6.1f%c", (double)_vertical_speed_m_s, OSD_SYMBOL_MPS);
	buf[sizeof(buf) - 1] = '\0';
	return add_string_to_screen(buf, pos_x, pos_y);
}

int
OSDatxxxx::add_yaw(uint8_t pos_x, uint8_t pos_y)
{
	char buf[8];
	snprintf(buf, sizeof(buf), "%c%03u", OSD_SYMBOL_ARROW_UP, (unsigned)_yaw_deg);
	buf[sizeof(buf) - 1] = '\0';
	return add_string_to_screen(buf, pos_x, pos_y);
}

int
OSDatxxxx::add_home_info(uint8_t pos_x, uint8_t pos_y)
{
	char bearing_buf[8];
	char distance_buf[8];

	if (_home_valid) {
		snprintf(bearing_buf, sizeof(bearing_buf), "%c%03u", OSD_SYMBOL_HOME_NEW, (unsigned)_home_bearing_deg);
		snprintf(distance_buf, sizeof(distance_buf), "%c%3u", OSD_SYMBOL_DIST_M, (unsigned)roundf(_home_distance_m));

	} else {
		snprintf(bearing_buf, sizeof(bearing_buf), "%c---", OSD_SYMBOL_HOME_NEW);
		snprintf(distance_buf, sizeof(distance_buf), "%c---", OSD_SYMBOL_DIST_M);
	}

	bearing_buf[sizeof(bearing_buf) - 1] = '\0';
	distance_buf[sizeof(distance_buf) - 1] = '\0';

	clear_line(pos_x, pos_y, 4);
	clear_line(pos_x, pos_y + 1, 7);

	int ret = PX4_OK;
	ret |= add_string_to_screen(bearing_buf, pos_x, pos_y);
	ret |= add_string_to_screen(distance_buf, pos_x, pos_y + 1);
	return ret;
}

int
OSDatxxxx::add_gps_info(uint8_t pos_x, uint8_t pos_y)
{
	constexpr int gps_info_width = 29;
	char raw[32];
	char buf[gps_info_width + 1];

	if (!_gps_data_valid) {
		snprintf(raw, sizeof(raw), "%c%c-- %c--.------ %c---.------", OSD_SYMBOL_SAT_L, OSD_SYMBOL_SAT_R,
			 OSD_SYMBOL_LAT, OSD_SYMBOL_LON);

	} else if (!_gps_fix_valid) {
		snprintf(raw, sizeof(raw), "%c%c00 %c--.------ %c---.------", OSD_SYMBOL_SAT_L, OSD_SYMBOL_SAT_R,
			 OSD_SYMBOL_LAT, OSD_SYMBOL_LON);

	} else {
		snprintf(raw, sizeof(raw), "%c%c%02u %c%.6f %c%.6f", OSD_SYMBOL_SAT_L, OSD_SYMBOL_SAT_R,
			 (unsigned)_gps_satellites_used, OSD_SYMBOL_LAT, _gps_lat_deg, OSD_SYMBOL_LON, _gps_lon_deg);
	}

	snprintf(buf, sizeof(buf), "%-29s", raw);
	buf[sizeof(buf) - 1] = '\0';
	return add_string_to_screen(buf, pos_x, pos_y);
}

int
OSDatxxxx::add_signal_strength(uint8_t pos_x, uint8_t pos_y)
{
	char buf[8];

	if (_rssi_valid) {
		snprintf(buf, sizeof(buf), "%c%.0f", OSD_SYMBOL_RSSI, (double)_rssi_dbm);

	} else {
		snprintf(buf, sizeof(buf), "%c---", OSD_SYMBOL_RSSI);
	}

	buf[sizeof(buf) - 1] = '\0';
	return add_string_to_screen(buf, pos_x, pos_y);
}

int
OSDatxxxx::add_link_quality(uint8_t pos_x, uint8_t pos_y)
{
	char buf[8];
	int ret = PX4_OK;

	if (_link_quality_valid) {
		snprintf(buf, sizeof(buf), "%c%03d", OSD_SYMBOL_LQ, _link_quality);

	} else {
		snprintf(buf, sizeof(buf), "%c---", OSD_SYMBOL_LQ);
	}

	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	return ret;
}

int
OSDatxxxx::add_ekf_fusion_status(uint8_t pos_x, uint8_t pos_y)
{
	char buf[6];

	if (_ekf_fusion_status_valid) {
		// G=gnss_pos H=gps_hgt B=baro_hgt F=opt_flow R=rng_hgt (upper=on, '-'=off; font has no lowercase)
		buf[0] = _cs_gnss_pos ? 'G' : '-';
		buf[1] = _cs_gps_hgt ? 'H' : '-';
		buf[2] = _cs_baro_hgt ? 'B' : '-';
		buf[3] = _cs_opt_flow ? 'F' : '-';
		buf[4] = _cs_rng_hgt ? 'R' : '-';
		buf[5] = '\0';

	} else {
		snprintf(buf, sizeof(buf), "-----");
	}

	return add_string_to_screen(buf, pos_x, pos_y);
}

int
OSDatxxxx::add_throttle(uint8_t pos_x, uint8_t pos_y)
{
	char buf[8];
	int ret = PX4_OK;

	if (_throttle_valid && hrt_elapsed_time(&_throttle_timestamp) < 300_ms) {
		// throttle_sp is 0.0 to 1.0, convert to percentage 0-100
		int throttle_pct = static_cast<int>(_throttle_sp * 100.0f + 0.5f);
		snprintf(buf, sizeof(buf), "%c%5u%%", OSD_SYMBOL_THROTTLE, throttle_pct);
	} else {
		snprintf(buf, sizeof(buf), "%c  ---%%", OSD_SYMBOL_THROTTLE);
	}

	buf[sizeof(buf) - 1] = '\0';

	for (int i = 0; buf[i] != '\0'; i++) {
		ret |= add_character_to_screen(buf[i], pos_x + i, pos_y);
	}

	return ret;
}

int
OSDatxxxx::enable_screen()
{
	uint8_t data = 0;
	int ret = PX4_OK;

	ret |= readRegister(0x00, &data, 1);
	ret |= writeRegister(0x00, data | 0x48);

	return ret;
}

int
OSDatxxxx::disable_screen()
{
	uint8_t data = 0;
	int ret = PX4_OK;

	ret |= readRegister(0x00, &data, 1);
	ret |= writeRegister(0x00, data & 0xF7);

	return ret;
}

int
OSDatxxxx::update_topics()
{
	/* update battery subscription */
	if (_battery_sub.updated()) {
		battery_status_s battery{};
		_battery_sub.copy(&battery);

		if (battery.connected) {
			_battery_voltage_v = battery.voltage_v;
			_battery_discharge_mah = battery.discharged_mah;
			_battery_remaining = battery.remaining;
			_battery_cell_count = battery.cell_count;
			_battery_valid = true;

		} else {
			_battery_valid = false;
		}
	}

	/* update vehicle local position subscription */
	if (_local_position_sub.updated()) {
		vehicle_local_position_s local_position{};
		_local_position_sub.copy(&local_position);

		_local_z_ned = local_position.z;
		_local_z_valid = local_position.z_valid;

		_local_position_valid = _local_z_valid && _home_lpos_valid;

		if (_local_position_valid) {
			/* Height above home (m, up positive): unlock/home ~ 0 */
			_local_position_z = -(_local_z_ned - _home_z_ned);
		}

		_vertical_speed_valid = local_position.v_z_valid;

		if (_vertical_speed_valid) {
			_vertical_speed_m_s = -local_position.vz;
		}

		_ground_speed_valid = local_position.v_xy_valid;

		if (_ground_speed_valid) {
			_ground_speed_kmh = sqrtf(local_position.vx * local_position.vx + local_position.vy * local_position.vy) * 3.6f;
		}
	}

	if (_estimator_status_flags_sub.updated()) {
		estimator_status_flags_s flags{};
		_estimator_status_flags_sub.copy(&flags);

		_cs_gnss_pos = flags.cs_gnss_pos;
		_cs_gps_hgt = flags.cs_gps_hgt;
		_cs_baro_hgt = flags.cs_baro_hgt;
		_cs_opt_flow = flags.cs_opt_flow;
		_cs_rng_hgt = flags.cs_rng_hgt;
		_ekf_fusion_status_valid = true;
	}

	if (_input_rc_sub.updated()) {
		input_rc_s input_rc{};
		_input_rc_sub.copy(&input_rc);

		_rssi_valid = PX4_ISFINITE(input_rc.rssi_dbm);

		if (_rssi_valid) {
			_rssi_dbm = input_rc.rssi_dbm;
		}

		_link_quality_valid = (input_rc.link_quality >= 0);
		if (_link_quality_valid) {
			_link_quality = input_rc.link_quality;
		}
	}

	if (_vehicle_thrust_setpoint_sub.updated()) {
		vehicle_thrust_setpoint_s vehicle_thrust_setpoint{};
		_vehicle_thrust_setpoint_sub.copy(&vehicle_thrust_setpoint);

		float throttle_norm = NAN;

		switch (_vehicle_type) {
		case vehicle_status_s::VEHICLE_TYPE_FIXED_WING:
			throttle_norm = vehicle_thrust_setpoint.xyz[0];
			break;

		case vehicle_status_s::VEHICLE_TYPE_ROTARY_WING:
			throttle_norm = -vehicle_thrust_setpoint.xyz[2];
			break;

		default:
			throttle_norm = vehicle_thrust_setpoint.xyz[0];
			break;
		}

		_throttle_valid = PX4_ISFINITE(throttle_norm);

		if (_throttle_valid) {
			throttle_norm = math::constrain(throttle_norm, 0.f, 1.f);
			_throttle_sp = throttle_norm;
			_throttle_timestamp = vehicle_thrust_setpoint.timestamp;
		}
	}

	if (_vehicle_attitude_sub.updated()) {
		vehicle_attitude_s vehicle_attitude{};
		_vehicle_attitude_sub.copy(&vehicle_attitude);

		matrix::Eulerf euler_attitude(matrix::Quatf(vehicle_attitude.q));
		float yaw_deg = math::degrees(euler_attitude.psi());

		if (yaw_deg < 0.f) {
			yaw_deg += 360.f;
		}

		_yaw_deg = static_cast<uint16_t>(roundf(yaw_deg)) % 360U;
		_yaw_valid = true;
	}

	{
		vehicle_global_position_s vehicle_global_position{};
		home_position_s home_position{};

		const bool global_position_valid = _vehicle_global_position_sub.copy(&vehicle_global_position)
			&& vehicle_global_position.lat_lon_valid;
		const bool home_copied = _home_position_sub.copy(&home_position);
		const bool home_position_valid = home_copied && home_position.valid_hpos;

		_home_lpos_valid = home_copied && home_position.valid_lpos;

		if (_home_lpos_valid) {
			_home_z_ned = home_position.z;
		}

		_local_position_valid = _local_z_valid && _home_lpos_valid;

		if (_local_position_valid) {
			_local_position_z = -(_local_z_ned - _home_z_ned);
		}

		_home_valid = global_position_valid && home_position_valid;

		if (_home_valid) {
			float bearing_to_home = math::degrees(get_bearing_to_next_waypoint(vehicle_global_position.lat,
					vehicle_global_position.lon,
					home_position.lat, home_position.lon));

			if (bearing_to_home < 0.f) {
				bearing_to_home += 360.f;
			}

			_home_bearing_deg = static_cast<uint16_t>(roundf(bearing_to_home)) % 360U;
			_home_distance_m = get_distance_to_next_waypoint(vehicle_global_position.lat,
					 vehicle_global_position.lon,
					 home_position.lat, home_position.lon);
		}
	}

	if (_sensor_gps_sub.updated()) {
		sensor_gps_s sensor_gps{};
		_sensor_gps_sub.copy(&sensor_gps);

		_gps_data_valid = hrt_elapsed_time(&sensor_gps.timestamp) < 2_s;
		_gps_fix_type = sensor_gps.fix_type;
		_gps_satellites_used = sensor_gps.satellites_used;
		_gps_fix_valid = _gps_data_valid && (sensor_gps.fix_type >= sensor_gps_s::FIX_TYPE_2D);

		if (_gps_fix_valid) {
			_gps_lat_deg = sensor_gps.latitude_deg;
			_gps_lon_deg = sensor_gps.longitude_deg;
		}
	}

	/* update vehicle status subscription */
	if (_vehicle_status_sub.updated()) {
		vehicle_status_s vehicle_status{};
		_vehicle_status_sub.copy(&vehicle_status);

		if (vehicle_status.arming_state == vehicle_status_s::ARMING_STATE_ARMED &&
		    _arming_state != vehicle_status_s::ARMING_STATE_ARMED) {
			// arming
			_arming_timestamp = hrt_absolute_time();

		} else if (vehicle_status.arming_state != vehicle_status_s::ARMING_STATE_ARMED &&
			   _arming_state == vehicle_status_s::ARMING_STATE_ARMED) {
			// disarming
		}

		_arming_state = vehicle_status.arming_state;
		_nav_state = vehicle_status.nav_state;
		_vehicle_type = vehicle_status.vehicle_type;
	}

	return PX4_OK;
}

uint8_t
OSDatxxxx::get_flighttime_row() const
{
	return _param_osd_atxxxx_cfg.get() == 1 ? 11 : 14;
}

uint8_t
OSDatxxxx::get_gps_row() const
{
	return _param_osd_atxxxx_cfg.get() == 1 ? 12 : 15;
}

const char *
OSDatxxxx::get_flight_mode(uint8_t nav_state)
{
	const char *flight_mode = "UNKNOWN";

	switch (nav_state) {
	case vehicle_status_s::NAVIGATION_STATE_MANUAL:
		flight_mode = "MANUAL";
		break;

	case vehicle_status_s::NAVIGATION_STATE_ALTCTL:
		flight_mode = "ALTITUDE";
		break;

	case vehicle_status_s::NAVIGATION_STATE_ALTITUDE_CRUISE:
		flight_mode = "CRUISE";
		break;

	case vehicle_status_s::NAVIGATION_STATE_POSCTL:
		flight_mode = "POSITION";
		break;

	case vehicle_status_s::NAVIGATION_STATE_AUTO_RTL:
		flight_mode = "RETURN";
		break;

	case vehicle_status_s::NAVIGATION_STATE_AUTO_MISSION:
		flight_mode = "MISSION";
		break;

	case vehicle_status_s::NAVIGATION_STATE_AUTO_LOITER:
	case vehicle_status_s::NAVIGATION_STATE_DESCEND:
	case vehicle_status_s::NAVIGATION_STATE_AUTO_TAKEOFF:
	case vehicle_status_s::NAVIGATION_STATE_AUTO_LAND:
	case vehicle_status_s::NAVIGATION_STATE_AUTO_FOLLOW_TARGET:
	case vehicle_status_s::NAVIGATION_STATE_AUTO_PRECLAND:
		flight_mode = "AUTO";
		break;

	case vehicle_status_s::NAVIGATION_STATE_ACRO:
		flight_mode = "ACRO";
		break;

	case vehicle_status_s::NAVIGATION_STATE_TERMINATION:
		flight_mode = "TERMINATE";
		break;

	case vehicle_status_s::NAVIGATION_STATE_OFFBOARD:
		flight_mode = "OFFBOARD";
		break;

	case vehicle_status_s::NAVIGATION_STATE_STAB:
		flight_mode = "STABILIZED";
		break;
	}

	return flight_mode;
}

int
OSDatxxxx::update_screen()
{
	int ret = PX4_OK;

	if (_battery_valid) {
		ret |= add_battery_info(1, 1);
		ret |= add_cell_voltage(9, 1);

	} else {
		clear_line(1, 1, 10);
		clear_line(1, 2, 10);
	}

	if (_yaw_valid) {
		ret |= add_yaw(16, 1);

	} else {
		clear_line(16, 1, 4);
	}

	ret |= add_home_info(23, 1);

	if (_local_position_valid) {
		ret |= add_altitude(1, 3);

	} else {
		clear_line(1, 3, 10);
	}

	if (_vertical_speed_valid) {
		ret |= add_vertical_speed(1, 4);

	} else {
		clear_line(1, 4, 10);
	}

	if (_ground_speed_valid) {
		ret |= add_groundspeed(1, 5);

	} else {
		clear_line(1, 5, 10);
	}

	if (_throttle_valid) {
		ret |= add_throttle(1, 6);

	} else {
		clear_line(1, 6, 7);
	}

	clear_line(23, 3, 5);
	ret |= add_signal_strength(23, 3);

	clear_line(23, 4, 5);
	ret |= add_link_quality(23, 4);

	clear_line(23, 5, 5);
	ret |= add_ekf_fusion_status(23, 5);

	const char *flight_mode = "";

	if (_arming_state == vehicle_status_s::ARMING_STATE_ARMED) {
		float flight_time_sec = static_cast<float>((hrt_absolute_time() - _arming_timestamp) / (1e6f));
		ret |= add_flighttime(flight_time_sec, 1, get_flighttime_row());

	} else {
		flight_mode = get_flight_mode(_nav_state);
	}

	add_string_to_screen_centered(flight_mode, 10, 10);
	ret |= add_gps_info(1, get_gps_row());

	return ret;
}

int
OSDatxxxx::reset()
{
	int ret = writeRegister(0x00, 0x02);
	usleep(100);

	return ret;
}

void
OSDatxxxx::RunImpl()
{
	if (should_exit()) {
		exit_and_cleanup();
		return;
	}

	update_topics();

	update_screen();
}

void
OSDatxxxx::print_usage()
{
	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description
OSD driver for the ATXXXX chip that is mounted on the OmnibusF4SD board for example.

It can be enabled with the OSD_ATXXXX_CFG parameter.
)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("atxxxx", "driver");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_PARAMS_I2C_SPI_DRIVER(false, true);
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
}

int
atxxxx_main(int argc, char *argv[])
{
	using ThisDriver = OSDatxxxx;
	BusCLIArguments cli{false, true};
	cli.spi_mode = SPIDEV_MODE0;
	cli.default_spi_frequency = OSD_SPI_BUS_SPEED;

	const char *verb = cli.parseDefaultArguments(argc, argv);

	if (!verb) {
		ThisDriver::print_usage();
		return -1;
	}

	BusInstanceIterator iterator(MODULE_NAME, cli, DRV_OSD_DEVTYPE_ATXXXX);

	if (!strcmp(verb, "start")) {
		return ThisDriver::module_start(cli, iterator);
	}

	if (!strcmp(verb, "stop")) {
		return ThisDriver::module_stop(iterator);
	}

	if (!strcmp(verb, "status")) {
		return ThisDriver::module_status(iterator);
	}

	ThisDriver::print_usage();
	return -1;
}
