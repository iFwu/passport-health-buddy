#pragma once
#include "lvgl.h"

typedef struct {
    lv_obj_t *obj;
    unsigned value, places, height;
    bool leading_zero;
} plank_digits_t;

void plank_digits_create(plank_digits_t *digits, lv_obj_t *parent,
                         unsigned places, unsigned height, bool leading_zero);
void plank_digits_set(plank_digits_t *digits, unsigned value);
