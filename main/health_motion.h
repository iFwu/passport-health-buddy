#pragma once
#include "lvgl.h"

/* One draw-event widget, six persistent segments and a circle. */
typedef struct {
    lv_obj_t *root;
    lv_point_precise_t points[6][2];
    int head_x, head_y;
    unsigned pose;
} health_motion_t;

void health_motion_create(health_motion_t *motion, lv_obj_t *parent, int x, int y);
/* kind: 0 plank, 1 squat, 2 jumping jacks, 3 stretch, 4 generic voice plan. */
void health_motion_show(health_motion_t *motion, unsigned kind, bool moving, uint64_t now);
