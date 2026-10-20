// Minimal FreeRTOS delay seam; each test supplies its own vTaskDelay().
#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

void vTaskDelay(TickType_t ticks);
