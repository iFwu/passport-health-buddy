#include "plank_ui.h"
#include "plank_digits.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t s_buffer[240 * 320];
static unsigned char s_pixels[240 * 320 * 3];
static unsigned s_flushes;
static lv_area_t s_labels[32];
static unsigned s_label_count;
static unsigned s_footer_labels;

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

static void check_labels(lv_obj_t *obj)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    assert(lv_obj_get_style_shadow_width(obj, 0) == 0);
    bool is_label = lv_obj_check_type(obj, &lv_label_class);
    if (is_label || lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_1)) {
        lv_area_t box;
        lv_obj_get_coords(obj, &box);
        assert(box.x1 >= 0 && box.x2 < 240 && box.y1 >= 0 && box.y2 < 320);
        const char *text = is_label ? lv_label_get_text(obj) : "[digits]";
        lv_point_t size;
        if (is_label) lv_text_get_size(&size, text, lv_obj_get_style_text_font(obj, 0), 0,
                         lv_obj_get_style_text_line_space(obj, 0), LV_COORD_MAX, 0);
        if (is_label && size.x > lv_obj_get_width(obj)) {
            fprintf(stderr, "Label overflow: %s (%ld > %ld)\n", text,
                    (long)size.x, (long)lv_obj_get_width(obj));
            assert(false);
        }
        if (is_label && *text) {
            assert(size.y <= lv_obj_get_height(obj));
            if (box.y1 >= 292) {
                assert(strchr(text, '\n') == NULL);
                s_footer_labels++;
            }
        }
        if (*text) {
            for (unsigned i = 0; i < s_label_count; i++) {
                const lv_area_t *other = &s_labels[i];
                if (!(box.x1 > other->x2 || box.x2 < other->x1 ||
                      box.y1 > other->y2 || box.y2 < other->y1))
                    fprintf(stderr, "Overlap: %s [%d,%d,%d,%d] and [%d,%d,%d,%d]\n",
                            text, (int)box.x1, (int)box.y1, (int)box.x2, (int)box.y2,
                            (int)other->x1, (int)other->y1, (int)other->x2, (int)other->y2);
                assert(box.x1 > other->x2 || box.x2 < other->x1 ||
                       box.y1 > other->y2 || box.y2 < other->y1);
            }
            assert(s_label_count < sizeof(s_labels) / sizeof(s_labels[0]));
            s_labels[s_label_count++] = box;
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(obj); i++)
        check_labels(lv_obj_get_child(obj, (int)i));
}

static void capture(plank_model_t *model, const char *directory, const char *name,
                    uint64_t now, bool audio_ok, bool buttons_ok)
{
    plank_ui_refresh(model, now, -1, audio_ok, buttons_ok,
                      "语音就绪", 0xD9E7EC, 50, false, NULL);
    lv_obj_update_layout(lv_screen_active());
    s_label_count = 0;
    s_footer_labels = 0;
    check_labels(lv_screen_active());
    assert(s_footer_labels == 1);
    unsigned before = s_flushes;
    lv_obj_invalidate(lv_screen_active());
    lv_tick_inc(40);
    lv_timer_handler();
    assert(s_flushes > before);
    unsigned bright = 0, dark = 0;
    for (unsigned i = 0; i < sizeof(s_pixels); i++) {
        bright += s_pixels[i] > 180;
        dark += s_pixels[i] < 60;
    }
    assert(bright > 10000 && dark > 10000);
    char filename[1024];
    snprintf(filename, sizeof(filename), "%s/%s.ppm", directory, name);
    FILE *file = fopen(filename, "wb");
    assert(file);
    fprintf(file, "P6\n240 320\n255\n");
    assert(fwrite(s_pixels, 1, sizeof(s_pixels), file) == sizeof(s_pixels));
    assert(fclose(file) == 0);
}

static void check_digit_pixels(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), 0);
    lv_screen_load(screen);
    const unsigned heights[] = {16, 24, 48};
    const char *lit_segments[] = {
        "abcdef", "bc", "abdeg", "abcdg", "bcfg",
        "acdfg", "acdefg", "abc", "abcdefg", "abcdfg",
    };
    for (unsigned size = 0; size < sizeof(heights) / sizeof(heights[0]); size++) {
        unsigned h = heights[size], w = h * 9 / 16, t = h / 8;
        plank_digits_t digits;
        plank_digits_create(&digits, screen, 2, h, false);
        lv_obj_set_pos(digits.obj, 20, 20);
        lv_obj_update_layout(screen);
        lv_area_t original;
        lv_obj_get_coords(digits.obj, &original);
        const lv_point_t centers[] = {
            {w / 2, t / 2}, {w - 1 - t / 2, h / 4},
            {w - 1 - t / 2, h * 3 / 4}, {w / 2, h - 1 - t / 2},
            {t / 2, h * 3 / 4}, {t / 2, h / 4}, {w / 2, h / 2},
        };
        for (unsigned n = 20; n > 0; n--) {
            unsigned value = n - 1, digit = value % 10;
            plank_digits_set(&digits, value);
            lv_tick_inc(40);
            lv_timer_handler();
            lv_area_t current;
            lv_obj_get_coords(digits.obj, &current);
            assert(original.x1 == current.x1 && original.y1 == current.y1 &&
                   original.x2 == current.x2 && original.y2 == current.y2);
            for (unsigned segment = 0; segment < 7; segment++) {
                unsigned x = 20 + w + h / 16 + centers[segment].x;
                unsigned y = 20 + centers[segment].y;
                const unsigned char *pixel = &s_pixels[(y * 240 + x) * 3];
                bool active = strchr(lit_segments[digit], 'a' + (int)segment) != NULL;
                if (active) assert(pixel[0] < 60 && pixel[1] < 60 && pixel[2] < 60);
                else assert(pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255);
            }
            unsigned corner_x = 20 + w + h / 16 + t / 2 + 1;
            unsigned corner = s_pixels[(20 * 240 + corner_x) * 3];
            /* A one-pixel bevel leaves a half-covered antialiased edge pixel. */
            assert(corner >= 127);
            if (h == 64) assert(corner == 255);
            if (value < 10) {
                for (unsigned y = 20; y < 20 + h; y++)
                    for (unsigned x = 20; x < 20 + w; x++)
                        assert(s_pixels[(y * 240 + x) * 3] == 255);
            }
        }
        lv_obj_delete(digits.obj);
    }
    puts("Plank digits: PASS (0-9, activity-layout sizes, bevel, no inactive tint, stable bounds)");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, s_buffer, NULL, sizeof(s_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    plank_ui_create();
    plank_model_t model;
    plank_init(&model, NULL, true);
    capture(&model, argv[1], "ready", 0, true, true);
    plank_key(&model, PLANK_DOWN, 0);
    capture(&model, argv[1], "empty-history", 0, true, true);
    plank_key(&model, PLANK_OK, 0);
    plank_key(&model, PLANK_OK, 0);
    capture(&model, argv[1], "preparing", 2000, true, true);
    plank_tick(&model, 5000);
    capture(&model, argv[1], "starting", 5000, true, true);
    capture(&model, argv[1], "running", 5623, true, true);
    capture(&model, argv[1], "running-narrow-digits", 16110, true, true);
    capture(&model, argv[1], "running-seven", 22070, true, true);
    capture(&model, argv[1], "running-under-minute", 64990, true, true);
    capture(&model, argv[1], "running-over-minute", 65000, true, true);
    capture(&model, argv[1], "long-running", 359999999, true, true);
    plank_key(&model, PLANK_OK, 5623);
    capture(&model, argv[1], "saving", 5623, true, true);
    plank_saved(&model, false);
    capture(&model, argv[1], "save-failed", 5623, true, true);
    plank_key(&model, PLANK_DOWN, 5623);
    plank_key(&model, PLANK_OK, 5623);
    capture(&model, argv[1], "retry-from-home", 5623, true, true);
    plank_key(&model, PLANK_UP, 5623);
    capture(&model, argv[1], "retry-saving", 5623, true, true);
    plank_saved(&model, true);
    capture(&model, argv[1], "saved", 5623, true, true);
    plank_key(&model, PLANK_DOWN, 6000);
    capture(&model, argv[1], "history", 6000, true, true);
    plank_init(&model, NULL, false);
    capture(&model, argv[1], "storage-unavailable", 0, false, true);
    capture(&model, argv[1], "buttons-unavailable", 0, false, false);
    puts("Plank LVGL layout and rendered pixels: PASS (18 states, 240x320, fixed digit regions)");
    check_digit_pixels();
    lv_deinit();
    return 0;
}
