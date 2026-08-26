/****************************************************************************
 *
 *   Copyright (c) 2012, 2013 PX4 Development Team. All rights reserved.
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

/**
 * @file hrt.c
 *
 * High-resolution timer callouts and timekeeping for ESP32-S3.
 *
 * Uses Timer Group general-purpose timers directly (same approach as esp32/hrt).
 * SYSTIMER TARGET0 is reserved for the NuttX system tick.
 */

#include <px4_platform_common/px4_config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <sys/types.h>
#include <stdbool.h>

#include <assert.h>
#include <debug.h>
#include <time.h>
#include <queue.h>
#include <errno.h>
#include <string.h>

#include <xtensa.h>

#include "esp32_irq.h"

#include "hardware/esp32s3_soc.h"
#include "hardware/esp32s3_tim.h"

#include <board_config.h>
#include <drivers/drv_hrt.h>

#ifdef CONFIG_DEBUG_HRT
#  define hrtinfo _info
#else
#  define hrtinfo(x...)
#endif

#ifdef HRT_TIMER

/* HRT configuration — map HRT_TIMER to ESP32-S3 timer group instances */
#if HRT_TIMER == 0
# define HRT_TG_INDEX                     0
# define HRT_TIM_T1                       0
# define HRT_TIMER_PERIPH                 ESP32S3_PERIPH_TG_T0_LEVEL
# define HRT_TIMER_VECTOR                 ESP32S3_IRQ_TG_T0_LEVEL
# define HRT_TIMER_INT_ENA                TIMG_T0_INT_ENA
# define HRT_TIMER_INT_CLR                TIMG_T0_INT_CLR
# if defined(CONFIG_ESP32S3_TIMER0)
#  error must not set CONFIG_ESP32S3_TIMER0=y and HRT_TIMER=0
# endif
#elif HRT_TIMER == 1
# define HRT_TG_INDEX                     0
# define HRT_TIM_T1                       1
# define HRT_TIMER_PERIPH                 ESP32S3_PERIPH_TG_T1_LEVEL
# define HRT_TIMER_VECTOR                 ESP32S3_IRQ_TG_T1_LEVEL
# define HRT_TIMER_INT_ENA                TIMG_T1_INT_ENA
# define HRT_TIMER_INT_CLR                TIMG_T1_INT_CLR
# if defined(CONFIG_ESP32S3_TIMER1)
#  error must not set CONFIG_ESP32S3_TIMER1=y and HRT_TIMER=1
# endif
#elif HRT_TIMER == 2
# define HRT_TG_INDEX                     1
# define HRT_TIM_T1                       0
# define HRT_TIMER_PERIPH                 ESP32S3_PERIPH_TG1_T0_LEVEL
# define HRT_TIMER_VECTOR                 ESP32S3_IRQ_TG1_T0_LEVEL
# define HRT_TIMER_INT_ENA                TIMG_T0_INT_ENA
# define HRT_TIMER_INT_CLR                TIMG_T0_INT_CLR
# if defined(CONFIG_ESP32S3_TIMER2)
#  error must not set CONFIG_ESP32S3_TIMER2=y and HRT_TIMER=2
# endif
#elif HRT_TIMER == 3
# define HRT_TG_INDEX                     1
# define HRT_TIM_T1                       1
# define HRT_TIMER_PERIPH                 ESP32S3_PERIPH_TG1_T1_LEVEL
# define HRT_TIMER_VECTOR                 ESP32S3_IRQ_TG1_T1_LEVEL
# define HRT_TIMER_INT_ENA                TIMG_T1_INT_ENA
# define HRT_TIMER_INT_CLR                TIMG_T1_INT_CLR
# if defined(CONFIG_ESP32S3_TIMER3)
#  error must not set CONFIG_ESP32S3_TIMER3=y and HRT_TIMER=3
# endif
#else
# error HRT_TIMER must be a value between 0 and 3
#endif

#define HRT_TG_BASE                       REG_TIMG_BASE(HRT_TG_INDEX)
#define HRT_TIM_BASE                      (HRT_TG_BASE + ((HRT_TIM_T1) ? 0x24 : 0))
#define HRT_TIMER_BASE                    HRT_TIM_BASE
#define HRT_TIMER_PRIO                    1
#define HRT_TIMER_CLOCK                   (80 * 1000000)
#define HRT_TIMER_INT_ENA_OFFSET          0x70
#define HRT_TIMER_CLR_OFFSET              0x7c

#define REG(_reg)                         (*(volatile uint32_t *)(HRT_TIMER_BASE + _reg))

#define HRT_CONFIG_OFFSET                 0x00
#define HRT_LOAD_LO_OFFSET                0x0018
#define HRT_LOAD_HI_OFFSET                0x001c
#define HRT_LOAD_OFFSET                   0x0020
#define HRT_ALARM_LO_OFFSET               0x0010
#define HRT_ALARM_HI_OFFSET               0x0014
#define HRT_UPDATE_OFFSET                 0x000c
#define HRT_LO_OFFSET                     0x0004
#define HRT_HI_OFFSET                     0x0008
#define HRT_DIVIDER_S                     13
#define HRT_DIVIDER_M                     (0xffff << 13)
#define HRT_ALARM_EN                      (1 << 10)
#define HRT_AUTORELOAD                    (1 << 29)
#define HRT_TIMER_EN                      (1 << 31)
#define HRT_INCREASE                      (1 << 30)

#define rLO                               REG(HRT_LO_OFFSET)
#define rHI                               REG(HRT_HI_OFFSET)
#define rUPDATE                           REG(HRT_UPDATE_OFFSET)
#define rALARMLO                          REG(HRT_ALARM_LO_OFFSET)
#define rALARMHI                          REG(HRT_ALARM_HI_OFFSET)

#if (HRT_TIMER_CLOCK % 1000000) != 0
# error HRT_TIMER_CLOCK must be a multiple of 1MHz
#endif
#if HRT_TIMER_CLOCK <= 1000000
# error HRT_TIMER_CLOCK must be greater than 1MHz
#endif

#define HRT_INTERVAL_MIN                  50
#define HRT_INTERVAL_MAX                  50000
#define HRT_COUNTER_PERIOD                18446744073709551615ULL
#define HRT_COUNTER_SCALE(_c)             (_c)

static struct sq_queue_s callout_queue;

static uint64_t latency_baseline;
static uint64_t latency_actual;

const uint16_t latency_bucket_count = LATENCY_BUCKET_COUNT;
const uint16_t latency_buckets[LATENCY_BUCKET_COUNT] = { 1, 2, 5, 10, 20, 50, 100, 1000 };
__EXPORT uint32_t latency_counters[LATENCY_BUCKET_COUNT + 1];

static void hrt_tim_init(void);
static int hrt_tim_isr(int irq, void *context, void *arg);
static void hrt_latency_update(void);
static void hrt_tim_modifyreg32(uint32_t base, uint32_t offset, uint32_t clearbits, uint32_t setbits);
static void hrt_tim_putreg(uint32_t base, uint32_t offset, uint32_t value);
static uint32_t hrt_tim_getreg(uint32_t base, uint32_t offset);

static void hrt_call_internal(struct hrt_call *entry,
			      hrt_abstime deadline,
			      hrt_abstime interval,
			      hrt_callout callout,
			      void *arg);
static void hrt_call_enter(struct hrt_call *entry);
static void hrt_call_reschedule(void);
static void hrt_call_invoke(void);

int hrt_ioctl(unsigned int cmd, unsigned long arg);

static void hrt_tim_modifyreg32(uint32_t base, uint32_t offset, uint32_t clearbits, uint32_t setbits)
{
	modifyreg32(base + offset, clearbits, setbits);
}

static void hrt_tim_putreg(uint32_t base, uint32_t offset, uint32_t value)
{
	putreg32(value, base + offset);
}

static uint32_t hrt_tim_getreg(uint32_t base, uint32_t offset)
{
	return getreg32(base + offset);
}

static void
hrt_tim_init(void)
{
	uint32_t mask = ((uint32_t)(HRT_TIMER_CLOCK / 1000000) - 1) << HRT_DIVIDER_S;

	hrt_tim_modifyreg32(HRT_TIM_BASE, HRT_CONFIG_OFFSET, HRT_DIVIDER_M, mask);
	hrt_tim_modifyreg32(HRT_TIM_BASE, HRT_CONFIG_OFFSET, 0, HRT_INCREASE);

	hrt_tim_putreg(HRT_TIM_BASE, HRT_LOAD_LO_OFFSET, 0);
	hrt_tim_putreg(HRT_TIM_BASE, HRT_LOAD_HI_OFFSET, 0);
	hrt_tim_putreg(HRT_TIM_BASE, HRT_LOAD_OFFSET, 1 << 0);

	hrt_tim_putreg(HRT_TIM_BASE, HRT_LOAD_LO_OFFSET, 0);
	hrt_tim_putreg(HRT_TIM_BASE, HRT_LOAD_HI_OFFSET, 0);
	hrt_tim_putreg(HRT_TIM_BASE, HRT_LOAD_OFFSET, 1 << 0);

	uint64_t val = 1000;
	hrt_tim_putreg(HRT_TIM_BASE, HRT_ALARM_LO_OFFSET, (uint32_t)(val & 0xffffffff));
	hrt_tim_putreg(HRT_TIM_BASE, HRT_ALARM_HI_OFFSET, (uint32_t)((val >> 32) & 0xffffffff));

	hrt_tim_modifyreg32(HRT_TIM_BASE, HRT_CONFIG_OFFSET, 0, HRT_ALARM_EN);
	hrt_tim_modifyreg32(HRT_TIM_BASE, HRT_CONFIG_OFFSET, HRT_AUTORELOAD, 0);

	esp32_setup_irq(0, HRT_TIMER_PERIPH, HRT_TIMER_PRIO, ESP32_CPUINT_LEVEL);
	irq_attach(HRT_TIMER_VECTOR, hrt_tim_isr, NULL);
	up_enable_irq(HRT_TIMER_VECTOR);

	hrt_tim_modifyreg32(HRT_TG_BASE, HRT_TIMER_INT_ENA_OFFSET, 0, HRT_TIMER_INT_ENA);
	hrt_tim_modifyreg32(HRT_TIM_BASE, HRT_CONFIG_OFFSET, 0, HRT_TIMER_EN);
}

static int __attribute__((section(".iram1")))
hrt_tim_isr(int irq, void *context, void *arg)
{
	uint32_t value_32;

	latency_actual = 0;
	hrt_tim_putreg(HRT_TIM_BASE, HRT_UPDATE_OFFSET, 1 << 0);
	value_32 = hrt_tim_getreg(HRT_TIM_BASE, HRT_HI_OFFSET);
	latency_actual |= (uint64_t)value_32;
	latency_actual <<= 32;
	value_32 = hrt_tim_getreg(HRT_TIM_BASE, HRT_LO_OFFSET);
	latency_actual |= (uint64_t)value_32;

	hrt_latency_update();
	hrt_call_invoke();
	hrt_call_reschedule();

	hrt_tim_putreg(HRT_TG_BASE, HRT_TIMER_CLR_OFFSET, HRT_TIMER_INT_CLR);
	hrt_tim_modifyreg32(HRT_TIM_BASE, HRT_CONFIG_OFFSET, 0, HRT_ALARM_EN);

	return OK;
}

hrt_abstime __attribute__((section(".iram1")))
hrt_absolute_time(void)
{
	hrt_abstime abstime;
	irqstate_t flags;

	flags = px4_enter_critical_section();
	rUPDATE = 1;
	abstime = (hrt_abstime)(((uint64_t)rHI << 32) | (uint64_t)rLO);
	px4_leave_critical_section(flags);

	return abstime;
}

void
hrt_store_absolute_time(volatile hrt_abstime *t)
{
	irqstate_t flags = px4_enter_critical_section();
	*t = hrt_absolute_time();
	px4_leave_critical_section(flags);
}

void
hrt_init(void)
{
	sq_init(&callout_queue);
	hrt_tim_init();
}

void __attribute__((section(".iram1")))
hrt_call_after(struct hrt_call *entry, hrt_abstime delay, hrt_callout callout, void *arg)
{
	hrt_call_internal(entry, hrt_absolute_time() + delay, 0, callout, arg);
}

void __attribute__((section(".iram1")))
hrt_call_at(struct hrt_call *entry, hrt_abstime calltime, hrt_callout callout, void *arg)
{
	hrt_call_internal(entry, calltime, 0, callout, arg);
}

void __attribute__((section(".iram1")))
hrt_call_every(struct hrt_call *entry, hrt_abstime delay, hrt_abstime interval, hrt_callout callout, void *arg)
{
	hrt_call_internal(entry, hrt_absolute_time() + delay, interval, callout, arg);
}

static void __attribute__((section(".iram1")))
hrt_call_internal(struct hrt_call *entry, hrt_abstime deadline, hrt_abstime interval, hrt_callout callout, void *arg)
{
	irqstate_t flags = px4_enter_critical_section();

	if (entry->deadline != 0) {
		sq_rem(&entry->link, &callout_queue);
	}

	entry->deadline = deadline;
	entry->period = interval;
	entry->callout = callout;
	entry->arg = arg;

	hrt_call_enter(entry);

	px4_leave_critical_section(flags);
}

bool __attribute__((section(".iram1")))
hrt_called(struct hrt_call *entry)
{
	return (entry->deadline == 0);
}

void __attribute__((section(".iram1")))
hrt_cancel(struct hrt_call *entry)
{
	irqstate_t flags = px4_enter_critical_section();

	sq_rem(&entry->link, &callout_queue);
	entry->deadline = 0;
	entry->period = 0;

	px4_leave_critical_section(flags);
}

static void __attribute__((section(".iram1")))
hrt_call_enter(struct hrt_call *entry)
{
	struct hrt_call *call, *next;

	call = (struct hrt_call *)sq_peek(&callout_queue);

	if ((call == NULL) || (entry->deadline < call->deadline)) {
		sq_addfirst(&entry->link, &callout_queue);
		hrt_call_reschedule();

	} else {
		do {
			next = (struct hrt_call *)sq_next(&call->link);

			if ((next == NULL) || (entry->deadline < next->deadline)) {
				sq_addafter(&call->link, &entry->link, &callout_queue);
				break;
			}
		} while ((call = next) != NULL);
	}
}

static void __attribute__((section(".iram1")))
hrt_call_invoke(void)
{
	struct hrt_call *call;
	hrt_abstime deadline;

	while (true) {
		hrt_abstime now = hrt_absolute_time();

		call = (struct hrt_call *)sq_peek(&callout_queue);

		if (call == NULL) {
			break;
		}

		if (call->deadline > now) {
			break;
		}

		sq_rem(&call->link, &callout_queue);
		deadline = call->deadline;
		call->deadline = 0;

		if (call->callout) {
			call->callout(call->arg);
		}

		if (call->period != 0) {
			if (call->deadline <= now) {
				call->deadline = deadline + call->period;
			}

			hrt_call_enter(call);
		}
	}
}

static void __attribute__((section(".iram1")))
hrt_call_reschedule()
{
	hrt_abstime now = hrt_absolute_time();
	struct hrt_call *next = (struct hrt_call *)sq_peek(&callout_queue);
	hrt_abstime deadline = now + HRT_INTERVAL_MAX;

	if (next != NULL) {
		if (next->deadline <= (now + HRT_INTERVAL_MIN)) {
			deadline = now + HRT_INTERVAL_MIN;

		} else if (next->deadline < deadline) {
			deadline = next->deadline;
		}
	}

	latency_baseline = deadline & 0xffff;

	rALARMLO = (uint32_t)(deadline & 0xffffffff);
	rALARMHI = (uint32_t)((deadline >> 32) & 0xffffffff);
}

static void
hrt_latency_update(void)
{
	uint16_t latency = latency_actual - latency_baseline;
	unsigned index;

	for (index = 0; index < LATENCY_BUCKET_COUNT; index++) {
		if (latency <= latency_buckets[index]) {
			latency_counters[index]++;
			return;
		}
	}

	latency_counters[index]++;
}

void __attribute__((section(".iram1")))
hrt_call_init(struct hrt_call *entry)
{
	memset(entry, 0, sizeof(*entry));
}

void __attribute__((section(".iram1")))
hrt_call_delay(struct hrt_call *entry, hrt_abstime delay)
{
	entry->deadline = hrt_absolute_time() + delay;
}

#endif /* HRT_TIMER */
