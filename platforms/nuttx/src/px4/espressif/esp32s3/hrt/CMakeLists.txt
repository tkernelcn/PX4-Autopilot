############################################################################
#
#   Copyright (c) 2015-2019 PX4 Development Team. All rights reserved.
#
############################################################################

px4_add_library(arch_hrt
	hrt.c
)

target_include_directories(arch_hrt PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_options(arch_hrt
	PRIVATE
		${MAX_CUSTOM_OPT_LEVEL}
		-Wno-cast-align # TODO: fix and enable
)

target_link_libraries(arch_hrt
	INTERFACE
		nuttx_arch
)
