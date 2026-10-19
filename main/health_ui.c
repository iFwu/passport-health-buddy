#include "health_ui.h"
#include "health_exercise.h"
#include "plank_audio.h"
#include "ui_pixel.h"
#include "health_motion.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(plank_zh_16);

#define UI_WATER 0x3498DB
#define UI_WATER_HIGHLIGHT 0x85C1E9

static health_ui_mode_t s_mode = HEALTH_UI_EVERYDAY;
static health_ui_mode_t s_help_return = HEALTH_UI_EVERYDAY;
static lv_obj_t *s_screen;
static lv_obj_t *s_plank;

static lv_obj_t *s_tabs[4];
static lv_obj_t *s_tab_names[4];
static lv_obj_t *s_battery;
static lv_obj_t *s_title;

static lv_obj_t *s_card;
static lv_obj_t *s_help_card;
static lv_obj_t *s_activity_progress;
static health_motion_t s_motion;
static lv_obj_t *s_mute;
static lv_obj_t *s_cup_wrap;
static lv_obj_t *s_cup_body;
static lv_obj_t *s_water_fill;
static lv_obj_t *s_water_surface;

static lv_obj_t *s_big;
static lv_obj_t *s_big_unit;
static lv_obj_t *s_line1;
static lv_obj_t *s_line1_sub;
static lv_obj_t *s_line2;
static lv_obj_t *s_line3;
static lv_obj_t *s_alert_box;
static lv_obj_t *s_alert;

static lv_obj_t *s_voice_bar;
static lv_obj_t *s_voice;

static lv_obj_t *s_keys1;
static lv_obj_t *s_volume_box;
static lv_obj_t *s_volume_label;
static unsigned s_volume;
static uint64_t s_volume_until_ms;
static uint64_t s_notice_until_ms;
static char s_notice[96];

static char s_portal_ssid[33];
static char s_portal_pass[16];

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h,
                     uint32_t bg_color, int radius, int border_w, uint32_t border_color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border_color), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w,
                       const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "");
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_width(lbl, w);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(lbl, align, 0);
    return lbl;
}

static void place(lv_obj_t *obj, int x, int y, int w)
{
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
}

static void show(health_ui_mode_t mode)
{
    if (mode == HEALTH_UI_HELP && s_mode >= HEALTH_UI_EVERYDAY && s_mode <= HEALTH_UI_STRETCH)
        s_help_return = s_mode;
    s_mode = mode;
    if (mode == HEALTH_UI_HOME) {
        if (s_plank) lv_screen_load(s_plank);
    } else {
        if (s_screen) lv_screen_load(s_screen);
    }
}

void health_ui_create(lv_obj_t *plank_screen)
{
    s_plank = plank_screen;
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(UI_SKY), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    const int tab_xs[] = { 8, 70, 132, 194 };
    const int tab_widths[] = { 56, 56, 56, 38 };
    const char *tab_titles[] = { "办公", "健身", "居家", "?" };

    for (int i = 0; i < 4; i++) {
        s_tabs[i] = box(s_screen, tab_xs[i], 6, tab_widths[i], 32, UI_PAPER, 8, 2, UI_INK);
        s_tab_names[i] = label(s_tabs[i], 0, 6, tab_widths[i] - 4,
                               &plank_zh_16, UI_INK, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(s_tab_names[i], tab_titles[i]);
    }

    s_card = box(s_screen, 8, 44, 224, 200, UI_PAPER, 8, 3, UI_INK);
    s_help_card = box(s_screen, 8, 44, 224, 200, UI_PAPER, 8, 3, UI_INK);
    const char *help_lines[] = {
        "短按上/下:调节音量", "长按上/下:前页/后页",
        "短按确定:当前页操作", "长按确定:说话", "松开确定:发送",
    };
    for (unsigned i = 0; i < sizeof(help_lines) / sizeof(help_lines[0]); i++) {
        lv_obj_t *line = label(s_help_card, 14, 46 + (int)i * 26, 194,
                               &plank_zh_16, UI_INK, LV_TEXT_ALIGN_LEFT);
        lv_label_set_text(line, help_lines[i]);
    }
    lv_obj_add_flag(s_help_card, LV_OBJ_FLAG_HIDDEN);
    health_motion_create(&s_motion, s_card, 61, 40);
    s_activity_progress = lv_bar_create(s_card);
    lv_obj_set_pos(s_activity_progress, 24, 168);
    lv_obj_set_size(s_activity_progress, 170, 4);
    lv_obj_set_style_bg_color(s_activity_progress, lv_color_hex(0xD9E7EC), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_activity_progress, lv_color_hex(UI_GRASS), LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(s_activity_progress, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_motion.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_activity_progress, LV_OBJ_FLAG_HIDDEN);

    s_title = label(s_screen, 22, 55, 104, &plank_zh_16, UI_INK, LV_TEXT_ALIGN_LEFT);
    s_mute = label(s_screen, 130, 55, 44, &plank_zh_16, UI_INK, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(s_mute, "静音");
    lv_obj_add_flag(s_mute, LV_OBJ_FLAG_HIDDEN);
    s_battery = label(s_screen, 179, 56, 42, &lv_font_montserrat_14, UI_INK, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(s_battery, "--%");
    block(s_screen, 20, 80, 200, 2, 0xD9E7EC);
    s_cup_wrap = lv_obj_create(s_card);
    lv_obj_remove_flag(s_cup_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_cup_wrap, 14, 54);
    lv_obj_set_size(s_cup_wrap, 74, 84);
    lv_obj_set_style_border_width(s_cup_wrap, 0, 0);
    lv_obj_set_style_pad_all(s_cup_wrap, 0, 0);
    lv_obj_set_style_shadow_width(s_cup_wrap, 0, 0);
    lv_obj_set_style_bg_opa(s_cup_wrap, LV_OPA_TRANSP, 0);

    // Mug handle on the right
    box(s_cup_wrap, 50, 26, 16, 34, UI_INK, 8, 0, 0);
    box(s_cup_wrap, 50, 30, 12, 26, UI_PAPER, 5, 0, 0);

    // Mug outer body (rounded bottom + flat top + rim)
    box(s_cup_wrap, 6, 14, 48, 66, UI_INK, 12, 0, 0);
    block(s_cup_wrap, 6, 14, 48, 16, UI_INK);
    box(s_cup_wrap, 3, 12, 54, 4, UI_INK, 2, 0, 0);

    // Mug white interior (rounded bottom + flat top)
    box(s_cup_wrap, 9, 17, 42, 60, 0xFFFFFF, 9, 0, 0);
    block(s_cup_wrap, 9, 17, 42, 14, 0xFFFFFF);

    // Water fill container anchored inside the mug interior
    s_cup_body = lv_obj_create(s_cup_wrap);
    lv_obj_remove_flag(s_cup_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_cup_body, 9, 21);
    lv_obj_set_size(s_cup_body, 42, 54);
    lv_obj_set_style_radius(s_cup_body, 6, 0);
    lv_obj_set_style_border_width(s_cup_body, 0, 0);
    lv_obj_set_style_pad_all(s_cup_body, 0, 0);
    lv_obj_set_style_shadow_width(s_cup_body, 0, 0);
    lv_obj_set_style_bg_opa(s_cup_body, LV_OPA_TRANSP, 0);

    s_water_fill = box(s_cup_body, 0, 54, 42, 0, UI_WATER, 6, 0, 0);
    s_water_surface = block(s_water_fill, 0, 0, 42, 3, UI_WATER_HIGHLIGHT);
    (void)s_water_surface;

    s_big = label(s_card, 14, 44, 120, &lv_font_montserrat_32, UI_INK, LV_TEXT_ALIGN_LEFT);
    s_big_unit = label(s_card, 60, 59, 48, &plank_zh_16, UI_INK, LV_TEXT_ALIGN_LEFT);

    s_line1 = label(s_card, 14, 92, 196, &plank_zh_16, UI_SKY_DARK, LV_TEXT_ALIGN_LEFT);
    s_line1_sub = label(s_card, 14, 112, 196, &lv_font_montserrat_14, UI_INK, LV_TEXT_ALIGN_LEFT);
    s_line2 = label(s_card, 14, 132, 196, &plank_zh_16, UI_INK, LV_TEXT_ALIGN_LEFT);
    s_line3 = label(s_card, 104, 56, 102, &plank_zh_16, UI_INK, LV_TEXT_ALIGN_RIGHT);

    s_alert_box = box(s_card, 14, 156, 190, 28, UI_GRASS, 6, 2, UI_INK);
    s_alert = label(s_alert_box, 0, 3, 186, &plank_zh_16, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);

    s_voice_bar = box(s_screen, 8, 250, 224, 32, 0xD9E7EC, 6, 2, UI_INK);
    s_voice = label(s_voice_bar, 4, 5, 212, &plank_zh_16, UI_INK, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *footer = box(s_screen, 0, 292, 240, 28, UI_INK, 0, 0, 0);
    s_keys1 = label(footer, 4, 5, 232, &plank_zh_16, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);

    s_volume_box = box(s_screen, 40, 94, 160, 40, UI_INK, 8, 0, 0);
    s_volume_label = label(s_volume_box, 4, 10, 152, &plank_zh_16,
                           0xFFFFFF, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(s_volume_box, LV_OBJ_FLAG_HIDDEN);

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

void health_ui_set_mode(health_ui_mode_t mode)
{
    show(mode);
}

unsigned health_ui_action(health_action_t action, health_log_t *log,
                          plank_model_t *model, uint64_t now_ms)
{
    if (action == HEALTH_ACTION_PREVIOUS_MODE || action == HEALTH_ACTION_NEXT_MODE) {
        show(health_activity_next(s_mode, action == HEALTH_ACTION_NEXT_MODE));
        return PLANK_EFFECT_NONE;
    }
    if (s_mode == HEALTH_UI_PORTAL) return PLANK_EFFECT_NONE;
    if (action != HEALTH_ACTION_CONFIRM) return PLANK_EFFECT_NONE;
    if (s_mode == HEALTH_UI_HELP) {
        show(s_help_return);
        return PLANK_EFFECT_NONE;
    }
    if (s_mode == HEALTH_UI_EVERYDAY) {
        return log && health_log_add_water(log, 250, now_ms) ? HEALTH_SOUND_OK : PLANK_EFFECT_NONE;
    }
    if (s_mode == HEALTH_UI_GYM && log) {
        if (health_log_rest_left_s(log, now_ms)) {
            log->rest_until_ms = 0;
            log->timer_running = false;
            return HEALTH_SOUND_RELAX;
        }
        health_log_rest(log, HEALTH_REST_DEFAULT_S, now_ms);
    } else if (s_mode >= HEALTH_UI_HOME && s_mode <= HEALTH_UI_STRETCH) {
        health_ui_mode_t target = s_mode;
        unsigned effect = health_activity_confirm(&target, log, model, now_ms);
        show(target);
        if (effect == HEALTH_SOUND_ERROR) health_ui_notice("请先结束当前训练", now_ms);
        return effect;
    }
    return PLANK_EFFECT_NONE;
}

void health_ui_set_volume(unsigned percent, uint64_t now_ms)
{
    s_volume = percent > 100 ? 100 : percent;
    s_volume_until_ms = now_ms + 1500;
}

void health_ui_notice(const char *text, uint64_t now_ms)
{
    size_t n = text ? strlen(text) : 0;
    if (n >= sizeof(s_notice)) {
        n = sizeof(s_notice) - 1;
        while (n && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
    }
    if (n) memcpy(s_notice, text, n);
    s_notice[n] = '\0';
    s_notice_until_ms = now_ms + 3000;
}

static const char *voice_feedback(health_voice_state_t voice, uint64_t now_ms, uint32_t *bg)
{
    *bg = 0xD9E7EC;
    switch (voice) {
    case HEALTH_VOICE_RECORDING:
        *bg = UI_YELLOW;
        return "正在倾听，松手发送";
    case HEALTH_VOICE_THINKING:
        *bg = UI_ORANGE;
        return "正在思考...";
    case HEALTH_VOICE_SPEAKING:
        *bg = UI_GRASS;
        return "正在语音播报...";
    default:
        break;
    }
    if (s_notice[0] && now_ms < s_notice_until_ms) {
        *bg = UI_YELLOW;
        return s_notice;
    }
    if (voice == HEALTH_VOICE_CONNECTING) return "正在连接语音服务...";
    if (voice == HEALTH_VOICE_OFF || voice == HEALTH_VOICE_ERROR) return "语音离线，按键可用";
    return "语音就绪";
}

void health_ui_refresh(const health_log_t *log, const plank_model_t *model,
                       uint64_t now_ms, int battery, const char *status,
                       bool audio_ok, bool buttons_ok, health_voice_state_t voice)
{
    uint32_t voice_bg;
    const char *voice_text = voice_feedback(voice, now_ms, &voice_bg);
    bool volume_visible = now_ms < s_volume_until_ms;
    bool voice_plan = log && log->interval_activity == HEALTH_ACTIVITY_VOICE &&
                      (log->interval_on || log->interval_finished);
    if (s_mode == HEALTH_UI_HOME && !voice_plan) {
        if (s_plank && lv_screen_active() != s_plank) lv_screen_load(s_plank);
        if (model) plank_ui_refresh(model, now_ms, battery, audio_ok, buttons_ok,
                                    voice_text, voice_bg, s_volume, volume_visible,
                                    log && log->interval_on ? health_activity_get(log->interval_activity)->name : NULL);
        return;
    }
    if (lv_screen_active() != s_screen) lv_screen_load(s_screen);
    if (volume_visible) {
        if (s_volume) lv_label_set_text_fmt(s_volume_label, "音量 %u%%", s_volume);
        else lv_label_set_text(s_volume_label, "已静音");
        lv_obj_clear_flag(s_volume_box, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(s_volume_box, LV_OBJ_FLAG_HIDDEN);

    if (battery < 0) lv_label_set_text(s_battery, "--%");
    else lv_label_set_text_fmt(s_battery, "%d%%", battery);
    if (s_volume == 0) lv_obj_clear_flag(s_mute, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_mute, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_voice_bar, lv_color_hex(voice_bg), 0);
    lv_label_set_text(s_voice, voice_text);

    int active_tab = (s_mode == HEALTH_UI_EVERYDAY) ? 0 :
                     (s_mode == HEALTH_UI_GYM) ? 1 : (s_mode == HEALTH_UI_HELP) ? 3 : 2;
    for (int i = 0; i < 4; i++) {
        uint32_t c = (i == active_tab) ? UI_YELLOW : UI_PAPER;
        lv_obj_set_style_bg_color(s_tabs[i], lv_color_hex(c), 0);
    }
    if (s_mode == HEALTH_UI_HELP) {
        lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_help_card, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_title, "按键帮助");
        lv_label_set_text(s_keys1, "确定返回");
        return;
    }
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_help_card, LV_OBJ_FLAG_HIDDEN);
    if (s_mode >= HEALTH_UI_HOME && s_mode <= HEALTH_UI_STRETCH) {
        lv_obj_add_flag(s_cup_wrap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_big_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_line1_sub, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_alert_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_motion.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_activity_progress, LV_OBJ_FLAG_HIDDEN);
        place(s_line1, 10, 86, 198);
        place(s_big, 10, 106, 198);
        place(s_line2, 10, 146, 198);
        place(s_line3, 10, 175, 198);
        lv_obj_set_style_text_align(s_line1, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(s_big, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(s_line2, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(s_line3, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(s_line3, lv_color_hex(UI_SKY_DARK), 0);
        health_activity_id_t id = (health_activity_id_t)(s_mode - HEALTH_UI_HOME);
        const health_activity_def_t *def = health_activity_get(id);
        bool owns = log && log->interval_activity == id;
        bool active = owns && log->interval_on;
        bool finished = owns && log->interval_finished;
        bool conflict = (log && log->interval_on && !owns) || health_activity_plank_busy(model);
        unsigned work = owns && (active || finished) ? log->interval_work_s : def->work_s;
        unsigned rest = owns && (active || finished) ? log->interval_rest_s : def->rest_s;
        unsigned rounds = owns && (active || finished) ? log->interval_rounds : def->rounds;
        unsigned remaining = active && log->interval_until_ms > now_ms ?
                             (unsigned)((log->interval_until_ms - now_ms + 999) / 1000) :
                             finished ? 0 : work;
        lv_label_set_text(s_title, def->name);
        health_motion_show(&s_motion, id ? (unsigned)id : 4,
                           active && log->interval_work, now_ms);
        if (conflict) {
            const char *running = health_activity_plank_busy(model) ? "平板支撑" :
                                  health_activity_get(log->interval_activity)->name;
            lv_label_set_text_fmt(s_line1, "%s进行中", running);
        } else lv_label_set_text(s_line1, active ? (log->interval_work ? "运动中" : "休息中") :
                                finished ? "训练完成" : def->hint);
        lv_label_set_text_fmt(s_big, "%02u:%02u", remaining / 60, remaining % 60);
        if (active || finished)
            lv_label_set_text_fmt(s_line2, "%s %u/%u 轮", finished ? "完成" : "当前",
                                  (unsigned)log->interval_round, rounds);
        else lv_label_set_text_fmt(s_line2, "%u秒动 / %u秒休 / %u轮", work, rest, rounds);
        unsigned total = (work + rest) * rounds;
        unsigned elapsed = finished ? total : 0;
        if (active) elapsed = (log->interval_round - 1) * (work + rest) +
                              (log->interval_work ? work : work + rest) - remaining;
        lv_bar_set_value(s_activity_progress, total ? (int)(elapsed * 100 / total) : 0, LV_ANIM_OFF);
        if (id) lv_label_set_text_fmt(s_line3, "%u/4 长按上/下切换", (unsigned)id + 1);
        else lv_label_set_text(s_line3, "语音计划 长按上/下切换");
        lv_label_set_text(s_keys1, !buttons_ok ? "按键不可用，请重新开机" : conflict ?
                          "确定:查看进行中训练" : active ? "确定:结束训练" :
                          finished && id == HEALTH_ACTIVITY_VOICE ? "确定:返回平板" : "确定:开始");
        return;
    }

    const char *title = "";
    char big_str[32] = "";
    char unit_str[16] = "";
    char line1_str[64] = "";
    char line1_sub_str[32] = "";
    char line2_str[64] = "";
    char line3_str[64] = "";
    char alert_str[32] = "";
    uint32_t alert_bg = UI_GRASS;
    uint32_t alert_fg = 0xFFFFFF;
    char k1_str[64] = "";

    lv_obj_add_flag(s_cup_wrap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_motion.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_activity_progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_big_unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_line1_sub, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_alert_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_align(s_line1, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_big, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_line2, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(s_line3, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_line3, lv_color_hex(UI_INK), 0);
    place(s_line3, 104, 56, 102);
    lv_obj_set_pos(s_big, 14, 44);
    place(s_line1, 14, 92, 196);
    place(s_line1_sub, 14, 112, 196);
    place(s_line2, 14, 132, 196);

    if (s_mode == HEALTH_UI_PORTAL) {
        title = "配网";
        snprintf(big_str, sizeof(big_str), "%s", s_portal_pass[0] ? s_portal_pass : "----");
        snprintf(line1_str, sizeof(line1_str), "热点 %s", s_portal_ssid);
        snprintf(line2_str, sizeof(line2_str), "%s", status && status[0] ? status : "浏览器打开 192.168.4.1");
        snprintf(alert_str, sizeof(alert_str), "手机连接热点配网");
        alert_bg = UI_YELLOW;
        alert_fg = UI_INK;
        snprintf(k1_str, sizeof(k1_str), "请用手机连接热点填写配置");
    } else if (s_mode == HEALTH_UI_EVERYDAY) {
        title = "今日饮水";
        lv_obj_clear_flag(s_cup_wrap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_big, 98, 44);
        place(s_line1_sub, 98, 84, 112);
        place(s_line1, 98, 106, 112);
        place(s_line2, 98, 128, 112);

        uint32_t ml = log ? log->saved.water_ml : 0;
        uint32_t goal = (log && log->saved.goal_ml) ? log->saved.goal_ml : 2000;
        snprintf(big_str, sizeof(big_str), "%u", (unsigned)ml);
        snprintf(unit_str, sizeof(unit_str), "ml");
        snprintf(line1_sub_str, sizeof(line1_sub_str), "/ %u ml", (unsigned)goal);

        int fill_h = (int)((ml >= goal ? goal : ml) * 54 / goal);
        if (ml > 0 && fill_h < 8) fill_h = 8;
        lv_obj_set_size(s_water_fill, 42, fill_h);
        lv_obj_set_pos(s_water_fill, 0, 54 - fill_h);

        snprintf(line1_str, sizeof(line1_str), "已喝 %u 杯", (unsigned)(ml / 250));
        uint64_t activity_at = log ? log->last_stand_ms + HEALTH_SIT_MS : now_ms + HEALTH_SIT_MS;
        if (log && log->snooze_until_ms > activity_at) activity_at = log->snooze_until_ms;
        unsigned activity_min = activity_at > now_ms ? (unsigned)((activity_at - now_ms + 59999) / 60000) : 0;
        snprintf(line2_str, sizeof(line2_str), "活动 %u 分钟后", activity_min);
        if (ml >= goal) snprintf(alert_str, sizeof(alert_str), "今日已达标");
        else snprintf(alert_str, sizeof(alert_str), "还差 %u ml", (unsigned)(goal - ml));
        snprintf(k1_str, sizeof(k1_str), "确定:记水 +250ml");
    } else if (s_mode == HEALTH_UI_GYM) {
        title = "力量训练";
        unsigned rest_s = health_log_rest_left_s(log, now_ms);
        if (rest_s > 0) {
            snprintf(big_str, sizeof(big_str), "%u", rest_s);
            snprintf(unit_str, sizeof(unit_str), "秒");
            snprintf(alert_str, sizeof(alert_str), "组间休息 放松呼吸");
            alert_bg = UI_ORANGE;
            alert_fg = UI_INK;
        } else {
            snprintf(big_str, sizeof(big_str), "%u", (unsigned)health_log_total_sets(log));
            snprintf(unit_str, sizeof(unit_str), "组");
            snprintf(alert_str, sizeof(alert_str), "准备下一组");
        }

        if (log && log->saved.entry_count > 0) {
            const health_entry_t *last = &log->saved.entries[log->saved.entry_count - 1];
            if (last->exercise < HEALTH_EX_KNOWN_COUNT) {
                const health_exercise_def_t *def = health_exercise_get(last->exercise);
                snprintf(line1_str, sizeof(line1_str), "最近: %s", def->name_zh);
                snprintf(line1_sub_str, sizeof(line1_sub_str), "%s", def->name_en);
            } else {
                snprintf(line1_str, sizeof(line1_str), "最近: %s", health_category_name_zh(last->category));
                snprintf(line1_sub_str, sizeof(line1_sub_str), "%s", last->name_en);
            }
            if (last->weight_x10 == 0) {
                snprintf(line2_str, sizeof(line2_str), "自重 x %u 次 x %u 组",
                         (unsigned)last->reps, (unsigned)last->sets);
            } else {
                snprintf(line2_str, sizeof(line2_str), "%u.%u kg x %u 次 x %u 组",
                         (unsigned)(last->weight_x10 / 10), (unsigned)(last->weight_x10 % 10),
                         (unsigned)last->reps, (unsigned)last->sets);
            }
            snprintf(line3_str, sizeof(line3_str), "总量 %u kg",
                     (unsigned)health_log_total_volume_kg(log));
        } else {
            snprintf(line1_str, sizeof(line1_str), "暂无训练记录");
            snprintf(line2_str, sizeof(line2_str), "长按确定说出动作");
            place(s_line2, 14, 116, 196);
        }

        snprintf(k1_str, sizeof(k1_str), "确定:%s休息", rest_s ? "结束" : "开始");
    }

    lv_label_set_text(s_title, title);
    lv_label_set_text(s_big, big_str);
    lv_obj_set_width(s_big, LV_SIZE_CONTENT);
    lv_obj_update_layout(s_big);
    lv_label_set_text(s_big_unit, unit_str);
    lv_obj_set_width(s_big_unit, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_big_unit, lv_obj_get_x(s_big) + lv_obj_get_width(s_big) + 6,
                   lv_obj_get_y(s_big) + 15);
    lv_label_set_text(s_line1, line1_str);
    lv_label_set_text(s_line1_sub, line1_sub_str);
    lv_label_set_text(s_line2, line2_str);
    lv_label_set_text(s_line3, line3_str);

    lv_obj_set_style_bg_color(s_alert_box, lv_color_hex(alert_bg), 0);
    lv_obj_set_style_text_color(s_alert, lv_color_hex(alert_fg), 0);
    lv_label_set_text(s_alert, alert_str);

    lv_label_set_text(s_keys1, buttons_ok ? k1_str : "按键不可用，请重新开机");
}
