#pragma once

#include "esp_err.h"

// 启动热点和配置页。ssid/pass 由调用方生成并显示在屏幕上。
esp_err_t health_portal_start(const char *ap_ssid, const char *ap_pass);

// 当前状态，供界面显示。空闲时返回空串。
const char *health_portal_status(void);
