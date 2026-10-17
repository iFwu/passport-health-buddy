#include "plank_core.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(plank_history_t) == 176, "NVS history v1 layout changed");

bool plank_history_valid(const plank_history_t *history)
{
    return history->version == 1 && history->count <= PLANK_HISTORY_CAPACITY &&
           history->revision >= history->count && history->revision < UINT64_MAX &&
           (history->count == PLANK_HISTORY_CAPACITY || history->revision == history->count);
}

void plank_init(plank_model_t *model, const plank_history_t *history, bool storage_ok)
{
    memset(model, 0, sizeof(*model));
    model->sound = true;
    model->history.version = 1;
    model->storage_available = storage_ok && (!history || plank_history_valid(history));
    if (model->storage_available && history) {
        model->history = *history;
        model->durable_revision = history->revision;
    }
}

uint64_t plank_elapsed(const plank_model_t *model, uint64_t now_ms)
{
    if (model->state != PLANK_RUNNING) return model->result_ms;
    return now_ms >= model->started_ms ? now_ms - model->started_ms : 0;
}

unsigned plank_prepare_seconds(const plank_model_t *model, uint64_t now_ms)
{
    uint64_t elapsed = now_ms >= model->started_ms ? now_ms - model->started_ms : 0;
    return elapsed >= PLANK_PREPARE_MS ? 0 :
           (unsigned)((PLANK_PREPARE_MS - elapsed + 999) / 1000);
}

static unsigned request_save(plank_model_t *model)
{
    if (!model->storage_available) {
        model->save = PLANK_SAVE_FAILED;
        return PLANK_EFFECT_NONE;
    }
    model->save = PLANK_SAVE_PENDING;
    return PLANK_EFFECT_SAVE;
}

unsigned plank_key(plank_model_t *model, plank_key_t key, uint64_t now_ms)
{
    if (model->state == PLANK_PREPARING) {
        model->state = PLANK_READY;
        return PLANK_EFFECT_NONE;
    }
    if (model->state == PLANK_RUNNING) {
        model->result_ms = plank_elapsed(model, now_ms);
        model->state = PLANK_RESULT;
        memmove(&model->history.duration_ms[1], &model->history.duration_ms[0],
                (PLANK_HISTORY_CAPACITY - 1) * sizeof(uint64_t));
        model->history.duration_ms[0] = model->result_ms;
        if (model->history.count < PLANK_HISTORY_CAPACITY) model->history.count++;
        model->history.revision++;
        return PLANK_EFFECT_STOP | request_save(model);
    }
    if (model->save == PLANK_SAVE_PENDING) return PLANK_EFFECT_NONE;
    if (model->state == PLANK_HISTORY) {
        if (key == PLANK_OK) model->state = PLANK_READY;
        if (key == PLANK_UP && model->history_index > 0) model->history_index--;
        if (key == PLANK_DOWN && model->history_index + 1 < model->history.count)
            model->history_index++;
        return PLANK_EFFECT_NONE;
    }
    if (key == PLANK_OK) {
        model->state = PLANK_PREPARING;
        model->started_ms = now_ms;
        model->result_ms = 0;
        model->prepare_step = 1;
        return PLANK_EFFECT_PREP_1;
    } else if (key == PLANK_DOWN) {
        model->state = PLANK_HISTORY;
        model->history_index = 0;
    } else if (model->history.revision > model->durable_revision && model->storage_available) {
        return request_save(model);
    } else {
        model->sound = !model->sound;
    }
    return PLANK_EFFECT_NONE;
}

unsigned plank_tick(plank_model_t *model, uint64_t now_ms)
{
    if (model->state == PLANK_PREPARING) {
        if (plank_prepare_seconds(model, now_ms) == 0) {
            model->state = PLANK_RUNNING;
            // Start when the transition/cue is dispatched, never backdate a delayed tick.
            model->started_ms = now_ms;
            model->last_cue = 0;
            return PLANK_EFFECT_START;
        }
        uint64_t elapsed = now_ms >= model->started_ms ? now_ms - model->started_ms : 0;
        unsigned step = (unsigned)(elapsed / 1000) + 1;
        if (step > model->prepare_step && step <= 4) {
            model->prepare_step = step;
            return PLANK_EFFECT_PREP_1 << (step - 1);
        }
    }
    if (model->state == PLANK_RUNNING) {
        uint64_t cue = plank_elapsed(model, now_ms) / PLANK_CUE_MS;
        if (cue > model->last_cue) {
            model->last_cue = cue;
            if (cue % 6 == 0) return PLANK_EFFECT_CUE_60;
            if (cue % 3 == 0) return PLANK_EFFECT_CUE_30;
            return PLANK_EFFECT_CUE_10;
        }
    }
    return PLANK_EFFECT_NONE;
}

void plank_saved(plank_model_t *model, bool success)
{
    if (model->save != PLANK_SAVE_PENDING) return;
    model->save = success ? PLANK_SAVE_OK : PLANK_SAVE_FAILED;
    if (success) model->durable_revision = model->history.revision;
}

bool plank_record_saved(const plank_model_t *model, unsigned index)
{
    return index < model->history.count &&
           model->history.revision - index <= model->durable_revision;
}

void plank_format_time(uint64_t duration_ms, char *text, size_t size)
{
    uint64_t seconds = duration_ms / 1000;
    if (seconds < 3600) {
        snprintf(text, size, "%02" PRIu64 ":%02" PRIu64 ".%02u", seconds / 60,
                 seconds % 60, (unsigned)(duration_ms % 1000 / 10));
    } else if (seconds < 360000) {
        snprintf(text, size, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
                 seconds / 3600, seconds / 60 % 60, seconds % 60);
    } else {
        snprintf(text, size, "99h+");
    }
}
