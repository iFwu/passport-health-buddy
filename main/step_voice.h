#pragma once

#include "health_log.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    STEP_OFF = 0,
    STEP_CONNECTING,
    STEP_READY,
    STEP_RECORDING,
    STEP_THINKING,
    STEP_SPEAKING,
    STEP_ERROR,
} step_state_t;

typedef struct {
    char name[32];
    char call_id[48];
    char arguments[160];
} step_tool_t;

// 直连 StepFun Realtime。api_key 只在本任务内使用，不写日志。
esp_err_t step_voice_start(const char *api_key);

void step_voice_ptt(bool held);
step_state_t step_voice_state(void);
bool step_voice_owns_audio(void);
void step_voice_text(char *out, size_t cap);

// 主循环取出待执行的工具；没有则返回 false。
bool step_voice_take_tool(step_tool_t *tool);
// 主循环执行完后回传结果。等当前 response 结束后才会发给模型。
void step_voice_tool_result(const char *call_id, const char *json);
