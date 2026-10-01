/****************************************************************************
 *
 * ESP32-S3 ADC2_CH6 battery voltage input (GPIO17), RTC oneshot, 6 dB.
 * Owns ADC2 exclusively: this port currently has no Wi-Fi/ULP ADC users.
 * eFuse init-code and linear voltage calibration are used. BAT1_V_DIV can
 * trim residual board gain error; this is not IDF polynomial curve fitting.
 *
 ****************************************************************************/

#include <board_config.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <errno.h>
#include <drivers/drv_adc.h>
#include <drivers/drv_hrt.h>
#include <px4_platform_common/log.h>
#include <px4_platform_common/time.h>
#include <pthread.h>

#include "xtensa.h"
#include "esp32s3_gpio.h"
#include "hardware/esp32s3_efuse.h"
#include "hardware/esp32s3_rtccntl.h"
#include "hardware/esp32s3_system.h"
#include "adc_registers.h"

/* ROM internal analog-bus access, already provided by esp32s3_rom.ld. */
extern "C" void esp_rom_regi2c_write_mask(uint8_t block, uint8_t host, uint8_t reg,
		uint8_t msb, uint8_t lsb, uint8_t value);

using namespace esp32s3_adc;
static_assert(BOARD_BATTERY_ADC_GPIO == 17 && ADC_BATTERY_VOLTAGE_CHANNEL == 6,
	      "This backend implements GPIO17 / ADC2_CH6 only");

static bool g_adc_initialized;
static float g_reference_v;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* Read a small field from eFuse block 2, including fields spanning two words.
 * Read-only: never issue an eFuse programming command.
 */
static uint32_t efuse_field(unsigned bit, unsigned width)
{
	const unsigned shift = bit % 32;
	uint32_t value = getreg32(EFUSE_RD_SYS_PART1_DATA0_REG + (bit / 32) * 4) >> shift;

	if (shift + width > 32) {
		value |= getreg32(EFUSE_RD_SYS_PART1_DATA0_REG + (bit / 32 + 1) * 4) << (32 - shift);
	}

	return value & ((1U << width) - 1U);
}

static void analog_write(uint8_t reg, uint8_t msb, uint8_t lsb, uint8_t value)
{
	irqstate_t flags = px4_enter_critical_section();
	esp_rom_regi2c_write_mask(0x69, 1, reg, msb, lsb, value);
	px4_leave_critical_section(flags);
}

static uint32_t read_once()
{
	const uint32_t config = software_control | (1U << (19 + ADC_BATTERY_VOLTAGE_CHANNEL));
	putreg32(config, control);
	putreg32(config | start, control);
	const hrt_abstime deadline = hrt_absolute_time() + 200;

	/* Do not hold an IRQ lock while polling. One failed sample costs at most
	 * 200 us of work-queue time rather than hanging hp_default indefinitely.
	 */
	while (!(getreg32(control) & done)) {
		if (hrt_absolute_time() >= deadline) {
			putreg32(config, control);
			return UINT32_MAX;
		}
	}

	const uint32_t result = getreg32(control) & 0xffffU;
	putreg32(config, control);
	return (result & invalid_data) ? UINT32_MAX : result & data_mask;
}

int px4_arch_adc_init(uint32_t base_address)
{
	(void)base_address;
	pthread_mutex_lock(&g_lock);

	if (g_adc_initialized) {
		pthread_mutex_unlock(&g_lock);
		return OK;
	}

	if (efuse_field(128, 2) != 1) {
		PX4_ERR("ADC: unsupported/missing eFuse calibration version");
		pthread_mutex_unlock(&g_lock);
		return -ENOTSUP;
	}

	const uint32_t init_code = 2020 + efuse_field(175, 8) + efuse_field(183, 6) + efuse_field(189, 6);
	// ADC1 atten3 -> ADC1 atten2 -> ADC2 atten2, at the factory 850 mV point.
	const uint32_t cal_code = 900 + efuse_field(225, 8) + 800 + efuse_field(217, 8)
				 - efuse_field(248, 7) + 20;
	g_reference_v = 0.850f * 4096.f / (float)cal_code;

	// Disable digital I/O before selecting RTC analog pad. No pull resistors.
	esp32s3_configgpio(BOARD_BATTERY_ADC_GPIO, 0);
	modifyreg32(clock_gate, 0, iomux_clock | sar_clock);
	putreg32(1U << (BOARD_BATTERY_ADC_GPIO + 10), rtc_output_disable);
	modifyreg32(rtc_pad17, pad_disable, pad_mux);

	modifyreg32(SYSTEM_PERIP_CLK_EN0_REG, 0, SYSTEM_APB_SARADC_CLK_EN);
	modifyreg32(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_APB_SARADC_RST, 0);
	modifyreg32(power, power_mask, 3U << 29);
	// Fixed arbitration: RTC(2) > Wi-Fi(1) > digital(0); RTC grant selected.
	putreg32((1U << 12) | (1U << 10) | (2U << 8) | (1U << 3), arbiter);
	modifyreg32(reader, (1U << 30) | (1U << 29) | (0xffU << 19) | 0xffU, (1U << 18) | 1U);
	modifyreg32(mux, (1U << 31) | (7U << 28), 4U << 28);
	modifyreg32(attenuation, 3U << (ADC_BATTERY_VOLTAGE_CHANNEL * 2),
		    atten_6db << (ADC_BATTERY_VOLTAGE_CHANNEL * 2));

	// Enable internal SAR analog configuration bus, set DREF and factory ICode.
	modifyreg32(RTC_CNTL_RTC_ANA_CONF_REG, 0, RTC_CNTL_SAR_I2C_PU);
	modifyreg32(analog_config, 1U << 18, 0);
	modifyreg32(analog_config2, 0, 1U << 16);
	analog_write(2, 2, 0, 2); // SAR sample cycle (shared setting)
	analog_write(5, 6, 4, 4); // ADC2 DREF
	analog_write(7, 7, 7, 0); // Disconnect calibration GND
	analog_write(4, 3, 0, init_code >> 8);
	analog_write(3, 7, 0, init_code & 0xff);
	px4_usleep(40000); // allow divider capacitor and analog power to settle

	if (read_once() == UINT32_MAX) {
		PX4_ERR("ADC2_CH6 initial conversion failed");
		modifyreg32(control, start | pad_mask, 0);
		modifyreg32(power, power_mask, 0); // return to FSM power control
		pthread_mutex_unlock(&g_lock);
		return -EIO;
	}

	g_adc_initialized = true;
	PX4_INFO("ADC2_CH6 GPIO17: 6 dB, eFuse init=%lu cal=%lu, full-scale=%.3f V",
		 (unsigned long)init_code, (unsigned long)cal_code, (double)g_reference_v);
	pthread_mutex_unlock(&g_lock);
	return OK;
}

void px4_arch_adc_uninit(uint32_t base_address)
{
	(void)base_address;
	pthread_mutex_lock(&g_lock);
	if (g_adc_initialized) {
		modifyreg32(control, start | pad_mask, 0);
		modifyreg32(power, power_mask, 0);
	}
	g_adc_initialized = false;
	pthread_mutex_unlock(&g_lock);
}

uint32_t px4_arch_adc_sample(uint32_t base_address, unsigned channel)
{
	(void)base_address;

	if (channel != ADC_BATTERY_VOLTAGE_CHANNEL) {
		return 0;//UINT32_MAX;
	}

	pthread_mutex_lock(&g_lock);
	if (!g_adc_initialized) {
		pthread_mutex_unlock(&g_lock);
		return UINT32_MAX;
	}

	uint32_t sum = 0;
	for (unsigned i = 0; i < 4; ++i) {
		const uint32_t raw = read_once();
		if (raw == UINT32_MAX) {
			pthread_mutex_unlock(&g_lock);
			return 0;//UINT32_MAX;
		}
		sum += raw;
	}
	pthread_mutex_unlock(&g_lock);
	return (sum + 2) / 4;
}

float px4_arch_adc_reference_v()
{
	return g_reference_v;
}

uint32_t px4_arch_adc_temp_sensor_mask()
{
	return 0;
}

uint32_t px4_arch_adc_dn_fullcount()
{
	return 1u << 12;
}
