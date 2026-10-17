#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLANK_HISTORY_CAPACITY 20
#define PLANK_PREPARE_MS UINT64_C(4000)
#define PLANK_START_SIGNAL_MS UINT64_C(350)
#define PLANK_CUE_MS UINT64_C(10000)

typedef enum { PLANK_READY, PLANK_PREPARING, PLANK_RUNNING,
               PLANK_RESULT, PLANK_HISTORY } plank_state_t;
typedef enum { PLANK_UP, PLANK_DOWN, PLANK_OK } plank_key_t;
typedef enum { PLANK_SAVE_NONE, PLANK_SAVE_PENDING,
               PLANK_SAVE_OK, PLANK_SAVE_FAILED } plank_save_t;
enum { PLANK_EFFECT_NONE = 0, PLANK_EFFECT_START = 1,
       PLANK_EFFECT_STOP = 2, PLANK_EFFECT_CUE_10 = 4,
       PLANK_EFFECT_CUE_30 = 8, PLANK_EFFECT_CUE_60 = 16,
       PLANK_EFFECT_PREP_1 = 32, PLANK_EFFECT_PREP_2 = 64,
       PLANK_EFFECT_PREP_3 = 128, PLANK_EFFECT_PREP_4 = 256,
       PLANK_EFFECT_SAVE = 512 };

typedef struct {
    uint32_t version;
    uint32_t count;
    uint64_t revision;
    uint64_t duration_ms[PLANK_HISTORY_CAPACITY];
} plank_history_t;

typedef struct {
    plank_state_t state;
    plank_save_t save;
    plank_history_t history;
    uint64_t durable_revision;
    uint64_t started_ms;
    uint64_t last_cue;
    uint64_t result_ms;
    unsigned prepare_step;
    unsigned history_index;
    bool sound;
    bool storage_available;
} plank_model_t;

bool plank_history_valid(const plank_history_t *history);
void plank_init(plank_model_t *model, const plank_history_t *history, bool storage_ok);
unsigned plank_key(plank_model_t *model, plank_key_t key, uint64_t now_ms);
unsigned plank_tick(plank_model_t *model, uint64_t now_ms);
void plank_saved(plank_model_t *model, bool success);
uint64_t plank_elapsed(const plank_model_t *model, uint64_t now_ms);
unsigned plank_prepare_seconds(const plank_model_t *model, uint64_t now_ms);
bool plank_record_saved(const plank_model_t *model, unsigned index);
void plank_format_time(uint64_t duration_ms, char *text, size_t size);
