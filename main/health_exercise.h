#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HEALTH_CAT_FREE_WEIGHT = 0,
    HEALTH_CAT_MACHINE,
    HEALTH_CAT_CABLE,
    HEALTH_CAT_BODYWEIGHT,
    HEALTH_CAT_COUNT,
} health_category_t;

typedef enum {
    HEALTH_EX_BENCH_PRESS = 0,
    HEALTH_EX_INCLINE_PRESS,
    HEALTH_EX_SQUAT,
    HEALTH_EX_DEADLIFT,
    HEALTH_EX_ROMANIAN_DEADLIFT,
    HEALTH_EX_OVERHEAD_PRESS,
    HEALTH_EX_BARBELL_ROW,
    HEALTH_EX_DUMBBELL_ROW,
    HEALTH_EX_CURL,
    HEALTH_EX_LATERAL_RAISE,
    HEALTH_EX_HIP_THRUST,
    HEALTH_EX_LUNGE,
    HEALTH_EX_LAT_PULLDOWN,
    HEALTH_EX_SEATED_ROW,
    HEALTH_EX_LEG_PRESS,
    HEALTH_EX_LEG_EXTENSION,
    HEALTH_EX_LEG_CURL,
    HEALTH_EX_CHEST_FLY,
    HEALTH_EX_TRICEPS_PUSHDOWN,
    HEALTH_EX_PULL_UP,
    HEALTH_EX_PUSH_UP,
    HEALTH_EX_DIP,
    HEALTH_EX_CRUNCH,
    HEALTH_EX_KNOWN_COUNT,
    HEALTH_EX_OTHER = 255,
} health_exercise_id_t;

typedef struct {
    const char *id;
    const char *name_zh;
    const char *name_en;
    health_category_t default_cat;
} health_exercise_def_t;

const health_exercise_def_t *health_exercise_get(uint8_t index);
uint8_t health_exercise_find(const char *id);
const char *health_category_name_zh(uint8_t cat);
uint8_t health_category_find(const char *cat_str);
void health_exercise_sanitize_en(const char *in, char *out, size_t cap);
