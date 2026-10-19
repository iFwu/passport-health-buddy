#pragma once

#include "health_log.h"
#include "plank_core.h"

#include "bsp_button.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HEALTH_UI_PORTAL,
    HEALTH_UI_MENU,
    HEALTH_UI_EVERYDAY,
    HEALTH_UI_GYM,
    HEALTH_UI_HOME,
} health_ui_mode_t;

// 调用方持有 LVGL 锁。plank_screen 是已经创建好的平板支撑页面。
void health_ui_create(lv_obj_t *plank_screen);
void health_ui_set_portal(const char *ssid, const char *password, const char *status);

health_ui_mode_t health_ui_mode(void);

// 返回 false 表示这个按下事件要交给平板支撑。
bool health_ui_key(bsp_btn_t button, bsp_btn_ev_t event, health_log_t *log, uint64_t now_ms);

void health_ui_refresh(const health_log_t *log, uint64_t now_ms, int battery, const char *status);
