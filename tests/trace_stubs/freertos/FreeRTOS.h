#pragma once

#include <stdint.h>

#define configSUPPORT_STATIC_ALLOCATION 1
#define configUSE_MUTEXES 1

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef struct { unsigned held; } StaticSemaphore_t;
typedef int portMUX_TYPE;

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define portMUX_INITIALIZER_UNLOCKED 0

void trace_stub_enter_critical(void);
void trace_stub_exit_critical(void);
#define portENTER_CRITICAL(mux) ((void)(mux), trace_stub_enter_critical())
#define portEXIT_CRITICAL(mux) ((void)(mux), trace_stub_exit_critical())
#define taskENTER_CRITICAL() trace_stub_enter_critical()
#define taskEXIT_CRITICAL() trace_stub_exit_critical()
