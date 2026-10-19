#include "health_ui.h"
#include "plank_audio.h"
#include "lvgl.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t s_buffer[240 * 320];
static unsigned char s_pixels[240 * 320 * 3];
static unsigned char s_before[sizeof(s_pixels)];
static unsigned s_flushes;
static lv_area_t s_labels[32];
static unsigned s_label_count;
static unsigned s_footer_labels;
static health_input_t s_input;
static health_log_t s_log;
static plank_model_t s_model;
static unsigned s_volume = 50;
static unsigned s_voice_starts, s_voice_stops;
static bool s_recording;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            unsigned color = pixels[0] | ((unsigned)pixels[1] << 8);
            unsigned index = (unsigned)(y * 240 + x) * 3;
            s_pixels[index] = (unsigned char)((color >> 11) * 255 / 31);
            s_pixels[index + 1] = (unsigned char)(((color >> 5) & 63) * 255 / 63);
            s_pixels[index + 2] = (unsigned char)((color & 31) * 255 / 31);
            pixels += 2;
        }
    }
    s_flushes++;
    lv_display_flush_ready(display);
}

static void check_glyphs(lv_obj_t *label)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, 0);
    const unsigned char *text = (const unsigned char *)lv_label_get_text(label);
    while (*text) {
        uint32_t cp = *text++;
        unsigned remaining = 0;
        if ((cp & 0xE0) == 0xC0) { cp &= 0x1F; remaining = 1; }
        else if ((cp & 0xF0) == 0xE0) { cp &= 0x0F; remaining = 2; }
        else if ((cp & 0xF8) == 0xF0) { cp &= 7; remaining = 3; }
        while (remaining--) {
            assert((*text & 0xC0) == 0x80);
            cp = (cp << 6) | (*text++ & 0x3F);
        }
        if (cp < 32) continue;
        lv_font_glyph_dsc_t glyph = {0};
        bool found = lv_font_get_glyph_dsc(font, &glyph, cp, 0);
        if (!found || glyph.is_placeholder)
            fprintf(stderr, "Missing selected-font glyph U+%04lx in %s\n",
                    (unsigned long)cp, lv_label_get_text(label));
        assert(found && !glyph.is_placeholder);
    }
}

static void check_objects(lv_obj_t *obj, bool overlaps)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    assert(lv_obj_get_style_shadow_width(obj, 0) == 0);
    bool is_label = lv_obj_check_type(obj, &lv_label_class);
    if (is_label || lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_1)) {
        lv_area_t area;
        lv_obj_get_coords(obj, &area);
        assert(area.x1 >= 0 && area.x2 < 240 && area.y1 >= 0 && area.y2 < 320);
        bool nonempty = !is_label || *lv_label_get_text(obj);
        if (is_label && nonempty) {
            check_glyphs(obj);
            lv_point_t size;
            lv_text_get_size(&size, lv_label_get_text(obj), lv_obj_get_style_text_font(obj, 0),
                             0, lv_obj_get_style_text_line_space(obj, 0), LV_COORD_MAX, 0);
            if (size.x > lv_obj_get_width(obj))
                fprintf(stderr, "Health label overflow: %s (%ld > %ld)\n",
                        lv_label_get_text(obj), (long)size.x, (long)lv_obj_get_width(obj));
            assert(size.x <= lv_obj_get_width(obj));
            assert(size.y <= lv_obj_get_height(obj));
            if (area.y1 >= 292) {
                assert(strchr(lv_label_get_text(obj), '\n') == NULL);
                s_footer_labels++;
            }
        }
        if (nonempty && overlaps) {
            for (unsigned i = 0; i < s_label_count; i++) {
                const lv_area_t *other = &s_labels[i];
                bool separate = area.x1 > other->x2 || area.x2 < other->x1 ||
                                area.y1 > other->y2 || area.y2 < other->y1;
                if (!separate) fprintf(stderr, "Health label overlap: %s\n",
                                       is_label ? lv_label_get_text(obj) : "[digits]");
                assert(separate);
            }
            assert(s_label_count < sizeof(s_labels) / sizeof(s_labels[0]));
            s_labels[s_label_count++] = area;
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(obj); i++)
        check_objects(lv_obj_get_child(obj, (int)i), overlaps);
}

static void check_regions(void)
{
    bool card = false, voice = false, footer = false;
    lv_obj_t *screen = lv_screen_active();
    for (unsigned i = 0; i < lv_obj_get_child_count(screen); i++) {
        lv_obj_t *obj = lv_obj_get_child(screen, (int)i);
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) continue;
        lv_area_t area;
        lv_obj_get_coords(obj, &area);
        if (area.x1 == 8 && area.y1 == 44 && lv_obj_get_width(obj) == 224 &&
            lv_obj_get_height(obj) == 200) card = true;
        if (area.x1 == 8 && area.y1 == 250 && lv_obj_get_width(obj) == 224 &&
            lv_obj_get_height(obj) == 32) voice = true;
        if (area.x1 == 0 && area.y1 == 292 && lv_obj_get_width(obj) == 240 &&
            lv_obj_get_height(obj) == 28) footer = true;
    }
    assert(card && voice && footer && s_footer_labels == 1);
}

static bool region_changed(int x, int y, int width, int height)
{
    for (int row = y; row < y + height; row++) {
        size_t offset = (size_t)(row * 240 + x) * 3;
        if (memcmp(s_before + offset, s_pixels + offset, (size_t)width * 3)) return true;
    }
    return false;
}

static void frame(uint64_t now, health_voice_state_t voice, bool volume_visible,
                   const char *directory, const char *name)
{
    health_ui_refresh(&s_log, &s_model, now, 75, "", true, true, voice);
    lv_obj_update_layout(lv_screen_active());
    s_label_count = 0;
    s_footer_labels = 0;
    check_objects(lv_screen_active(), !volume_visible);
    check_regions();
    unsigned before = s_flushes;
    lv_obj_invalidate(lv_screen_active());
    lv_tick_inc(40);
    lv_timer_handler();
    assert(s_flushes > before);
    if (!name) return;
    char filename[1024];
    snprintf(filename, sizeof(filename), "%s/%s.ppm", directory, name);
    FILE *file = fopen(filename, "wb");
    assert(file);
    fprintf(file, "P6\n240 320\n255\n");
    assert(fwrite(s_pixels, 1, sizeof(s_pixels), file) == sizeof(s_pixels));
    assert(fclose(file) == 0);
}

/* This dispatch uses the production reducer and production UI action handler.
 * Only platform-owned volume/PTT sinks are represented locally. */
static unsigned key(health_key_t key, health_key_event_t event, uint64_t now)
{
    health_action_t action = health_input_event(&s_input, key, event);
    if (action == HEALTH_ACTION_VOLUME_UP || action == HEALTH_ACTION_VOLUME_DOWN) {
        if (action == HEALTH_ACTION_VOLUME_UP) s_volume = s_volume >= 90 ? 100 : s_volume + 10;
        else s_volume = s_volume < 10 ? 0 : s_volume - 10;
        health_ui_set_volume(s_volume, now);
        return 0;
    }
    if (action == HEALTH_ACTION_VOICE_START) { s_recording = true; s_voice_starts++; return 0; }
    if (action == HEALTH_ACTION_VOICE_STOP) { s_recording = false; s_voice_stops++; return 0; }
    return health_ui_action(action, &s_log, &s_model, now);
}

static unsigned tap(health_key_t button, uint64_t now)
{
    assert(key(button, HEALTH_KEY_PRESS, now) == 0);
    return key(button, HEALTH_KEY_RELEASE, now + 1);
}

static void hold_mode(health_key_t button, health_ui_mode_t expected, uint64_t now)
{
    unsigned volume = s_volume;
    assert(key(button, HEALTH_KEY_PRESS, now) == 0);
    assert(key(button, HEALTH_KEY_LONG, now + 800) == 0);
    assert(health_ui_mode() == expected);
    for (unsigned i = 0; i < 3; i++) {
        assert(key(button, HEALTH_KEY_LONG, now + 900 + i) == 0);
        assert(health_ui_mode() == expected);
    }
    assert(key(button, HEALTH_KEY_RELEASE, now + 1000) == 0);
    assert(s_volume == volume);
}

static void voice_hold(uint64_t now)
{
    uint32_t water = s_log.saved.water_ml;
    uint64_t rest = s_log.rest_until_ms;
    bool interval = s_log.interval_on;
    plank_state_t state = s_model.state;
    health_ui_mode_t mode = health_ui_mode();
    unsigned starts = s_voice_starts, stops = s_voice_stops;
    assert(key(HEALTH_KEY_OK, HEALTH_KEY_PRESS, now) == 0);
    assert(key(HEALTH_KEY_OK, HEALTH_KEY_LONG, now + 800) == 0);
    assert(s_recording && s_voice_starts == starts + 1);
    assert(key(HEALTH_KEY_OK, HEALTH_KEY_LONG, now + 900) == 0);
    assert(s_voice_starts == starts + 1);
    assert(key(HEALTH_KEY_UP, HEALTH_KEY_PRESS, now + 910) == 0);
    assert(key(HEALTH_KEY_UP, HEALTH_KEY_RELEASE, now + 920) == 0);
    assert(s_recording && s_volume == 50);
    assert(key(HEALTH_KEY_OK, HEALTH_KEY_RELEASE, now + 1000) == 0);
    assert(!s_recording && s_voice_stops == stops + 1);
    assert(s_log.saved.water_ml == water && s_log.rest_until_ms == rest);
    assert(s_log.interval_on == interval && s_model.state == state && health_ui_mode() == mode);
}

static void help_navigation(const char *directory)
{
    const health_ui_mode_t pages[] = { HEALTH_UI_EVERYDAY, HEALTH_UI_GYM, HEALTH_UI_HOME };
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        uint64_t now = 40000 + i * 10000;
        health_ui_set_mode(pages[i]);
        health_log_t log = s_log;
        plank_model_t model = s_model;
        health_ui_mode_t target = pages[i];
        if (pages[i] == HEALTH_UI_EVERYDAY) {
            hold_mode(HEALTH_KEY_UP, HEALTH_UI_HELP, now);
        } else {
            if (pages[i] == HEALTH_UI_GYM) {
                hold_mode(HEALTH_KEY_DOWN, HEALTH_UI_HOME, now);
                target = HEALTH_UI_HOME;
            }
            hold_mode(HEALTH_KEY_DOWN, HEALTH_UI_HELP, now + 1100);
        }
        frame(now + 2200, HEALTH_VOICE_READY, false, directory, i == 0 ? "help" : NULL);
        assert(tap(HEALTH_KEY_UP, now + 2300) == 0 && s_volume == 60);
        assert(tap(HEALTH_KEY_DOWN, now + 2400) == 0 && s_volume == 50);
        assert(health_ui_mode() == HEALTH_UI_HELP);
        voice_hold(now + 2500);
        assert(tap(HEALTH_KEY_OK, now + 3700) == 0);
        assert(health_ui_mode() == target);
        assert(memcmp(&s_log, &log, sizeof(log)) == 0);
        assert(memcmp(&s_model, &model, sizeof(model)) == 0);

        // Direct application page selection remembers each business page too.
        health_ui_set_mode(pages[i]);
        health_ui_set_mode(HEALTH_UI_HELP);
        health_ui_set_mode(HEALTH_UI_HELP);
        assert(tap(HEALTH_KEY_OK, now + 3800) == 0);
        assert(health_ui_mode() == pages[i]);
    }
    health_ui_set_mode(HEALTH_UI_HOME);
    hold_mode(HEALTH_KEY_DOWN, HEALTH_UI_HELP, 70000);
    hold_mode(HEALTH_KEY_DOWN, HEALTH_UI_EVERYDAY, 71100);
    hold_mode(HEALTH_KEY_UP, HEALTH_UI_HELP, 72200);
    hold_mode(HEALTH_KEY_UP, HEALTH_UI_HOME, 73300);
}

static void timers_through_help(const char *directory)
{
    health_ui_set_mode(HEALTH_UI_HOME);
    assert(tap(HEALTH_KEY_OK, 80000) == PLANK_EFFECT_PREP_1);
    hold_mode(HEALTH_KEY_DOWN, HEALTH_UI_HELP, 80100);
    assert(plank_tick(&s_model, 84002) == PLANK_EFFECT_START);
    assert(s_model.state == PLANK_RUNNING);
    frame(88000, HEALTH_VOICE_READY, false, directory, NULL);
    uint64_t elapsed = plank_elapsed(&s_model, 88000);
    voice_hold(88100);
    assert(tap(HEALTH_KEY_OK, 89500) == 0);
    assert(health_ui_mode() == HEALTH_UI_HOME && s_model.state == PLANK_RUNNING);
    assert(plank_elapsed(&s_model, 89501) > elapsed);

    health_log_rest(&s_log, 90, 90000);
    assert(health_log_start_interval(&s_log, 30, 10, 3, 90000));
    uint64_t rest_until = s_log.rest_until_ms;
    hold_mode(HEALTH_KEY_DOWN, HEALTH_UI_HELP, 90100);
    assert(health_log_rest_left_s(&s_log, 95000) == 85);
    assert(health_log_tick(&s_log, 120000));
    assert(s_log.interval_on && !s_log.interval_work);
    assert(s_log.rest_until_ms == rest_until);
    frame(120001, HEALTH_VOICE_READY, false, directory, NULL);
    assert(tap(HEALTH_KEY_OK, 120100) == 0);
    assert(health_ui_mode() == HEALTH_UI_HOME && s_log.interval_on);
    assert(s_model.state == PLANK_RUNNING);
    assert(tap(HEALTH_KEY_OK, 120200) == PLANK_EFFECT_STOP && !s_log.interval_on);
}

static void persistent_mute(const char *directory)
{
    const health_ui_mode_t modes[] = {
        HEALTH_UI_EVERYDAY, HEALTH_UI_GYM, HEALTH_UI_HOME, HEALTH_UI_HELP,
    };
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        health_ui_set_mode(modes[i]);
        uint64_t now = 200000 + i * 10000;
        health_ui_set_volume(50, now);
        frame(now + 2000, HEALTH_VOICE_READY, false, directory, NULL);
        memcpy(s_before, s_pixels, sizeof(s_before));
        health_ui_set_volume(0, now + 2100);
        frame(now + 4000, HEALTH_VOICE_READY, false, directory, NULL);
        assert(region_changed(130, 52, 46, 25));
        memcpy(s_before, s_pixels, sizeof(s_before));
        frame(now + 6000, HEALTH_VOICE_READY, false, directory, NULL);
        assert(!region_changed(130, 52, 46, 25));
        health_ui_set_volume(50, now + 6100);
        frame(now + 8000, HEALTH_VOICE_READY, false, directory, NULL);
        assert(region_changed(130, 52, 46, 25));
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *directory = argv[1];
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, s_buffer, NULL, sizeof(s_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    plank_ui_create();
    lv_obj_t *home = lv_screen_active();
    health_ui_create(home);
    health_log_init(&s_log, NULL, 0);
    plank_init(&s_model, NULL, true);
    health_ui_set_volume(s_volume, 0);

    assert(tap(HEALTH_KEY_OK, 1600) == HEALTH_SOUND_OK);
    assert(tap(HEALTH_KEY_OK, 1610) == HEALTH_SOUND_OK);
    assert(s_log.saved.water_ml == 500 && s_log.last_stand_ms == 0);
    frame(1700, HEALTH_VOICE_READY, false, directory, "office");
    memcpy(s_before, s_pixels, sizeof(s_before));
    assert(tap(HEALTH_KEY_DOWN, 1701) == 0 && s_volume == 40);
    assert(s_log.saved.water_ml == 500);
    frame(1702, HEALTH_VOICE_READY, true, directory, "office-volume");
    frame(3202, HEALTH_VOICE_READY, false, directory, NULL);
    assert(memcmp(s_before, s_pixels, sizeof(s_pixels)) == 0);
    assert(tap(HEALTH_KEY_UP, 3300) == 0 && s_volume == 50);
    voice_hold(3400);
    frame(4900, HEALTH_VOICE_RECORDING, false, directory, "office-recording");

    hold_mode(HEALTH_KEY_DOWN, HEALTH_UI_GYM, 5000);
    assert(tap(HEALTH_KEY_OK, 6100) == 0);
    assert(health_log_rest_left_s(&s_log, 6101) == 90);
    frame(6200, HEALTH_VOICE_READY, false, directory, "gym-rest");
    assert(tap(HEALTH_KEY_OK, 6300) == HEALTH_SOUND_RELAX);
    assert(health_log_rest_left_s(&s_log, 6301) == 0 && !s_log.timer_running);
    health_entry_t entry = { .exercise = HEALTH_EX_BENCH_PRESS, .reps = 8,
                             .sets = 3, .weight_x10 = 600 };
    assert(health_log_add_entry(&s_log, &entry, false, 6400));
    frame(6400, HEALTH_VOICE_READY, false, directory, "gym");
    voice_hold(6500);
    assert(tap(HEALTH_KEY_UP, 7600) == 0 && s_volume == 60);
    frame(7601, HEALTH_VOICE_RECORDING, true, directory, "gym-volume-recording");
    assert(tap(HEALTH_KEY_DOWN, 7700) == 0 && s_volume == 50);

    hold_mode(HEALTH_KEY_DOWN, HEALTH_UI_HOME, 10000);
    frame(11000, HEALTH_VOICE_READY, false, directory, "home");
    assert(lv_screen_active() == home);
    assert(tap(HEALTH_KEY_OK, 11100) == PLANK_EFFECT_PREP_1);
    frame(11101, HEALTH_VOICE_READY, false, directory, "home-preparing");
    hold_mode(HEALTH_KEY_DOWN, HEALTH_UI_HELP, 11200);
    assert(s_model.state == PLANK_PREPARING);
    assert(tap(HEALTH_KEY_OK, 12300) == 0 && health_ui_mode() == HEALTH_UI_HOME);
    assert(s_model.state == PLANK_PREPARING);
    assert(tap(HEALTH_KEY_OK, 13400) == 0 && s_model.state == PLANK_READY);
    assert(tap(HEALTH_KEY_OK, 13500) == PLANK_EFFECT_PREP_1);
    assert(plank_tick(&s_model, 17501) == PLANK_EFFECT_START);
    frame(18500, HEALTH_VOICE_READY, false, directory, "home-running");
    voice_hold(19000);
    assert((tap(HEALTH_KEY_OK, 21000) & (PLANK_EFFECT_STOP | PLANK_EFFECT_SAVE)) ==
           (PLANK_EFFECT_STOP | PLANK_EFFECT_SAVE));
    assert(s_model.state == PLANK_RESULT && s_model.history.count == 1);
    plank_saved(&s_model, true);
    frame(22000, HEALTH_VOICE_READY, false, directory, "home-result");
    uint64_t result = s_model.result_ms;
    s_model.history.count = 2;
    s_model.history.revision = s_model.durable_revision = 2;
    s_model.history.duration_ms[1] = 7654;
    frame(24000, HEALTH_VOICE_READY, false, directory, NULL);
    memcpy(s_before, s_pixels, sizeof(s_before));
    frame(28000, HEALTH_VOICE_READY, false, directory, NULL);
    assert(memcmp(s_before, s_pixels, sizeof(s_pixels)) == 0);
    assert(s_model.result_ms == result && s_model.state == PLANK_RESULT);
    assert(tap(HEALTH_KEY_UP, 28100) == 0 && s_volume == 60);
    frame(28101, HEALTH_VOICE_RECORDING, true, directory, "home-volume-recording");
    assert(tap(HEALTH_KEY_DOWN, 28200) == 0 && s_volume == 50);
    frame(30000, HEALTH_VOICE_READY, false, directory, NULL);

    assert(health_log_start_interval(&s_log, 30, 10, 3, 31000));
    frame(31000, HEALTH_VOICE_THINKING, false, directory, "home-interval");
    voice_hold(31200);
    assert(tap(HEALTH_KEY_OK, 32400) == PLANK_EFFECT_STOP && !s_log.interval_on);
    frame(32500, HEALTH_VOICE_READY, false, directory, NULL);
    assert(lv_screen_active() == home);
    memcpy(s_before, s_pixels, sizeof(s_before));
    health_ui_notice("活动一下吧", 33000);
    frame(33000, HEALTH_VOICE_READY, false, directory, "home-notice");
    assert(region_changed(8, 250, 224, 32));
    frame(36000, HEALTH_VOICE_READY, false, directory, NULL);
    assert(!region_changed(8, 250, 224, 32));
    help_navigation(directory);
    timers_through_help(directory);
    persistent_mute(directory);
    puts("Health LVGL: PASS (real reducer, Help return, timer continuity, compact layout, voice/volume, persistent mute, glyphs, bounds, history, notice expiry)");
    lv_deinit();
    return 0;
}
