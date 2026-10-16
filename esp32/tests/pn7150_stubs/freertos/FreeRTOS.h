#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portTICK_PERIOD_MS 10
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / portTICK_PERIOD_MS))
void test_yield_from_isr(void);
#define portYIELD_FROM_ISR() test_yield_from_isr()
