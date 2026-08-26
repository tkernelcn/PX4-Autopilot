/****************************************************************************
 *
 * ESP32-S3 PX4 arch ADC (minimal bring-up).
 *
 * Full SAR ADC + calibration needs NuttX esp32s3_sens / esp-hal headers.
 * This stub lets board_adc + sensors link for incremental stage testing.
 *
 ****************************************************************************/

#include <board_config.h>

#include <nuttx/arch.h>

#include <drivers/drv_adc.h>

static bool g_adc_initialized;

int px4_arch_adc_init(uint32_t base_address)
{
	(void)base_address;
	g_adc_initialized = true;
	return OK;
}

void px4_arch_adc_uninit(uint32_t base_address)
{
	(void)base_address;
	g_adc_initialized = false;
}

uint32_t px4_arch_adc_sample(uint32_t base_address, unsigned channel)
{
	(void)base_address;

	if (!g_adc_initialized) {
		return UINT32_MAX;
	}

	if (((ADC_CHANNELS) & (1u << channel)) == 0) {
		return 0;
	}

	/* TODO: wire ESP32-S3 SAR ADC once esp-hal/sens headers are in-tree */
	return 0;
}

float px4_arch_adc_reference_v()
{
	return 3.1f;
}

uint32_t px4_arch_adc_temp_sensor_mask()
{
	return 0;
}

uint32_t px4_arch_adc_dn_fullcount()
{
	return 1u << 12;
}
