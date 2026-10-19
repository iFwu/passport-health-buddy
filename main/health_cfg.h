#pragma once

#include "esp_err.h"
#include "health_log.h"

#include <stdbool.h>
#include <stddef.h>

#define HEALTH_SSID_MAX 32
#define HEALTH_PASS_MAX 64
#define HEALTH_KEY_MAX 180

// 读写 NVS 命名空间 health。不在日志里输出密码或 API Key。
esp_err_t health_cfg_init(void);
bool health_cfg_load_net(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap,
                         char *key, size_t key_cap);
esp_err_t health_cfg_save_net(const char *ssid, const char *pass, const char *key);
bool health_cfg_load_log(health_persist_t *log);
esp_err_t health_cfg_save_log(const health_persist_t *log);
