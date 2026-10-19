#include "health_motion.h"
#include "ui_pixel.h"
#include <limits.h>

static void draw_motion(lv_event_t *event)
{
    const health_motion_t *motion = lv_event_get_user_data(event);
    if (motion->pose == UINT_MAX) return;
    lv_area_t area;
    lv_obj_get_coords(motion->root, &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.round_start = line.round_end = true;
    for (unsigned i = 0; i < 6; i++) {
        line.width = i == 5 ? 2 : 3;
        line.color = lv_color_hex(i == 5 ? UI_SKY_DARK : UI_INK);
        line.p1 = (lv_point_precise_t){area.x1 + motion->points[i][0].x,
                                     area.y1 + motion->points[i][0].y};
        line.p2 = (lv_point_precise_t){area.x1 + motion->points[i][1].x,
                                     area.y1 + motion->points[i][1].y};
        lv_draw_line(layer, &line);
    }
    lv_draw_rect_dsc_t head;
    lv_draw_rect_dsc_init(&head);
    head.bg_color = lv_color_hex(UI_ORANGE);
    head.radius = LV_RADIUS_CIRCLE;
    lv_area_t circle = {area.x1 + motion->head_x, area.y1 + motion->head_y,
                        area.x1 + motion->head_x + 8, area.y1 + motion->head_y + 8};
    lv_draw_rect(layer, &head, &circle);
}

void health_motion_create(health_motion_t *motion, lv_obj_t *parent, int x, int y)
{
    motion->root = lv_obj_create(parent);
    lv_obj_remove_style_all(motion->root);
    lv_obj_remove_flag(motion->root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(motion->root, x, y);
    lv_obj_set_size(motion->root, 96, 42);
    lv_obj_add_event_cb(motion->root, draw_motion, LV_EVENT_DRAW_MAIN, motion);
    motion->pose = UINT_MAX;
}

void health_motion_show(health_motion_t *motion, unsigned kind, bool moving, uint64_t now)
{
    unsigned phase = moving ? (unsigned)(now / 800 % 2) : 0;
    unsigned pose = kind * 2 + phase;
    if (pose == motion->pose) return;
    motion->pose = pose;
    int hx = 44, hy = 1;
    int joints[6][4] = {
        {48, 12, 48, 25}, {48, 14, 34, 22}, {48, 14, 62, 22},
        {48, 25, 37, 37}, {48, 25, 59, 37}, {16, 40, 80, 40},
    };
    if (kind == 0) {
        hx = 18; hy = phase ? 11 : 9;
        int plank[5][4] = {{30,18,63,25},{30,18,28,36},{28,36,41,36},
                            {63,25,78,36},{63,25,74,36}};
        for (unsigned i = 0; i < 5; i++)
            for (unsigned j = 0; j < 4; j++) joints[i][j] = plank[i][j];
    } else if (kind == 1 && phase) {
        hy = 9;
        int squat[5][4] = {{48,20,51,28},{48,21,29,21},{48,21,32,24},
                            {51,28,37,31},{37,31,44,37}};
        for (unsigned i = 0; i < 5; i++)
            for (unsigned j = 0; j < 4; j++) joints[i][j] = squat[i][j];
    } else if (kind == 2) {
        joints[1][2] = phase ? 28 : 39; joints[1][3] = phase ? 3 : 28;
        joints[2][2] = phase ? 68 : 57; joints[2][3] = phase ? 3 : 28;
        joints[3][2] = phase ? 30 : 44; joints[4][2] = phase ? 66 : 52;
    } else if (kind == 3) {
        hx = phase ? 48 : 39;
        joints[0][0] = phase ? 52 : 43;
        joints[1][2] = phase ? 64 : 30; joints[1][3] = 3;
        joints[2][2] = phase ? 66 : 32; joints[2][3] = 4;
    }
    motion->head_x = hx;
    motion->head_y = hy;
    for (unsigned i = 0; i < 6; i++) {
        motion->points[i][0] = (lv_point_precise_t){joints[i][0], joints[i][1]};
        motion->points[i][1] = (lv_point_precise_t){joints[i][2], joints[i][3]};
    }
    lv_obj_invalidate(motion->root);
}
