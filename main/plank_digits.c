#include "plank_digits.h"

static void draw_segment(lv_layer_t *layer, const lv_area_t *area, lv_color_t color, bool horizontal)
{
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = color;
    lv_area_t body = *area;
    lv_draw_triangle_dsc_t cap;
    lv_draw_triangle_dsc_init(&cap);
    cap.color = color;
    if (horizontal) {
        int bevel = lv_area_get_height(area) / 2;
        body.x1 += bevel;
        body.x2 -= bevel;
        cap.p[0] = (lv_point_precise_t){area->x1, area->y1 + bevel};
        cap.p[1] = (lv_point_precise_t){body.x1, area->y1};
        cap.p[2] = (lv_point_precise_t){body.x1, area->y2};
        lv_draw_triangle(layer, &cap);
        cap.p[0].x = area->x2;
        cap.p[1].x = cap.p[2].x = body.x2;
    } else {
        int bevel = lv_area_get_width(area) / 2;
        body.y1 += bevel;
        body.y2 -= bevel;
        cap.p[0] = (lv_point_precise_t){area->x1 + bevel, area->y1};
        cap.p[1] = (lv_point_precise_t){area->x1, body.y1};
        cap.p[2] = (lv_point_precise_t){area->x2, body.y1};
        lv_draw_triangle(layer, &cap);
        cap.p[0].y = area->y2;
        cap.p[1].y = cap.p[2].y = body.y2;
    }
    lv_draw_triangle(layer, &cap);
    lv_draw_rect(layer, &rect, &body);
}

static void draw_digits(lv_event_t *event)
{
    const plank_digits_t *digits = lv_event_get_user_data(event);
    lv_area_t box;
    lv_obj_get_coords(digits->obj, &box);
    lv_layer_t *layer = lv_event_get_layer(event);
    const int h = (int)digits->height;
    const int w = h * 9 / 16;
    const int thickness = h / 8;
    const int gap = h / 16;
    const int bevel = thickness / 2;
    const int half = h / 2;
    const lv_area_t segments[] = {
        {bevel + 1, 0, w - bevel - 2, thickness - 1},
        {w - thickness, bevel + 1, w - 1, half - 2},
        {w - thickness, half + 1, w - 1, h - bevel - 2},
        {bevel + 1, h - thickness, w - bevel - 2, h - 1},
        {0, half + 1, thickness - 1, h - bevel - 2},
        {0, bevel + 1, thickness - 1, half - 2},
        {bevel + 1, half - bevel, w - bevel - 2, half - bevel + thickness - 1},
    };
    static const uint8_t masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    unsigned divisor = 1;
    for (unsigned i = 1; i < digits->places; i++) divisor *= 10;
    bool visible = digits->leading_zero;
    for (unsigned i = 0; i < digits->places; i++, divisor /= 10) {
        unsigned digit = digits->value / divisor % 10;
        visible = visible || digit != 0 || i + 1 == digits->places;
        if (!visible) continue;
        for (unsigned segment = 0; segment < 7; segment++) {
            if (!(masks[digit] & (1U << segment))) continue;
            lv_area_t area = segments[segment];
            lv_area_move(&area, box.x1 + (int)i * (w + gap), box.y1);
            draw_segment(layer, &area, lv_color_hex(0x162E32),
                         segment == 0 || segment == 3 || segment == 6);
        }
    }
}

void plank_digits_create(plank_digits_t *digits, lv_obj_t *parent,
                         unsigned places, unsigned height, bool leading_zero)
{
    *digits = (plank_digits_t){.places = places, .height = height, .leading_zero = leading_zero};
    digits->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(digits->obj);
    lv_obj_remove_flag(digits->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(digits->obj, LV_OBJ_FLAG_USER_1);
    lv_obj_set_size(digits->obj, places * (height * 9 / 16) + (places - 1) * (height / 16), height);
    lv_obj_add_event_cb(digits->obj, draw_digits, LV_EVENT_DRAW_MAIN, digits);
}

void plank_digits_set(plank_digits_t *digits, unsigned value)
{
    if (digits->value == value) return;
    digits->value = value;
    lv_obj_invalidate(digits->obj);
}
