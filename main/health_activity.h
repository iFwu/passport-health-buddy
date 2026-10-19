#pragma once

#include "health_log.h"
#include "plank_core.h"

typedef enum {
    HEALTH_UI_PORTAL,
    HEALTH_UI_MENU,
    HEALTH_UI_EVERYDAY,
    HEALTH_UI_GYM,
    HEALTH_UI_HOME,
    HEALTH_UI_SQUAT,
    HEALTH_UI_JACKS,
    HEALTH_UI_STRETCH,
    HEALTH_UI_HELP,
} health_ui_mode_t;

typedef enum {
    HEALTH_ACTIVITY_VOICE,
    HEALTH_ACTIVITY_SQUAT,
    HEALTH_ACTIVITY_JACKS,
    HEALTH_ACTIVITY_STRETCH,
} health_activity_id_t;

typedef struct {
    const char *name;
    const char *hint;
    unsigned work_s, rest_s, rounds;
} health_activity_def_t;

const health_activity_def_t *health_activity_get(health_activity_id_t id);
health_ui_mode_t health_activity_next(health_ui_mode_t mode, bool forward);
health_ui_mode_t health_activity_page(const health_log_t *log);
bool health_activity_plank_busy(const plank_model_t *model);
/* A conflict returns the running workout's page without stopping/replacing it.
 * A second OK on that page stops it. Gym rest remains independent. */
unsigned health_activity_confirm(health_ui_mode_t *page, health_log_t *log,
                                  plank_model_t *model, uint64_t now_ms);
/* Voice plans never replace a running workout, including plank preparation. */
bool health_activity_tool(health_log_t *log, const plank_model_t *model,
                          const char *name, const char *args, uint64_t now_ms,
                          char *out, size_t cap);
