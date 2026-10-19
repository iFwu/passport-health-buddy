#pragma once

void trace_stub_log(const char *level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#define ESP_LOGI(tag, ...) trace_stub_log("I", tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) trace_stub_log("E", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) trace_stub_log("W", tag, __VA_ARGS__)
