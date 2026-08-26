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
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/tasks.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/board.h>
#include <nuttx/spi/spi.h>

#ifdef CONFIG_ESP32S3_SPIFLASH
#include <nuttx/drivers/drivers.h>
#include <nuttx/mtd/mtd.h>
#include "esp32s3_spiflash.h"
#include "esp32s3_spiflash_mtd.h"
#endif

#include "board_config.h"
#include "esp32s3_spi.h"

#include <arch/board/board.h>

#include <drivers/drv_board_led.h>
#include <drivers/drv_hrt.h>

#include <px4_platform/board_dma_alloc.h>
#include <px4_platform_common/init.h>
#include <px4_arch/io_timer.h>

#include <systemlib/px4_macros.h>

#include "esp32s3_gpio.h"
#include "hardware/esp32s3_gpio_sigmap.h"

__BEGIN_DECLS
extern void led_init(void);
extern void led_on(int led);
extern void led_off(int led);
__END_DECLS

__EXPORT void board_peripheral_reset(int ms)
{
	UNUSED(ms);
}

__EXPORT void board_on_reset(int status)
{
	/* Do not touch PWM/GPIO here. NuttX calls this from
	 * esp32s3_board_initialize() in IRAM before .flash.text helpers
	 * (io_timer, gpio matrix) are safe to execute. PWM pins are
	 * configured later in board_app_initialize().
	 */
	UNUSED(status);
}

int board_read_VBUS_state(void)
{
	return 0;
}

/* Must live in IRAM: this is the first PX4 function called from
 * __esp32s3_start() (still before nx_start). A flash `entry` at this
 * point IllegalInstruction'd (EXCCAUSE=0, PC=esp32s3_board_initialize).
 */
__EXPORT void __attribute__((section(".iram1"))) esp32s3_board_initialize(void)
{
}

static void board_pwm_gpio_init(void)
{
#if defined(BOARD_HAS_PWM)
	for (int i = 0; i < DIRECT_PWM_OUTPUT_CHANNELS; ++i) {
		px4_arch_configgpio(io_timer_channel_get_gpio_output(i));
		esp32s3_gpio_matrix_out(timer_io_channels[i].gpio_out,
					LEDC_LS_SIG_OUT0_IDX + timer_io_channels[i].timer_channel,
					false, false);
	}
#endif
}

#ifdef CONFIG_ESP32S3_SPI2
static struct spi_dev_s *spi2;
#endif

#ifdef CONFIG_ESP32S3_SPI3
static struct spi_dev_s *spi3;
#endif

__EXPORT int board_app_initialize(uintptr_t arg)
{
	UNUSED(arg);

	/* Hardware first. px4_platform_init() used to run before SPI/MTD and
	 * immediately issued an I2C general-call; that path LoadProhibited if
	 * the bus handle was NULL, before any USB console bytes were sent.
	 */

	if (board_dma_alloc_init() < 0) {
		/* silent */
	}

	led_init();
	drv_led_start();
	board_pwm_gpio_init();

	esp32s3_spiinitialize();

#ifdef CONFIG_ESP32S3_SPI2
	spi2 = esp32s3_spibus_initialize(2);

	if (spi2) {
		SPI_SETFREQUENCY(spi2, 10000000);
		SPI_SETBITS(spi2, 8);
		SPI_SETMODE(spi2, SPIDEV_MODE3);
	}

#endif

#ifdef CONFIG_ESP32S3_SPI3
	spi3 = esp32s3_spibus_initialize(3);

	if (spi3) {
		SPI_SETFREQUENCY(spi3, 10000000);
		SPI_SETBITS(spi3, 8);
		SPI_SETMODE(spi3, SPIDEV_MODE3);
	}

#endif

#if defined(BOARD_ENABLE_MTD_PARAMS) && BOARD_ENABLE_MTD_PARAMS
#ifdef CONFIG_ESP32S3_SPIFLASH

/* Params partition: must match boot/partitions.csv and src/mtd.cpp. */
#ifndef BOARD_MTD_PARAMS_OFFSET
# define BOARD_MTD_PARAMS_OFFSET 0x310000
#endif
#ifndef BOARD_MTD_PARAMS_SIZE
# define BOARD_MTD_PARAMS_SIZE   0x10000
#endif

	{
		int ret = mkdir("/fs", 0777);

		if (ret < 0 && errno != EEXIST) {
			syslog(LOG_ERR, "ERROR: mkdir /fs failed: %d\n", errno);
		}

		ret = esp32s3_spiflash_init();

		if (ret < 0) {
			syslog(LOG_ERR, "ERROR: Failed to initialize SPI Flash: %d\n", ret);

		} else {
			FAR struct mtd_dev_s *mtd;

			mtd = esp32s3_spiflash_alloc_mtdpart(BOARD_MTD_PARAMS_OFFSET,
							     BOARD_MTD_PARAMS_SIZE,
							     false);

			if (!mtd) {
				syslog(LOG_ERR, "ERROR: Failed to alloc MTD partition of SPI Flash\n");

			} else {
#ifdef CONFIG_BCH
				/* register_mtddriver() makes an MTD inode. POSIX open() of
				 * that inode is ENXIO unless CONFIG_BCH (see fs_open.c).
				 * PX4 param uses open/read/write, so wrap FTL + BCH like ESP32.
				 */
				ret = ftl_initialize(0, mtd);

				if (ret < 0) {
					syslog(LOG_ERR, "ERROR: ftl_initialize failed: %d\n", ret);

				} else {
					ret = bchdev_register("/dev/mtdblock0", "/fs/mtd_params", false);

					if (ret < 0) {
						syslog(LOG_ERR, "ERROR: bchdev_register /fs/mtd_params: %d\n", ret);

					} else {
						syslog(LOG_INFO, "[boot] /fs/mtd_params ready (FTL+BCH)\n");
					}
				}

#else
				ret = register_mtddriver("/fs/mtd_params", mtd, 0777, NULL);

				if (ret < 0) {
					syslog(LOG_ERR, "ERROR: Failed to register MTD: %d\n", ret);
				}

#endif
			}
		}
	}
#endif
#endif /* BOARD_ENABLE_MTD_PARAMS */

	usleep(100000);
	px4_platform_init();
	px4_platform_configure();

	return OK;
}
