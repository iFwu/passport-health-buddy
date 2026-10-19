#include "health_ui.h"

#include "step_rx.h"
#include "step_voice.h"

#include <stdio.h>

LV_FONT_DECLARE(plank_zh_16);

static health_ui_mode_t s_mode = HEALTH_UI_EVERYDAY;
static int s_menu;
static lv_obj_t *s_screen;
static lv_obj_t *s_plank;
static lv_obj_t *s_title;
static lv_obj_t *s_card;
static lv_obj_t *s_big;
static lv_obj_t *s_line1;
static lv_obj_t *s_line2;
static lv_obj_t *s_alert;
static lv_obj_t *s_voice;
static lv_obj_t *s_keys;
static lv_obj_t *s_battery;
static lv_obj_t *s_rows[3];
static char s_portal_ssid[33];
static char s_portal_pass[16];

static const char *k_modes[] = { "办公", "健身房", "居家" };

static lv_obj_t *solid(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_hidden(obj, false);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static lv_obj_t *centered_line(lv_obj_t *parent, int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_obj_set_width(label, 200);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

void health_ui_create(lv_obj_t *plank_screen)
{
    s_plank = plank_screen;
    s_screen = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0xD7EBF8), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    lv_obj_t *header = solid(s_screen, 0, 0, 240, 44, 0x1689E8, 0);
    s_title = centered_line(header, 12, &plank_zh_16, 0xFFFFFF);
    s_battery = lv_label_create(header);
    lv_label_set_text(s_battery, "--%");
    lv_obj_set_style_text_font(s_battery, &plank_zh_16, 0);
    lv_obj_set_style_text_color(s_battery, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_battery, LV_ALIGN_RIGHT_MID, -8, 0);

    s_card = solid(s_screen, 12, 56, 216, 196, 0xFFFFFF, 12);
    s_big = centered_line(s_card, 28, &lv_font_montserrat_32, 0x17202A);
    s_line1 = centered_line(s_card, 84, &plank_zh_16, 0x17202A);
    s_line2 = centered_line(s_card, 110, &plank_zh_16, 0x17202A);
    s_alert = centered_line(s_card, 140, &plank_zh_16, 0xE43B2F);
    s_voice = centered_line(s_card, 166, &plank_zh_16, 0x0872C9);

    for (int i = 0; i < 3; i++) {
        s_rows[i] = solid(s_screen, 12, 56 + i * 68, 216, 60, 0xFFFFFF, 12);
        lv_obj_t *name = centered_line(s_rows[i], 20, &plank_zh_16, 0x17202A);
        lv_label_set_text(name, k_modes[i]);
    }

    lv_obj_t *footer = solid(s_screen, 0, 268, 240, 52, 0x17202A, 0);
    s_keys = centered_line(footer, 16, &plank_zh_16, 0xFFFFFF);

    lv_screen_load(s_screen);
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
    if (mode == HEALTH_UI_HOME && s_plank) lv_screen_load(s_plank);
    else lv_screen_load(s_screen);
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

static void show_rows(bool on)
{
    for (int i = 0; i < 3; i++) lv_obj_set_hidden(s_rows[i], !on);
    lv_obj_set_hidden(s_card, on);
}

void health_ui_refresh(const health_log_t *log, uint64_t now_ms, int battery, const char *status)
{
    if (s_mode == HEALTH_UI_HOME) return;
    if (battery < 0) lv_label_set_text(s_battery, "--%");
    else lv_label_set_text_fmt(s_battery, "%d%%", battery);

    char line1[48], line2[48], alert[32];
    line1[0] = line2[0] = alert[0] = '\0';
    const char *title = "健康";
    const char *keys = "";
    const char *big = "";

    if (s_mode == HEALTH_UI_PORTAL) {
        title = "配网";
        big = s_portal_pass[0] ? s_portal_pass : "----";
        snprintf(line1, sizeof(line1), "热点 %s", s_portal_ssid);
        snprintf(line2, sizeof(line2), "%s", status && status[0] ? status : "192.168.4.1");
        keys = "手机打开网页";
        show_rows(false);
    } else if (s_mode == HEALTH_UI_MENU) {
        title = "健康";
        keys = "下 选择  确定 进入";
        show_rows(true);
        for (int i = 0; i < 3; i++) {
            lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(i == s_menu ? 0xFFD928 : 0xFFFFFF), 0);
        }
    } else if (s_mode == HEALTH_UI_EVERYDAY) {
        static char ml[16];
        const unsigned sit = (unsigned)((now_ms - log->last_stand_ms) / 60000);
        title = "办公";
        snprintf(ml, sizeof(ml), "%u", (unsigned)log->saved.water_ml);
        big = ml;
        snprintf(line1, sizeof(line1), "今天喝水");
        snprintf(line2, sizeof(line2), "起身 %u 分钟", sit);
        if (health_log_sitting_due(log, now_ms)) snprintf(alert, sizeof(alert), "该起身");
        else if (health_log_water_due(log, now_ms)) snprintf(alert, sizeof(alert), "该喝水");
        keys = health_log_undo_open(log, now_ms) ? "下 撤销" : "确定 起来  下 一杯";
        show_rows(false);
    } else if (s_mode == HEALTH_UI_GYM) {
        static char rest[16];
        title = "健身房";
        snprintf(rest, sizeof(rest), "%u", health_log_rest_left_s(log, now_ms));
        big = rest;
        if (log->saved.set_count) {
            const health_set_t *set = &log->saved.sets[log->saved.set_count - 1];
            snprintf(line1, sizeof(line1), "最近 %s", set->name);
            snprintf(line2, sizeof(line2), "%u.%u kg x %u",
                     (unsigned)(set->weight_x10 / 10), (unsigned)(set->weight_x10 % 10),
                     (unsigned)set->reps);
        } else {
            snprintf(line1, sizeof(line1), "休息 秒");
            snprintf(line2, sizeof(line2), "按住上键说动作");
        }
        keys = health_log_undo_open(log, now_ms) ? "下 撤销" : "确定 休息  上 说话";
        show_rows(false);
    } else {
        title = log->interval_on ? "跟练" : "居家";
        if (log->interval_on) {
            static char round[16];
            snprintf(round, sizeof(round), "%u", (unsigned)log->interval_round);
            big = round;
            snprintf(line1, sizeof(line1), "第 %u 轮", (unsigned)log->interval_rounds);
            snprintf(line2, sizeof(line2), "%s", log->interval_work ? "运动" : "休息");
        } else {
            big = "GO";
            snprintf(line1, sizeof(line1), "平板支撑");
            snprintf(line2, sizeof(line2), "长按确定返回");
        }
        keys = "长按确定 回菜单";
        show_rows(false);
    }

    lv_label_set_text(s_title, title);
    lv_label_set_text(s_big, big);
    lv_label_set_text(s_line1, line1);
    lv_label_set_text(s_line2, line2);
    lv_label_set_text(s_alert, alert);
    char heard[STEP_TEXT_MAX];
    char voice[STEP_TEXT_MAX + 24];
    step_voice_text(heard, sizeof(heard));
    if (heard[0]) snprintf(voice, sizeof(voice), "%s %s", voice_label(), heard);
    else snprintf(voice, sizeof(voice), "%s", voice_label());
    lv_label_set_text(s_voice, voice);
    lv_label_set_text(s_keys, keys);
}
