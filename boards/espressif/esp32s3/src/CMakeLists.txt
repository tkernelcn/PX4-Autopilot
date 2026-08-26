############################################################################
#
#   Copyright (c) 2015 PX4 Development Team. All rights reserved.
#
############################################################################

# Board bring-up: init, LED, I2C/SPI description, PWM timer map, param MTD.
add_library(drivers_board
	init.c
	led.c
	i2c.cpp
	spi.cpp
	timer_config.cpp
	mtd.cpp
)

target_link_libraries(drivers_board
	PRIVATE
		arch_io_pins
		arch_spi
		drivers__led # drv_led_start
		nuttx_arch
		nuttx_drivers
		px4_layer
)
