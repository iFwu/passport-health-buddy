#include "health_exercise.h"

#include <string.h>

static const health_exercise_def_t k_exercises[HEALTH_EX_KNOWN_COUNT] = {
    [HEALTH_EX_BENCH_PRESS]        = { "bench_press",        "卧推",       "Bench Press",    HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_INCLINE_PRESS]      = { "incline_press",      "上斜卧推",   "Incline Press",  HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_SQUAT]              = { "squat",              "深蹲",       "Squat",          HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_DEADLIFT]           = { "deadlift",           "硬拉",       "Deadlift",       HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_ROMANIAN_DEADLIFT]  = { "romanian_deadlift",  "罗马尼亚硬拉", "RDL",          HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_OVERHEAD_PRESS]     = { "overhead_press",     "推举",       "Overhead Press", HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_BARBELL_ROW]        = { "barbell_row",        "杠铃划船",   "Barbell Row",    HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_DUMBBELL_ROW]       = { "dumbbell_row",       "哑铃划船",   "Dumbbell Row",   HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_CURL]               = { "curl",               "弯举",       "Biceps Curl",    HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_LATERAL_RAISE]      = { "lateral_raise",      "侧平举",     "Lateral Raise",  HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_HIP_THRUST]         = { "hip_thrust",         "臀推",       "Hip Thrust",     HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_LUNGE]              = { "lunge",              "箭步蹲",     "Lunge",          HEALTH_CAT_FREE_WEIGHT },
    [HEALTH_EX_LAT_PULLDOWN]       = { "lat_pulldown",       "高位下拉",   "Lat Pulldown",   HEALTH_CAT_MACHINE },
    [HEALTH_EX_SEATED_ROW]         = { "seated_row",         "坐姿划船",   "Seated Row",     HEALTH_CAT_MACHINE },
    [HEALTH_EX_LEG_PRESS]          = { "leg_press",          "腿举",       "Leg Press",      HEALTH_CAT_MACHINE },
    [HEALTH_EX_LEG_EXTENSION]      = { "leg_extension",      "腿屈伸",     "Leg Extension",  HEALTH_CAT_MACHINE },
    [HEALTH_EX_LEG_CURL]           = { "leg_curl",           "腿弯举",     "Leg Curl",       HEALTH_CAT_MACHINE },
    [HEALTH_EX_CHEST_FLY]          = { "chest_fly",          "夹胸",       "Chest Fly",      HEALTH_CAT_MACHINE },
    [HEALTH_EX_TRICEPS_PUSHDOWN]   = { "triceps_pushdown",   "三头下压",   "Pushdown",       HEALTH_CAT_CABLE },
    [HEALTH_EX_PULL_UP]            = { "pull_up",            "引体向上",   "Pull-up",        HEALTH_CAT_BODYWEIGHT },
    [HEALTH_EX_PUSH_UP]            = { "push_up",            "俯卧撑",     "Push-up",        HEALTH_CAT_BODYWEIGHT },
    [HEALTH_EX_DIP]                = { "dip",                "臂屈伸",     "Dip",            HEALTH_CAT_BODYWEIGHT },
    [HEALTH_EX_CRUNCH]             = { "crunch",             "卷腹",       "Crunch",         HEALTH_CAT_BODYWEIGHT },
};

static const char *k_cat_names[HEALTH_CAT_COUNT] = {
    [HEALTH_CAT_FREE_WEIGHT] = "自由重量",
    [HEALTH_CAT_MACHINE]     = "器械",
    [HEALTH_CAT_CABLE]       = "绳索",
    [HEALTH_CAT_BODYWEIGHT]  = "自重",
};

const health_exercise_def_t *health_exercise_get(uint8_t index)
{
    if (index >= HEALTH_EX_KNOWN_COUNT) return NULL;
    return &k_exercises[index];
}

uint8_t health_exercise_find(const char *id)
{
    if (!id || !id[0]) return HEALTH_EX_OTHER;
    for (uint8_t i = 0; i < HEALTH_EX_KNOWN_COUNT; i++) {
        if (strcmp(k_exercises[i].id, id) == 0) return i;
    }
    return HEALTH_EX_OTHER;
}

const char *health_category_name_zh(uint8_t cat)
{
    if (cat >= HEALTH_CAT_COUNT) return "力量训练";
    return k_cat_names[cat];
}

uint8_t health_category_find(const char *cat_str)
{
    if (!cat_str) return HEALTH_CAT_FREE_WEIGHT;
    if (strcmp(cat_str, "machine") == 0) return HEALTH_CAT_MACHINE;
    if (strcmp(cat_str, "cable") == 0) return HEALTH_CAT_CABLE;
    if (strcmp(cat_str, "bodyweight") == 0) return HEALTH_CAT_BODYWEIGHT;
    return HEALTH_CAT_FREE_WEIGHT;
}

void health_exercise_sanitize_en(const char *in, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    if (!in || !in[0]) {
        strlcpy(out, "Workout", cap);
        return;
    }
    size_t w = 0;
    bool last_space = true;
    for (size_t i = 0; in[i] && w + 1 < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c >= 0x20 && c <= 0x7E) {
            if (c == ' ') {
                if (!last_space && w + 1 < cap) {
                    out[w++] = ' ';
                    last_space = true;
                }
            } else {
                out[w++] = (char)c;
                last_space = false;
            }
        }
    }
    while (w > 0 && out[w - 1] == ' ') w--;
    out[w] = '\0';
    if (w == 0) strlcpy(out, "Workout", cap);
}
