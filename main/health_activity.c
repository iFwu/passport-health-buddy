#include "health_activity.h"
#include "plank_audio.h"

#include <stdio.h>
#include <string.h>

static const health_activity_def_t s_activities[] = {
    { "语音跟练", "按计划运动", 0, 0, 0 },
    { "深蹲", "慢下慢起 自重练习", 30, 20, 3 },
    { "开合跳", "轻落地 保持呼吸", 30, 20, 3 },
    { "舒展拉伸", "轻柔伸展 不要憋气", 40, 10, 3 },
};

const health_activity_def_t *health_activity_get(health_activity_id_t id)
{
    if ((unsigned)id > HEALTH_ACTIVITY_STRETCH) id = HEALTH_ACTIVITY_VOICE;
    return &s_activities[id];
}

health_ui_mode_t health_activity_next(health_ui_mode_t mode, bool forward)
{
    if (mode == HEALTH_UI_PORTAL) return forward ? HEALTH_UI_EVERYDAY : HEALTH_UI_HELP;
    if (mode < HEALTH_UI_EVERYDAY || mode > HEALTH_UI_HELP) return HEALTH_UI_EVERYDAY;
    if (forward) return mode == HEALTH_UI_HELP ? HEALTH_UI_EVERYDAY : mode + 1;
    return mode == HEALTH_UI_EVERYDAY ? HEALTH_UI_HELP : mode - 1;
}

health_ui_mode_t health_activity_page(const health_log_t *log)
{
    return log && log->interval_activity >= HEALTH_ACTIVITY_SQUAT &&
           log->interval_activity <= HEALTH_ACTIVITY_STRETCH ?
           (health_ui_mode_t)(HEALTH_UI_HOME + log->interval_activity) : HEALTH_UI_HOME;
}

bool health_activity_plank_busy(const plank_model_t *model)
{
    return model && (model->state == PLANK_PREPARING || model->state == PLANK_RUNNING);
}

unsigned health_activity_confirm(health_ui_mode_t *page, health_log_t *log,
                                  plank_model_t *model, uint64_t now_ms)
{
    if (*page < HEALTH_UI_HOME || *page > HEALTH_UI_STRETCH) return PLANK_EFFECT_NONE;
    if (log && log->interval_on) {
        health_ui_mode_t running = health_activity_page(log);
        if (*page != running) {
            *page = running;
            return HEALTH_SOUND_ERROR;
        }
        log->interval_on = false;
        log->interval_finished = false;
        return PLANK_EFFECT_STOP;
    }
    if (*page == HEALTH_UI_HOME && log && log->interval_finished &&
        log->interval_activity == HEALTH_ACTIVITY_VOICE) {
        log->interval_finished = false;
        return PLANK_EFFECT_NONE;
    }
    if (*page == HEALTH_UI_HOME) return model ? plank_key(model, PLANK_OK, now_ms) : 0;
    if (health_activity_plank_busy(model)) {
        *page = HEALTH_UI_HOME;
        return HEALTH_SOUND_ERROR;
    }
    if (!log) return PLANK_EFFECT_NONE;
    health_activity_id_t id = (health_activity_id_t)(*page - HEALTH_UI_HOME);
    const health_activity_def_t *def = health_activity_get(id);
    if (!health_log_start_interval(log, def->work_s, def->rest_s, def->rounds, now_ms))
        return HEALTH_SOUND_ERROR;
    log->interval_activity = id;
    return PLANK_EFFECT_START;
}

bool health_activity_tool(health_log_t *log, const plank_model_t *model,
                          const char *name, const char *args, uint64_t now_ms,
                          char *out, size_t cap)
{
    if (strcmp(name, "start_interval_plan") == 0 && health_activity_plank_busy(model)) {
        snprintf(out, cap, "{\"ok\":false,\"reason\":\"workout_active\",\"activity\":\"plank\"}");
        return true;
    }
    return health_log_tool(log, name, args, now_ms, out, cap);
}
