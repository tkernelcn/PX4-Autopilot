#pragma once

#include <esp32s3_irq.h>
#include <arch/esp32s3/irq.h>

#define esp32_setup_irq esp32s3_setup_irq
#define esp32_teardown_irq esp32s3_teardown_irq
#define ESP32_CPUINT_LEVEL ESP32S3_CPUINT_LEVEL
#define ESP32_PIN2IRQ ESP32S3_PIN2IRQ
