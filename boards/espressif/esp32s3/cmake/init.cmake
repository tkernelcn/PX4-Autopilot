# Keep CONFIG_DRIVERS_PWM_OUT enabled, but build the board's duty-cycle module
# instead of the generic servo/ESC module. This hook runs before module creation.
if(CONFIG_DRIVERS_PWM_OUT)
	list(REMOVE_ITEM config_module_list drivers/pwm_out)
endif()
