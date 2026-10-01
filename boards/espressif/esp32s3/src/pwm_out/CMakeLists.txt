px4_add_module(
	MODULE drivers__pwm_out
	MAIN pwm_out
	SRCS
		BrushedPWMOut.cpp
	MODULE_CONFIG
		../../pwm_out.yaml
	DEPENDS
		arch_io_pins
		mixer_module
)
