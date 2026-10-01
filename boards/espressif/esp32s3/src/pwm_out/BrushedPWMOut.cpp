/****************************************************************************
 * ESP32-S3 brushed motor output module.
 *
 * Uses the standard PX4 MixingOutput (including actuator_test), with the
 * board's 0..2100 duty units instead of servo/ESC pulse-width auto-configuration.
 ****************************************************************************/

#include <board_config.h>
#include <drivers/drv_pwm_output.h>
#include <lib/mixer_module/mixer_module.hpp>
#include <px4_arch/io_timer.h>
#include <px4_platform_common/module.h>
#include <uORB/SubscriptionInterval.hpp>
#include <uORB/topics/parameter_update.h>

using namespace time_literals;

class BrushedPWMOut final : public ModuleBase<BrushedPWMOut>, public OutputModuleInterface
{
public:
	BrushedPWMOut() : OutputModuleInterface(MODULE_NAME, px4::wq_configurations::hp_default) {}

	~BrushedPWMOut() override
	{
		if (_initialized) {
			up_pwm_servo_deinit(_mask);
		}
	}

	static int task_spawn(int argc, char *argv[])
	{
		BrushedPWMOut *instance = new BrushedPWMOut();

		if (!instance) {
			return PX4_ERROR;
		}

		if (!instance->init()) {
			delete instance;
			return PX4_ERROR;
		}

		_object.store(instance);
		_task_id = task_id_is_work_queue;
		instance->ScheduleNow();
		return PX4_OK;
	}

	static int custom_command(int argc, char *argv[]) { return print_usage("unknown command"); }

	static int print_usage(const char *reason = nullptr)
	{
		if (reason) {
			PX4_WARN("%s", reason);
		}

		PRINT_MODULE_DESCRIPTION("ESP32-S3 brushed motor PWM: output 0..2100 = duty 0..100%.");
		PRINT_MODULE_USAGE_NAME("pwm_out", "driver");
		PRINT_MODULE_USAGE_COMMAND("start");
		PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
		return PX4_OK;
	}

	int print_status() override
	{
		PX4_INFO("LEDC brushed PWM: %ld Hz, full scale %u", (long)_rate, BOARD_PWM_DRIVE_FULL_SCALE);
		PX4_INFO("initialized mask: 0x%02lx", (unsigned long)_mask);
		_mixing_output.printStatus();
		return PX4_OK;
	}

	bool updateOutputs(uint16_t outputs[MAX_ACTUATORS], unsigned num_outputs,
			   unsigned num_control_groups_updated) override
	{
		for (unsigned i = 0; i < num_outputs && i < DIRECT_PWM_OUTPUT_CHANNELS; ++i) {
			if (!_mixing_output.isFunctionSet(i)) {
				outputs[i] = 0;
			}

			if (_mask & (1U << i)) {
				up_pwm_servo_set(i, outputs[i]);
			}
		}

		return true;
	}

private:
	bool init()
	{
		param_t timer_param = param_find("PWM_MAIN_TIM0");

		if (param_get(timer_param, &_rate) != PX4_OK || _rate < 10 || _rate > 40000) {
			PX4_ERR("PWM_MAIN_TIM0 must be 10..40000 Hz (no OneShot/DShot)");
			return false;
		}

		const uint32_t requested = (1U << DIRECT_PWM_OUTPUT_CHANNELS) - 1U;
		const int initialized = up_pwm_servo_init(requested);

		if (initialized <= 0) {
			PX4_ERR("PWM init failed: %d", initialized);
			return false;
		}

		_mask = initialized;
		_initialized = true;

		if (up_pwm_servo_set_rate_group_update(0, _rate) != PX4_OK) {
			PX4_ERR("PWM timer configuration failed");
			return false;
		}

		updateParams();
		up_pwm_servo_arm(true, _mask);
		return true;
	}

	void Run() override
	{
		if (should_exit()) {
			ScheduleClear();
			_mixing_output.unregister();
			exit_and_cleanup();
			return;
		}

		if (_parameter_update_sub.updated()) {
			parameter_update_s update;
			_parameter_update_sub.copy(&update);
			// Board defaults/metadata define the duty range. Function assignment
			// must not overwrite a user's min/max or introduce an ESC idle pulse.
			updateParams();
		}

		_mixing_output.update();
		_mixing_output.updateSubscriptions(true);
	}

	MixingOutput _mixing_output{"PWM_MAIN", DIRECT_PWM_OUTPUT_CHANNELS, *this,
		MixingOutput::SchedulingPolicy::Auto, true};
	uORB::SubscriptionInterval _parameter_update_sub{ORB_ID(parameter_update), 1_s};
	uint32_t _mask{0};
	int32_t _rate{400};
	bool _initialized{false};
};

extern "C" __EXPORT int pwm_out_main(int argc, char *argv[])
{
	return BrushedPWMOut::main(argc, argv);
}
