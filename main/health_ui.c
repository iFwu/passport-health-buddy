#include "health_ui.h"

#include "step_rx.h"
#include "step_voice.h"

#include <stdio.h>

LV_FONT_DECLARE(plank_zh_16);

static health_ui_mode_t s_mode = HEALTH_UI_MENU;
static int s_menu;
static lv_obj_t *s_screen;
static lv_obj_t *s_plank;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_status;
static char s_portal_ssid[33];
static char s_portal_pass[16];

static lv_obj_t *add_label(lv_obj_t *parent, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, 220);
    lv_obj_set_style_text_font(label, &plank_zh_16, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

void health_ui_create(lv_obj_t *plank_screen)
{
    s_plank = plank_screen;
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    s_title = add_label(s_screen, 28);
    s_body = add_label(s_screen, 70);
    s_status = add_label(s_screen, 250);
    lv_scr_load(s_screen);
}

void health_ui_set_portal(const char *ssid, const char *password, const char *status)
{
    snprintf(s_portal_ssid, sizeof(s_portal_ssid), "%s", ssid ? ssid : "");
    snprintf(s_portal_pass, sizeof(s_portal_pass), "%s", password ? password : "");
    s_mode = HEALTH_UI_PORTAL;
    (void)status;
}

health_ui_mode_t health_ui_mode(void)
{
    return s_mode;
}

static void show(health_ui_mode_t mode)
{
    s_mode = mode;
    if (mode == HEALTH_UI_HOME && s_plank) lv_scr_load(s_plank);
    else lv_scr_load(s_screen);
}

bool health_ui_key(bsp_btn_t button, bsp_btn_ev_t event, health_log_t *log, uint64_t now_ms)
{
    if (s_mode == HEALTH_UI_PORTAL) return true;
    if (s_mode != HEALTH_UI_HOME && button == BSP_BTN_UP && event == BSP_BTN_PRESS) {
        step_voice_ptt(true);
        return true;
    }
    if (s_mode != HEALTH_UI_HOME && button == BSP_BTN_UP && event == BSP_BTN_RELEASE) {
        step_voice_ptt(false);
        return true;
    }
    if (button == BSP_BTN_OK && event == BSP_BTN_LONG && s_mode != HEALTH_UI_MENU) {
        show(HEALTH_UI_MENU);
        return true;
    }
    if (s_mode == HEALTH_UI_HOME) return event != BSP_BTN_PRESS;
    if (event != BSP_BTN_CLICK) return true;

    if (s_mode == HEALTH_UI_MENU) {
        if (button == BSP_BTN_UP) s_menu = (s_menu + 2) % 3;
        else if (button == BSP_BTN_DOWN) s_menu = (s_menu + 1) % 3;
        else show((health_ui_mode_t)(HEALTH_UI_EVERYDAY + s_menu));
        return true;
    }
    if (button == BSP_BTN_DOWN && health_log_undo_open(log, now_ms)) {
        health_log_undo(log, now_ms);
        return true;
    }
    if (s_mode == HEALTH_UI_EVERYDAY && button == BSP_BTN_OK) health_log_stand(log, now_ms);
    if (s_mode == HEALTH_UI_EVERYDAY && button == BSP_BTN_DOWN) health_log_add_water(log, 250, now_ms);
    if (s_mode == HEALTH_UI_GYM && button == BSP_BTN_OK) health_log_rest(log, 90, now_ms);
    return true;
}

static const char *voice_label(void)
{
    switch (step_voice_state()) {
    case STEP_CONNECTING: return "语音连接中";
    case STEP_RECORDING: return "正在听";
    case STEP_THINKING: return "正在想";
    case STEP_SPEAKING: return "正在说";
    case STEP_ERROR: return "语音不可用";
    case STEP_READY: return "按住上键说话";
    default: return "";
    }
}

void health_ui_refresh(const health_log_t *log, uint64_t now_ms, int battery, const char *status)
{
    if (s_mode == HEALTH_UI_HOME) return;
    char title[64], body[240];
    const int battery_pct = battery >= 0 ? battery : -1;
    if (s_mode == HEALTH_UI_PORTAL) {
        snprintf(title, sizeof(title), "配网%s", battery_pct >= 0 ? "" : "");
        snprintf(body, sizeof(body), "热点 %s\n密码 %s\n手机打开\n192.168.4.1\n%s",
                 s_portal_ssid, s_portal_pass, status ? status : "");
    } else if (s_mode == HEALTH_UI_MENU) {
        snprintf(title, sizeof(title), "随身健康助手");
        snprintf(body, sizeof(body), "%s 办公\n%s 健身房\n%s 居家\n\n上/下选择  确定进入",
                 s_menu == 0 ? ">" : " ", s_menu == 1 ? ">" : " ", s_menu == 2 ? ">" : " ");
    } else if (s_mode == HEALTH_UI_EVERYDAY) {
        const unsigned sit = (unsigned)((now_ms - log->last_stand_ms) / 60000);
        snprintf(title, sizeof(title), "办公");
        snprintf(body, sizeof(body), "喝水 %u ml\n距离起身 %u 分钟\n%s%s\n确定=我起来了  下=记一杯",
                 (unsigned)log->saved.water_ml, sit,
                 health_log_sitting_due(log, now_ms) ? "该起来走走\n" : "",
                 health_log_water_due(log, now_ms) ? "该喝水了\n" : "");
    } else if (s_mode == HEALTH_UI_GYM) {
        snprintf(title, sizeof(title), "健身房");
        if (log->saved.set_count) {
            const health_set_t *set = &log->saved.sets[log->saved.set_count - 1];
            snprintf(body, sizeof(body), "休息 %u 秒\n最近 %s\n%u.%u kg × %u\n已练 %u 组",
                     health_log_rest_left_s(log, now_ms), set->name,
                     (unsigned)(set->weight_x10 / 10), (unsigned)(set->weight_x10 % 10),
                     (unsigned)set->reps, (unsigned)log->saved.set_count);
        } else {
            snprintf(body, sizeof(body), "休息 %u 秒\n按住上键说动作\n例如：卧推 60 公斤 8 个",
                     health_log_rest_left_s(log, now_ms));
        }
    } else {
        snprintf(title, sizeof(title), log->interval_on ? "跟练" : "居家");
        if (log->interval_on) {
            snprintf(body, sizeof(body), "第 %u / %u 轮\n%s\n剩余见倒计时",
                     (unsigned)log->interval_round, (unsigned)log->interval_rounds,
                     log->interval_work ? "运动" : "休息");
        } else {
            snprintf(body, sizeof(body), "平板支撑\n长按确定返回");
        }
    }
    lv_label_set_text(s_title, title);
    lv_label_set_text(s_body, body);
    char voice[STEP_TEXT_MAX + 32];
    char said[STEP_TEXT_MAX];
    step_voice_text(said, sizeof(said));
    snprintf(voice, sizeof(voice), "%s%s%s", voice_label(), said[0] ? "\n" : "", said);
    lv_label_set_text(s_status, voice);
    (void)battery_pct;
}
