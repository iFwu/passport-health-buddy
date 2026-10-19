#pragma once

#include "health_exercise.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HEALTH_ENTRY_CAP 16
#define HEALTH_SIT_MS (UINT64_C(45) * 60 * 1000)
#define HEALTH_WATER_MS (UINT64_C(60) * 60 * 1000)
#define HEALTH_UNDO_MS 5000
#define HEALTH_REST_DEFAULT_S 90

enum { HEALTH_REMINDER_ACTIVITY = 1, HEALTH_REMINDER_WATER = 2 };
typedef struct {
    uint8_t exercise;   // index into catalogue, or HEALTH_EX_OTHER
    uint8_t category;   // health_category_t
    uint8_t reps;
    uint8_t sets;
    uint16_t weight_x10;
    char name_en[18];   // only used for other
} health_entry_t;

typedef struct {
    uint32_t day;
    uint32_t water_ml;
    uint32_t goal_ml;
    uint8_t entry_count;
    health_entry_t entries[HEALTH_ENTRY_CAP];
    uint16_t last_weight_x10[HEALTH_EX_KNOWN_COUNT];
    uint8_t last_reps[HEALTH_EX_KNOWN_COUNT];
} health_persist_t;

typedef struct {
    health_persist_t saved;
    uint32_t revision;
    uint64_t last_water_reminder_ms;
    uint64_t last_stand_ms;
    uint64_t last_water_ms;
    uint64_t snooze_until_ms;
    uint64_t rest_until_ms;
    bool timer_running;
    uint64_t undo_until_ms;
    uint32_t undo_water_ml;
    health_entry_t undo_entry;
    bool undo_is_entry;
    bool undo_was_replace;
    uint8_t mode_request; // 0: none, 1: everyday, 2: gym, 3: home
    bool interval_on;
    bool interval_finished;
    uint8_t interval_activity; // 0: voice plan; 1/2/3: squat/jacks/stretch (RAM only)
    bool interval_work;
    uint16_t interval_work_s;
    uint16_t interval_rest_s;
    uint16_t interval_rounds;
    uint16_t interval_round;
    uint64_t interval_until_ms;
} health_log_t;

void health_log_init(health_log_t *log, const health_persist_t *loaded, uint64_t now_ms);
void health_log_set_day(health_log_t *log, uint32_t day);

bool health_log_add_water(health_log_t *log, uint32_t ml, uint64_t now_ms);
bool health_log_add_entry(health_log_t *log, const health_entry_t *entry,
                          bool replace_last, uint64_t now_ms);
bool health_log_undo_voice(health_log_t *log);
void health_log_snooze(health_log_t *log, uint64_t now_ms, unsigned minutes);
void health_log_rest(health_log_t *log, unsigned seconds, uint64_t now_ms);
bool health_log_start_interval(health_log_t *log, unsigned work_s, unsigned rest_s,
                               unsigned rounds, uint64_t now_ms);

bool health_log_sitting_due(const health_log_t *log, uint64_t now_ms);
bool health_log_water_due(const health_log_t *log, uint64_t now_ms);
unsigned health_log_take_reminders(health_log_t *log, uint64_t now_ms);
unsigned health_log_rest_left_s(const health_log_t *log, uint64_t now_ms);

uint32_t health_log_total_volume_kg(const health_log_t *log);
uint32_t health_log_total_sets(const health_log_t *log);

bool health_log_tick(health_log_t *log, uint64_t now_ms);
bool health_log_tool(health_log_t *log, const char *name, const char *args,
                     uint64_t now_ms, char *out, size_t cap);
