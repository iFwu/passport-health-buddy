#pragma once

#include "esp_err.h"

#include <stdbool.h>

// 初始化 netif、事件循环和 Wi-Fi 驱动。凭据只放在 RAM，不写驱动自己的 NVS。
esp_err_t health_wifi_init(void);

// 连上已有热点。失败返回错误，不打印密码。
esp_err_t health_wifi_connect(const char *ssid, const char *pass, int timeout_ms);

// 开 WPA2 热点，地址 192.168.4.1。可与 STA 同时存在。
esp_err_t health_wifi_start_ap(const char *ssid, const char *pass);

bool health_wifi_connected(void);

// 拿到 IP 后调用。时区固定为 CST-8，对时成功前 health_clock_day() 返回 0。
void health_clock_start(void);
uint32_t health_clock_day(void);
