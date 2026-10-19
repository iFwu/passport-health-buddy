#include "plank_ui.h"
#include "ui_pixel.h"
#include "plank_digits.h"
#include "health_motion.h"
#include <stdio.h>
#include <inttypes.h>
LV_FONT_DECLARE(plank_zh_16);

static lv_obj_t *s_time, *s_dot, *s_state, *s_detail, *s_keys_label, *s_battery;
static lv_obj_t *s_minute_unit, *s_voice_bar, *s_voice, *s_volume_box, *s_volume;
static lv_obj_t *s_mute;
static health_motion_t s_motion;
static plank_digits_t s_seconds, s_fraction, s_minutes, s_countdown;

static lv_obj_t *label(lv_obj_t *parent, int y, const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, "");
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(UI_INK), 0);
    lv_obj_set_width(obj, 208);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    return obj;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h,
                      uint32_t color, int radius, int border)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, border, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    return obj;
}

void plank_ui_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_SKY), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    const char *titles[] = { "办公", "健身", "居家", "?" };
    for (unsigned i = 0; i < 4; i++) {
        int width = i == 3 ? 38 : 56;
        lv_obj_t *tab = box(screen, 8 + 62 * (int)i, 6, width, 32,
                            i == 2 ? UI_YELLOW : UI_PAPER, 8, 2);
        lv_obj_t *name = label(tab, 6, &plank_zh_16);
        lv_obj_set_width(name, width - 4);
        lv_label_set_text(name, titles[i]);
    }
    box(screen, 8, 44, 224, 200, UI_PAPER, 8, 3);
    s_battery = label(screen, 56, &lv_font_montserrat_14);
    lv_obj_set_align(s_battery, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_battery, 179, 56);
    lv_obj_set_width(s_battery, 42);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    s_mute = label(screen, 55, &plank_zh_16);
    lv_obj_set_align(s_mute, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_mute, 130, 55);
    lv_obj_set_width(s_mute, 44);
    lv_obj_set_style_text_align(s_mute, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_mute, "静音");
    lv_obj_add_flag(s_mute, LV_OBJ_FLAG_HIDDEN);
    s_state = label(screen, 55, &plank_zh_16);
    lv_obj_set_align(s_state, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_state, 22, 55);
    lv_obj_set_width(s_state, 104);
    lv_obj_set_style_text_align(s_state, LV_TEXT_ALIGN_LEFT, 0);
    box(screen, 20, 80, 200, 2, 0xD9E7EC, 0, 0);
    lv_obj_t *name = label(screen, 55, &plank_zh_16);
    lv_obj_set_align(name, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(name, 22, 55);
    lv_obj_set_width(name, 104);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(name, "平板支撑");
    lv_obj_set_pos(s_state, 18, 130);
    lv_obj_set_width(s_state, 204);
    lv_obj_set_style_text_align(s_state, LV_TEXT_ALIGN_CENTER, 0);
    health_motion_create(&s_motion, screen, 72, 85);
    s_time = label(screen, 151, &lv_font_montserrat_32);
    plank_digits_create(&s_minutes, screen, 4, 16, false);
    lv_obj_set_pos(s_minutes.obj, 53, 132);
    s_minute_unit = label(screen, 132, &lv_font_montserrat_14);
    lv_label_set_text(s_minute_unit, "MIN");
    lv_obj_set_align(s_minute_unit, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_minute_unit, 122, 132);
    lv_obj_set_size(s_minute_unit, 31, 18);
    plank_digits_create(&s_seconds, screen, 2, 48, false);
    lv_obj_set_pos(s_seconds.obj, 53, 151);
    plank_digits_create(&s_fraction, screen, 2, 24, true);
    lv_obj_set_pos(s_fraction.obj, 149, 175);
    s_dot = lv_obj_create(screen);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_pos(s_dot, 137, 194);
    lv_obj_set_size(s_dot, 4, 4);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0x162E32), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_dot, 1, 0);
    plank_digits_create(&s_countdown, screen, 1, 48, false);
    lv_obj_set_pos(s_countdown.obj, 106, 151);
    s_detail = label(screen, 202, &plank_zh_16);
    lv_obj_t *navigation = label(screen, 222, &plank_zh_16);
    lv_label_set_text(navigation, "1/4 长按上/下切换");
    s_voice_bar = box(screen, 8, 250, 224, 32, 0xD9E7EC, 6, 2);
    s_voice = label(s_voice_bar, 5, &plank_zh_16);
    lv_obj_set_width(s_voice, 212);
    lv_obj_t *footer = box(screen, 0, 292, 240, 28, UI_INK, 0, 0);
    s_keys_label = label(footer, 5, &plank_zh_16);
    lv_obj_set_width(s_keys_label, 232);
    lv_obj_set_style_text_color(s_keys_label, lv_color_hex(0xFFFFFF), 0);
    s_volume_box = box(screen, 40, 94, 160, 40, UI_INK, 8, 0);
    s_volume = label(s_volume_box, 10, &plank_zh_16);
    lv_obj_set_width(s_volume, 152);
    lv_obj_set_style_text_color(s_volume, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(s_volume_box, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(screen);
}

static void show_static_time(const char *text, const lv_font_t *font)
{
    lv_obj_add_flag(s_seconds.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_minutes.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_minute_unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_time, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(s_time, font, 0);
    lv_label_set_text(s_time, text);
    lv_obj_add_flag(s_fraction.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_countdown.obj, LV_OBJ_FLAG_HIDDEN);
}

static void show_stopwatch_time(uint64_t duration_ms)
{
    uint64_t seconds = duration_ms / 1000;
    if (seconds >= 360000) {
        char text[24];
        plank_format_time(duration_ms, text, sizeof(text));
        show_static_time(text, &lv_font_montserrat_32);
        return;
    }

    lv_obj_add_flag(s_time, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_countdown.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_seconds.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_fraction.obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    unsigned secs = (unsigned)(seconds % 60);
    unsigned places = secs >= 10 ? 2 : 1;
    int x = 120 - (int)(places * 30 - 3 + 44) / 2 - (int)(2 - places) * 30;
    lv_obj_set_x(s_seconds.obj, x);
    lv_obj_set_x(s_dot, x + 63);
    lv_obj_set_x(s_fraction.obj, x + 72);
    plank_digits_set(&s_seconds, secs);
    plank_digits_set(&s_fraction, (unsigned)(duration_ms % 1000 / 10));
    if (seconds >= 60) {
        unsigned minutes = (unsigned)(seconds / 60);
        unsigned m_places = minutes >= 1000 ? 4 : minutes >= 100 ? 3 : minutes >= 10 ? 2 : 1;
        int m_width = (int)m_places * 10 - 1;
        int m_x = 120 - (m_width + 34) / 2;
        lv_obj_set_x(s_minutes.obj, m_x - (39 - m_width));
        lv_obj_set_x(s_minute_unit, m_x + m_width + 6);
        lv_obj_clear_flag(s_minutes.obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_minute_unit, LV_OBJ_FLAG_HIDDEN);
        plank_digits_set(&s_minutes, minutes);
    } else {
        lv_obj_add_flag(s_minutes.obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_minute_unit, LV_OBJ_FLAG_HIDDEN);
    }
}

void plank_ui_refresh(const plank_model_t *model, uint64_t now, int battery,
                      bool audio_ok, bool buttons_ok, const char *voice_text,
                      uint32_t voice_color, unsigned volume, bool volume_visible,
                      const char *running_activity)
{
    char text[24];
    lv_obj_set_style_bg_color(s_voice_bar, lv_color_hex(voice_color), 0);
    lv_label_set_text(s_voice, voice_text ? voice_text : "语音就绪");
    if (volume == 0) lv_obj_clear_flag(s_mute, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_mute, LV_OBJ_FLAG_HIDDEN);
    if (volume_visible) {
        if (volume) lv_label_set_text_fmt(s_volume, "音量 %u%%", volume);
        else lv_label_set_text(s_volume, "已静音");
        lv_obj_clear_flag(s_volume_box, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(s_volume_box, LV_OBJ_FLAG_HIDDEN);
    if (battery < 0) lv_label_set_text(s_battery, "--%");
    else lv_label_set_text_fmt(s_battery, "%d%%", battery);
    health_motion_show(&s_motion, 0, model->state == PLANK_RUNNING, now);
    const char *state = "自由计时";
    const char *detail = "";
    const char *keys = "确定:开始";
    switch (model->state) {
        case PLANK_READY:
            show_stopwatch_time(0);
            if (!model->storage_available) { state = "存取失败"; detail = "仅本次开机保留"; }
            else if (model->history.revision > model->durable_revision) {
                detail = model->save == PLANK_SAVE_PENDING ? "保存中..." : "有未保存记录";
                keys = "确定:开始";
            }
            break;
        case PLANK_PREPARING:
            state = "准备";
            show_static_time("", &lv_font_montserrat_32);
            lv_obj_clear_flag(s_countdown.obj, LV_OBJ_FLAG_HIDDEN);
            plank_digits_set(&s_countdown, plank_prepare_seconds(model, now));
            detail = "";
            keys = "确定:取消";
            break;
        case PLANK_RUNNING:
            state = "计时中";
            if (plank_elapsed(model, now) < PLANK_START_SIGNAL_MS) {
                show_static_time("", &lv_font_montserrat_32);
                lv_obj_clear_flag(s_countdown.obj, LV_OBJ_FLAG_HIDDEN);
                plank_digits_set(&s_countdown, 0);
            } else {
                show_stopwatch_time(plank_elapsed(model, now));
            }
            detail = "";
            keys = "确定:结束";
            break;
        case PLANK_RESULT:
            state = "已结束";
            show_stopwatch_time(model->result_ms);
            detail = model->save == PLANK_SAVE_PENDING ? "保存中..." :
                     model->save == PLANK_SAVE_OK ? "已保存" : "未保存 仅临时保留";
            keys = "确定:开始";
            break;
        case PLANK_HISTORY:
            state = "最近记录";
            if (!model->history.count) {
                show_static_time("--:--.--", &lv_font_montserrat_32);
                detail = model->storage_available ? "还没有记录" : "历史不可用";
            } else {
                unsigned i = model->history_index;
                plank_format_time(model->history.duration_ms[i], text, sizeof(text));
                show_static_time(text, &lv_font_montserrat_32);
                lv_label_set_text_fmt(s_detail, "%u / %u %s", i + 1,
                                      (unsigned)model->history.count,
                                      plank_record_saved(model, i) ? "已保存" : "未保存");
                detail = NULL;
            }
            keys = "确定:返回";
            break;
    }
    if (model->state == PLANK_READY && model->storage_available && !detail[0])
        detail = "就位后按确定开始";
    if ((model->state == PLANK_READY || model->state == PLANK_RESULT) &&
        model->save == PLANK_SAVE_PENDING) keys = "保存中...";
    if (!audio_ok && model->state == PLANK_READY) state = "声音不可用";
    if (!buttons_ok) { state = "按键不可用"; keys = "重新开机再试"; }
    if (running_activity) {
        lv_label_set_text_fmt(s_detail, "%s进行中", running_activity);
        detail = NULL;
        keys = "确定:查看进行中训练";
    }
    bool minutes_visible = !lv_obj_has_flag(s_minutes.obj, LV_OBJ_FLAG_HIDDEN);
    if (minutes_visible) lv_obj_add_flag(s_state, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(s_state, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_state, state);
    if (detail) lv_label_set_text(s_detail, detail);
    lv_label_set_text(s_keys_label, keys);
}
