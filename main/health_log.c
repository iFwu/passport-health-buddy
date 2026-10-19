#include "health_log.h"
#include "health_exercise.h"

#include <stdio.h>
#include <string.h>

void health_log_init(health_log_t *log, const health_persist_t *loaded, uint64_t now_ms)
{
    memset(log, 0, sizeof(*log));
    if (loaded && loaded->entry_count <= HEALTH_ENTRY_CAP) {
        log->saved = *loaded;
    }
    if (log->saved.goal_ml == 0) log->saved.goal_ml = 2000;
    log->last_stand_ms = now_ms;
    log->last_water_ms = now_ms;
    log->last_water_reminder_ms = now_ms;
}

void health_log_set_day(health_log_t *log, uint32_t day)
{
    if (day == 0 || day == log->saved.day) return;
    if (log->saved.day != 0) {
        log->saved.water_ml = 0;
        log->saved.entry_count = 0;
    }
    log->saved.day = day;
    log->revision++;
}

bool health_log_add_water(health_log_t *log, uint32_t ml, uint64_t now_ms)
{
    if (ml == 0 || ml > 2000) return false;
    log->undo_water_ml = log->saved.water_ml;
    log->undo_is_entry = false;
    log->undo_was_replace = false;
    log->undo_until_ms = now_ms + HEALTH_UNDO_MS;
    log->saved.water_ml += ml;
    log->last_water_ms = now_ms;
    log->last_water_reminder_ms = now_ms;
    log->revision++;
    return true;
}

bool health_log_add_entry(health_log_t *log, const health_entry_t *entry,
                          bool replace_last, uint64_t now_ms)
{
    if (!entry || entry->reps == 0 || entry->reps > 100 || entry->sets == 0 ||
        entry->sets > 10 || entry->weight_x10 > 5000) {
        return false;
    }
    if (replace_last && log->saved.entry_count > 0) {
        log->undo_entry = log->saved.entries[log->saved.entry_count - 1];
        log->undo_was_replace = true;
        log->undo_is_entry = true;
        log->undo_until_ms = now_ms + HEALTH_UNDO_MS;
        log->saved.entries[log->saved.entry_count - 1] = *entry;
    } else {
        if (log->saved.entry_count == HEALTH_ENTRY_CAP) {
            memmove(&log->saved.entries[0], &log->saved.entries[1],
                    (HEALTH_ENTRY_CAP - 1) * sizeof(health_entry_t));
            log->saved.entry_count--;
        }
        log->undo_entry = *entry;
        log->undo_was_replace = false;
        log->undo_is_entry = true;
        log->undo_until_ms = now_ms + HEALTH_UNDO_MS;
        log->saved.entries[log->saved.entry_count++] = *entry;
    }
    if (entry->exercise < HEALTH_EX_KNOWN_COUNT) {
        log->saved.last_weight_x10[entry->exercise] = entry->weight_x10;
        log->saved.last_reps[entry->exercise] = entry->reps;
    }
    log->revision++;
    return true;
}


bool health_log_undo_voice(health_log_t *log)
{
    if (log->undo_is_entry) {
        if (log->undo_was_replace) {
            if (log->saved.entry_count > 0) {
                log->saved.entries[log->saved.entry_count - 1] = log->undo_entry;
                log->undo_until_ms = 0;
                log->undo_is_entry = false;
                log->undo_was_replace = false;
                log->revision++;
                return true;
            }
        } else {
            if (log->saved.entry_count > 0) {
                log->saved.entry_count--;
                log->undo_until_ms = 0;
                log->undo_is_entry = false;
                log->revision++;
                return true;
            }
        }
    } else if (log->undo_until_ms != 0) {
        log->saved.water_ml = log->undo_water_ml;
        log->undo_until_ms = 0;
        log->revision++;
        return true;
    } else if (log->saved.entry_count > 0) {
        log->saved.entry_count--;
        log->revision++;
        return true;
    }
    return false;
}


void health_log_snooze(health_log_t *log, uint64_t now_ms, unsigned minutes)
{
    if (minutes == 0 || minutes > 180) minutes = 10;
    log->snooze_until_ms = now_ms + (uint64_t)minutes * 60 * 1000;
}

void health_log_rest(health_log_t *log, unsigned seconds, uint64_t now_ms)
{
    if (seconds == 0 || seconds > 3600) seconds = HEALTH_REST_DEFAULT_S;
    log->rest_until_ms = now_ms + (uint64_t)seconds * 1000;
    log->timer_running = true;
}

bool health_log_start_interval(health_log_t *log, unsigned work_s, unsigned rest_s,
                               unsigned rounds, uint64_t now_ms)
{
    if (work_s == 0 || work_s > 600 || rest_s > 600 || rounds == 0 || rounds > 40) return false;
    log->interval_on = true;
    log->interval_work = true;
    log->interval_work_s = (uint16_t)work_s;
    log->interval_rest_s = (uint16_t)rest_s;
    log->interval_rounds = (uint16_t)rounds;
    log->interval_round = 1;
    log->interval_until_ms = now_ms + (uint64_t)work_s * 1000;
    return true;
}

bool health_log_sitting_due(const health_log_t *log, uint64_t now_ms)
{
    if (log->snooze_until_ms && now_ms < log->snooze_until_ms) return false;
    return now_ms >= log->last_stand_ms + HEALTH_SIT_MS;
}

bool health_log_water_due(const health_log_t *log, uint64_t now_ms)
{
    if (log->snooze_until_ms && now_ms < log->snooze_until_ms) return false;
    return now_ms >= log->last_water_reminder_ms + HEALTH_WATER_MS;
}

unsigned health_log_take_reminders(health_log_t *log, uint64_t now_ms)
{
    unsigned reminders = 0;
    if (health_log_sitting_due(log, now_ms)) {
        reminders |= HEALTH_REMINDER_ACTIVITY;
        log->last_stand_ms = now_ms;
    }
    if (health_log_water_due(log, now_ms)) {
        reminders |= HEALTH_REMINDER_WATER;
        log->last_water_reminder_ms = now_ms;
    }
    return reminders;
}

unsigned health_log_rest_left_s(const health_log_t *log, uint64_t now_ms)
{
    if (now_ms >= log->rest_until_ms) return 0;
    return (unsigned)((log->rest_until_ms - now_ms + 999) / 1000);
}


uint32_t health_log_total_volume_kg(const health_log_t *log)
{
    uint32_t vol = 0;
    for (uint8_t i = 0; i < log->saved.entry_count; i++) {
        const health_entry_t *e = &log->saved.entries[i];
        vol += ((uint32_t)e->weight_x10 / 10u) * (uint32_t)e->reps * (uint32_t)e->sets;
    }
    return vol;
}

uint32_t health_log_total_sets(const health_log_t *log)
{
    uint32_t sets = 0;
    for (uint8_t i = 0; i < log->saved.entry_count; i++) {
        sets += log->saved.entries[i].sets;
    }
    return sets;
}

bool health_log_tick(health_log_t *log, uint64_t now_ms)
{
    bool alert = false;
    if (log->timer_running && now_ms >= log->rest_until_ms) {
        log->timer_running = false;
        alert = true;
    }
    if (!log->interval_on || now_ms < log->interval_until_ms) return alert;
    if (log->interval_work) {
        log->interval_work = false;
        if (log->interval_rest_s == 0) {
            if (log->interval_round >= log->interval_rounds) {
                log->interval_on = false;
                return true;
            }
            log->interval_round++;
            log->interval_work = true;
            log->interval_until_ms = now_ms + (uint64_t)log->interval_work_s * 1000;
            return true;
        }
        log->interval_until_ms = now_ms + (uint64_t)log->interval_rest_s * 1000;
        return true;
    }
    if (log->interval_round >= log->interval_rounds) {
        log->interval_on = false;
        return true;
    }
    log->interval_round++;
    log->interval_work = true;
    log->interval_until_ms = now_ms + (uint64_t)log->interval_work_s * 1000;
    return true;
}

static const char *find_key(const char *args, const char *key)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *hit = args ? strstr(args, pat) : NULL;
    if (!hit) return NULL;
    hit += strlen(pat);
    while (*hit == ' ' || *hit == '\t' || *hit == ':') hit++;
    return hit;
}

static bool json_uint(const char *args, const char *key, unsigned *out)
{
    const char *hit = find_key(args, key);
    if (!hit || *hit < '0' || *hit > '9') return false;
    unsigned value = 0;
    while (*hit >= '0' && *hit <= '9') value = value * 10u + (unsigned)(*hit++ - '0');
    *out = value;
    return true;
}

static bool json_bool(const char *args, const char *key, bool *out)
{
    const char *hit = find_key(args, key);
    if (!hit) return false;
    if (strncmp(hit, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(hit, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static unsigned json_weight_x10(const char *args)
{
    const char *hit = find_key(args, "weight_kg");
    if (!hit || *hit < '0' || *hit > '9') return 0;
    unsigned whole = 0;
    while (*hit >= '0' && *hit <= '9') whole = whole * 10u + (unsigned)(*hit++ - '0');
    unsigned frac = 0;
    if (*hit == '.' && hit[1] >= '0' && hit[1] <= '9') frac = (unsigned)(hit[1] - '0');
    return whole * 10u + frac;
}

static void json_string(const char *args, const char *key, char *out, size_t cap)
{
    out[0] = '\0';
    const char *hit = find_key(args, key);
    if (!hit || *hit != '"') return;
    hit++;
    size_t n = 0;
    while (*hit && *hit != '"' && n + 1 < cap) out[n++] = *hit++;
    out[n] = '\0';
}

static void format_entry_desc(const health_entry_t *e, char *out, size_t cap)
{
    const char *name = (e->exercise < HEALTH_EX_KNOWN_COUNT) ?
        health_exercise_get(e->exercise)->id : e->name_en;
    if (e->weight_x10 == 0) {
        snprintf(out, cap, "%s 0x%ux%u", name, (unsigned)e->reps, (unsigned)e->sets);
    } else {
        snprintf(out, cap, "%s %u.%ux%ux%u", name,
                 (unsigned)(e->weight_x10 / 10), (unsigned)(e->weight_x10 % 10),
                 (unsigned)e->reps, (unsigned)e->sets);
    }
}

static void summary(const health_log_t *log, uint64_t now_ms, uint16_t prev_weight,
                    uint8_t prev_reps, const char *prev_name, char *out, size_t cap)
{
    char last_str[64] = "";
    if (log->saved.entry_count > 0) {
        format_entry_desc(&log->saved.entries[log->saved.entry_count - 1], last_str, sizeof(last_str));
    }
    char prev_str[48] = "";
    if (prev_name && prev_name[0] && prev_reps > 0) {
        if (prev_weight == 0) {
            snprintf(prev_str, sizeof(prev_str), ",\"prev\":\"%s 0x%u\"", prev_name, (unsigned)prev_reps);
        } else {
            snprintf(prev_str, sizeof(prev_str), ",\"prev\":\"%s %u.%ux%u\"", prev_name,
                     (unsigned)(prev_weight / 10), (unsigned)(prev_weight % 10), (unsigned)prev_reps);
        }
    }
    snprintf(out, cap,
             "{\"ok\":true,\"water_ml\":%u,\"goal_ml\":%u,\"sets\":%u,\"volume_kg\":%u"
             ",\"last\":\"%s\"%s,\"rest_s\":%u,\"activity_reminder_in_min\":%u}",
             (unsigned)log->saved.water_ml, (unsigned)log->saved.goal_ml,
             (unsigned)health_log_total_sets(log), (unsigned)health_log_total_volume_kg(log),
             last_str, prev_str,
             health_log_rest_left_s(log, now_ms),
             (unsigned)(now_ms < log->last_stand_ms + HEALTH_SIT_MS ?
                 (log->last_stand_ms + HEALTH_SIT_MS - now_ms + 59999) / 60000 : 0));
}

bool health_log_tool(health_log_t *log, const char *name, const char *args,
                     uint64_t now_ms, char *out, size_t cap)
{
    unsigned value = 0;
    uint16_t prev_w = 0;
    uint8_t prev_r = 0;
    const char *prev_n = NULL;

    if (strcmp(name, "log_workout") == 0) {
        char ex_str[32] = "";
        char cat_str[16] = "";
        char name_en[32] = "";
        unsigned reps = 0;
        unsigned sets = 1;
        unsigned rest_s = HEALTH_REST_DEFAULT_S;
        bool replace_last = false;

        json_string(args, "exercise", ex_str, sizeof(ex_str));
        json_string(args, "category", cat_str, sizeof(cat_str));
        json_string(args, "name_en", name_en, sizeof(name_en));
        json_uint(args, "reps", &reps);
        json_uint(args, "sets", &sets);
        if (json_uint(args, "rest_s", &rest_s) && rest_s == 0) {
            rest_s = HEALTH_REST_DEFAULT_S;
        }
        json_bool(args, "replace_last", &replace_last);

        uint8_t ex_id = health_exercise_find(ex_str);
        uint8_t cat_id = (ex_id < HEALTH_EX_KNOWN_COUNT) ?
            health_exercise_get(ex_id)->default_cat : health_category_find(cat_str);

        if (ex_id < HEALTH_EX_KNOWN_COUNT) {
            prev_w = log->saved.last_weight_x10[ex_id];
            prev_r = log->saved.last_reps[ex_id];
            prev_n = health_exercise_get(ex_id)->id;
        }

        health_entry_t entry = {
            .exercise = ex_id,
            .category = cat_id,
            .reps = (uint8_t)reps,
            .sets = (uint8_t)sets,
            .weight_x10 = (uint16_t)json_weight_x10(args),
        };
        health_exercise_sanitize_en(name_en, entry.name_en, sizeof(entry.name_en));

        if (!health_log_add_entry(log, &entry, replace_last, now_ms)) {
            snprintf(out, cap, "{\"ok\":false,\"reason\":\"invalid_workout_args\"}");
            return true;
        }
        health_log_rest(log, rest_s, now_ms);
    } else if (strcmp(name, "log_water") == 0) {
        if (!json_uint(args, "ml", &value) || !health_log_add_water(log, value, now_ms)) {
            snprintf(out, cap, "{\"ok\":false,\"reason\":\"invalid_water_ml\"}");
            return true;
        }
    } else if (strcmp(name, "start_timer") == 0 || strcmp(name, "start_rest_timer") == 0) {
        if (!json_uint(args, "seconds", &value)) value = HEALTH_REST_DEFAULT_S;
        health_log_rest(log, value, now_ms);
    } else if (strcmp(name, "start_interval_plan") == 0) {
        unsigned work = 0, rest = 0, rounds = 0;
        json_uint(args, "work_s", &work);
        json_uint(args, "rest_s", &rest);
        json_uint(args, "rounds", &rounds);
        if (!health_log_start_interval(log, work, rest, rounds, now_ms)) {
            snprintf(out, cap, "{\"ok\":false,\"reason\":\"invalid_interval_args\"}");
            return true;
        }
        log->mode_request = 3; // home
    } else if (strcmp(name, "set_mode") == 0) {
        char m[16];
        json_string(args, "mode", m, sizeof(m));
        if (strcmp(m, "office") == 0 || strcmp(m, "everyday") == 0) log->mode_request = 1;
        else if (strcmp(m, "gym") == 0) log->mode_request = 2;
        else if (strcmp(m, "home") == 0) log->mode_request = 3;
    } else if (strcmp(name, "undo_last") == 0) {
        health_log_undo_voice(log);
    } else if (strcmp(name, "snooze_reminder") == 0) {
        json_uint(args, "minutes", &value);
        health_log_snooze(log, now_ms, value);
    } else if (strcmp(name, "query_today") != 0) {
        return false;
    }

    summary(log, now_ms, prev_w, prev_r, prev_n, out, cap);
    return true;
}
