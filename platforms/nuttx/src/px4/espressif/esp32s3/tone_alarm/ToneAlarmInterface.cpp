/****************************************************************************
 *
 *   Copyright (c) 2026 PX4 Development Team. All rights reserved.
 *
 * ESP32-S3: no buzzer wired — stub satisfies tone_alarm / tune_control linkage.
 *
 ****************************************************************************/

#include <drivers/drv_tone_alarm.h>
#include <px4_platform_common/defines.h>

namespace ToneAlarmInterface
{

void init()
{
}

hrt_abstime start_note(unsigned frequency)
{
	(void)frequency;
	return 0;
}

void stop_note()
{
}

} /* namespace ToneAlarmInterface */
