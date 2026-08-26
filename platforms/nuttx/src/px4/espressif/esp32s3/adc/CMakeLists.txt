############################################################################
#
#   Copyright (c) 2019 PX4 Development Team. All rights reserved.
#
############################################################################

px4_add_library(arch_adc
	adc.cpp
)

target_link_libraries(arch_adc
	PRIVATE
		nuttx_arch
		nuttx_drivers
)
