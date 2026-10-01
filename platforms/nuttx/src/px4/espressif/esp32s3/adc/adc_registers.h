/****************************************************************************
 * ESP32-S3 ADC2 RTC single-conversion register subset.
 * Register layout and eFuse decoding reference: ESP-IDF v5.2.1
 * components/hal/esp32s3/include/hal/{adc_ll,sar_ctrl_ll}.h and
 * components/efuse/esp32s3/{esp_efuse_table.csv,esp_efuse_rtc_calib.c}.
 * No ESP-IDF runtime or NuttX modifications are required.
 ****************************************************************************/
#pragma once

#include "hardware/esp32s3_soc.h"

namespace esp32s3_adc
{
constexpr uint32_t reader = DR_REG_SENS_BASE + 0x24;
constexpr uint32_t control = DR_REG_SENS_BASE + 0x30;
constexpr uint32_t mux = DR_REG_SENS_BASE + 0x34;
constexpr uint32_t attenuation = DR_REG_SENS_BASE + 0x38;
constexpr uint32_t power = DR_REG_SENS_BASE + 0x3c;
constexpr uint32_t clock_gate = DR_REG_SENS_BASE + 0x104;
constexpr uint32_t arbiter = DR_REG_APB_SARADC_BASE + 0x38;
constexpr uint32_t rtc_pad17 = DR_REG_RTCIO_BASE + 0xc8;
constexpr uint32_t rtc_output_disable = DR_REG_RTCIO_BASE + 0x14;
constexpr uint32_t analog_config = 0x6000e044;
constexpr uint32_t analog_config2 = 0x6000e048;

constexpr uint32_t start = 1U << 17;
constexpr uint32_t done = 1U << 16;
constexpr uint32_t pad_mask = 0xfffU << 19;
constexpr uint32_t software_control = (1U << 31) | (1U << 18);
constexpr uint32_t invalid_data = 0xc000U;
constexpr uint32_t data_mask = 0xfffU;
constexpr uint32_t power_mask = 3U << 29;
constexpr uint32_t sar_clock = 1U << 30;
constexpr uint32_t iomux_clock = 1U << 31;
constexpr uint32_t pad_mux = 1U << 19;
constexpr uint32_t pad_disable = (3U << 27) | (3U << 17) | (15U << 13);
constexpr unsigned atten_6db = 2;
}
