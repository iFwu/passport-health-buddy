#include "plank_ui.h"
#include "ui_pixel.h"
#include "plank_digits.h"
#include <stdio.h>
#include <inttypes.h>
LV_FONT_DECLARE(plank_zh_16);

static lv_obj_t *s_time, *s_dot, *s_state, *s_detail, *s_keys_label, *s_battery, *s_sound;
static lv_obj_t *s_minute_unit;
static plank_digits_t s_seconds, s_fraction, s_minutes, s_countdown;

static lv_obj_t *label(lv_obj_t *parent, int y, const lv_font_t *font)
{
    lv_obj_t *obj = ui_pixel_label(parent, "", font, UI_INK);
    lv_obj_set_width(obj, 208);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
    return obj;
}

void plank_ui_create(void)
{
    lv_obj_t *screen = ui_pixel_screen_create("PLANK");
    s_battery = ui_pixel_label(screen, "--%", &plank_zh_16, 0xFFFFFF);
    lv_obj_set_pos(s_battery, 173, 30);
    lv_obj_set_size(s_battery, 63, 20);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);
    ui_pixel_panel_create(screen, 9, 58, 217, 169, UI_PAPER);
    s_state = label(screen, 73, &plank_zh_16);
    lv_obj_set_align(s_state, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_state, 23, 73);
    lv_obj_set_width(s_state, 88);
    lv_obj_set_style_text_align(s_state, LV_TEXT_ALIGN_LEFT, 0);
    s_time = label(screen, 124, &lv_font_montserrat_32);
    plank_digits_create(&s_minutes, screen, 4, 28, false);
    lv_obj_set_pos(s_minutes.obj, 111, 71);
    s_minute_unit = ui_pixel_label(screen, "MIN", &lv_font_montserrat_14, UI_INK);
    lv_obj_set_pos(s_minute_unit, 184, 83);
    lv_obj_set_size(s_minute_unit, 31, 18);
    plank_digits_create(&s_seconds, screen, 2, 64, false);
    lv_obj_set_pos(s_seconds.obj, 49, 120);
    plank_digits_create(&s_fraction, screen, 2, 32, true);
    lv_obj_set_pos(s_fraction.obj, 149, 152);
    s_dot = lv_obj_create(screen);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_pos(s_dot, 137, 179);
    lv_obj_set_size(s_dot, 5, 5);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0x162E32), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_dot, 1, 0);
    plank_digits_create(&s_countdown, screen, 1, 64, false);
    lv_obj_set_pos(s_countdown.obj, 102, 120);
    s_detail = label(screen, 197, &plank_zh_16);
    lv_obj_t *commands = lv_obj_create(screen);
    lv_obj_remove_style_all(commands);
    lv_obj_set_pos(commands, 0, 235);
    lv_obj_set_size(commands, 240, 51);
    lv_obj_set_style_bg_color(commands, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_bg_opa(commands, LV_OPA_COVER, 0);
    s_keys_label = label(screen, 242, &plank_zh_16);
    lv_obj_set_align(s_keys_label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_keys_label, 16, 242);
    lv_obj_set_width(s_keys_label, 170);
    lv_obj_set_style_text_align(s_keys_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_keys_label, lv_color_hex(0xFFFFFF), 0);
    s_sound = ui_pixel_label(screen, "", &plank_zh_16, 0xFFFFFF);
    lv_obj_set_pos(s_sound, 16, 290);
    lv_obj_set_style_text_color(s_sound, lv_color_hex(UI_INK), 0);
    ui_pixel_mascot_create(screen, 187, 268);
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
    plank_digits_set(&s_seconds, (unsigned)(seconds % 60));
    plank_digits_set(&s_fraction, (unsigned)(duration_ms % 1000 / 10));
    if (seconds >= 60) {
        unsigned minutes = (unsigned)(seconds / 60);
        lv_obj_clear_flag(s_minutes.obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_minute_unit, LV_OBJ_FLAG_HIDDEN);
        plank_digits_set(&s_minutes, minutes);
    } else {
        lv_obj_add_flag(s_minutes.obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_minute_unit, LV_OBJ_FLAG_HIDDEN);
    }
}

void plank_ui_refresh(const plank_model_t *model, uint64_t now, int battery,
                      bool audio_ok, bool buttons_ok)
{
    char text[24];
    lv_label_set_text(s_sound, !audio_ok ? "声音不可用" : model->sound ? "声音 开" : "声音 关");
    if (battery < 0) lv_label_set_text(s_battery, "--%");
    else lv_label_set_text_fmt(s_battery, "%d%%", battery);
    const char *state = "自由计时";
    const char *detail = "";
    lv_obj_set_y(s_detail, 197);
    const char *keys = "确定:开始\n上:声音 下:历史";
    switch (model->state) {
        case PLANK_READY:
            show_stopwatch_time(0);
            if (!model->storage_available) { state = "存取失败"; detail = "仅本次开机保留"; }
            else if (model->history.revision > model->durable_revision) {
                detail = model->save == PLANK_SAVE_PENDING ? "保存中..." : "有未保存记录";
                keys = model->save == PLANK_SAVE_PENDING ? "" : "上:重试 确定:开始\n下:历史";
            }
            break;
        case PLANK_PREPARING:
            state = "准备";
            show_static_time("", &lv_font_montserrat_32);
            lv_obj_clear_flag(s_countdown.obj, LV_OBJ_FLAG_HIDDEN);
            plank_digits_set(&s_countdown, plank_prepare_seconds(model, now));
            detail = "";
            keys = "任意键取消";
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
            keys = "任意键结束并保存";
            break;
        case PLANK_RESULT:
            state = "已结束";
            show_stopwatch_time(model->result_ms);
            detail = model->save == PLANK_SAVE_PENDING ? "保存中..." :
                     model->save == PLANK_SAVE_OK ? "已保存" : "未保存 仅临时保留";
            keys = model->save == PLANK_SAVE_PENDING ? "" :
                   model->save == PLANK_SAVE_FAILED && model->storage_available ?
                   "上:重试 确定:开始\n下:历史" : "确定:开始\n上:声音 下:历史";
            break;
        case PLANK_HISTORY:
            state = "最近记录";
            lv_obj_set_y(s_detail, 177);
            if (!model->history.count) {
                show_static_time("--:--.--", &lv_font_montserrat_32);
                detail = model->storage_available ? "还没有记录" : "历史不可用";
            } else {
                unsigned i = model->history_index;
                plank_format_time(model->history.duration_ms[i], text, sizeof(text));
                show_static_time(text, &lv_font_montserrat_32);
                lv_label_set_text_fmt(s_detail, "%u / %u (从新到旧)\n%s", i + 1,
                                      (unsigned)model->history.count,
                                      plank_record_saved(model, i) ? "已保存 无日期" : "未保存 无日期");
                detail = NULL;
            }
            keys = "上:较新 下:较旧\n确定:返回";
            break;
    }
    if (!buttons_ok) { state = "按键不可用"; keys = "重新开机再试"; }
    lv_label_set_text(s_state, state);
    if (detail) lv_label_set_text(s_detail, detail);
    lv_label_set_text(s_keys_label, keys);
}
