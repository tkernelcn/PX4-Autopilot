/****************************************************************************
 *
 *   Copyright (C) 2021 PX4 Development Team. All rights reserved.
 *
 ****************************************************************************/

#include <px4_platform_common/px4_manifest.h>

/*
 * On-chip SPI flash parameter partition.
 * Physical range is registered in init.c:
 *   BOARD_MTD_PARAMS_OFFSET 0x310000
 *   BOARD_MTD_PARAMS_SIZE   0x10000 (64 KiB)
 * Geometry comes from the MTD driver registered in init.c.
 * nblocks=1 matches boards/espressif/esp32 (single on-chip param slot).
 */
static const px4_mft_device_t onchip_flash = {
	.bus_type = px4_mft_device_t::ONCHIP,
};

static const px4_mtd_entry_t params_flash = {
	.device = &onchip_flash,
	.npart = 1,
	.partd = {
		{
			.type = MTD_PARAMETERS,
			.path = "/fs/mtd_params",
			.nblocks = 1
		}
	},
};

static const px4_mtd_manifest_t board_mtd_config = {
	.nconfigs = 1,
	.entries  = {
		&params_flash
	}
};

static const px4_mft_entry_s mtd_mft = {
	.type = MTD,
	.pmft = (void *) &board_mtd_config,
};

static const px4_mft_s mft = {
	.nmft = 1,
	.mfts = {&mtd_mft}
};

const px4_mft_s *board_get_manifest(void)
{
	return &mft;
}
