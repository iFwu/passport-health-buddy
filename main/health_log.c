#include "health_log.h"

#include <stdio.h>
#include <string.h>

static void copy_name(char *dst, const char *src)
{
    size_t i = 0;
    if (!src) src = "";
    while (src[i] && i + 1 < HEALTH_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void health_log_init(health_log_t *log, const health_persist_t *loaded, uint64_t now_ms)
{
    memset(log, 0, sizeof(*log));
    if (loaded && loaded->set_count <= HEALTH_SET_CAP) log->saved = *loaded;
    log->last_stand_ms = now_ms;
    log->last_water_ms = now_ms;
}

void health_log_set_day(health_log_t *log, uint32_t day)
{
    if (day == 0 || day == log->saved.day) return;
    if (log->saved.day != 0) {
        log->saved.water_ml = 0;
        log->saved.set_count = 0;
    }
    log->saved.day = day;
}

bool health_log_add_water(health_log_t *log, uint32_t ml, uint64_t now_ms)
{
    if (ml == 0 || ml > 2000) return false;
    log->undo_water_ml = log->saved.water_ml;
    log->undo_is_set = false;
    log->undo_until_ms = now_ms + HEALTH_UNDO_MS;
    log->saved.water_ml += ml;
    log->last_water_ms = now_ms;
    return true;
}

bool health_log_add_set(health_log_t *log, const char *name, unsigned weight_x10,
                        unsigned reps, uint64_t now_ms)
{
    if (!name || !name[0] || reps == 0 || reps > 100 || weight_x10 > 5000) return false;
    if (log->saved.set_count == HEALTH_SET_CAP) {
        memmove(&log->saved.sets[0], &log->saved.sets[1],
                (HEALTH_SET_CAP - 1) * sizeof(health_set_t));
        log->saved.set_count--;
    }
    health_set_t *set = &log->saved.sets[log->saved.set_count++];
    copy_name(set->name, name);
    set->weight_x10 = (uint16_t)weight_x10;
    set->reps = (uint8_t)reps;
    log->undo_is_set = true;
    log->undo_until_ms = now_ms + HEALTH_UNDO_MS;
    return true;
}

bool health_log_undo(health_log_t *log, uint64_t now_ms)
{
    if (now_ms > log->undo_until_ms || log->undo_until_ms == 0) return false;
    if (log->undo_is_set) {
        if (log->saved.set_count) log->saved.set_count--;
    } else {
        log->saved.water_ml = log->undo_water_ml;
    }
    log->undo_until_ms = 0;
    return true;
}

void health_log_stand(health_log_t *log, uint64_t now_ms)
{
    log->last_stand_ms = now_ms;
    log->snooze_until_ms = 0;
}

void health_log_snooze(health_log_t *log, uint64_t now_ms, unsigned minutes)
{
    if (minutes == 0 || minutes > 180) minutes = 10;
    log->snooze_until_ms = now_ms + (uint64_t)minutes * 60 * 1000;
}

void health_log_rest(health_log_t *log, unsigned seconds, uint64_t now_ms)
{
    if (seconds == 0 || seconds > 600) seconds = HEALTH_REST_DEFAULT_S;
    log->rest_until_ms = now_ms + (uint64_t)seconds * 1000;
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
    return now_ms >= log->last_water_ms + HEALTH_WATER_MS;
}

unsigned health_log_rest_left_s(const health_log_t *log, uint64_t now_ms)
{
    if (now_ms >= log->rest_until_ms) return 0;
    return (unsigned)((log->rest_until_ms - now_ms + 999) / 1000);
}

bool health_log_undo_open(const health_log_t *log, uint64_t now_ms)
{
    return log->undo_until_ms != 0 && now_ms <= log->undo_until_ms;
}

bool health_log_tick(health_log_t *log, uint64_t now_ms)
{
    if (!log->interval_on || now_ms < log->interval_until_ms) return false;
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

static void summary(const health_log_t *log, uint64_t now_ms, char *out, size_t cap)
{
    snprintf(out, cap,
             "{\"ok\":true,\"water_ml\":%u,\"sets\":%u,\"rest_s\":%u,\"since_stand_min\":%u}",
             (unsigned)log->saved.water_ml, (unsigned)log->saved.set_count,
             health_log_rest_left_s(log, now_ms),
             (unsigned)((now_ms - log->last_stand_ms) / 60000));
}

bool health_log_tool(health_log_t *log, const char *name, const char *args,
                     uint64_t now_ms, char *out, size_t cap)
{
    unsigned value = 0;
    if (strcmp(name, "log_water") == 0) {
        if (!json_uint(args, "ml", &value) || !health_log_add_water(log, value, now_ms)) return false;
    } else if (strcmp(name, "log_set") == 0) {
        char exercise[HEALTH_NAME_MAX];
        unsigned reps = 0;
        json_string(args, "exercise", exercise, sizeof(exercise));
        json_uint(args, "reps", &reps);
        if (!health_log_add_set(log, exercise, json_weight_x10(args), reps, now_ms)) return false;
        health_log_rest(log, HEALTH_REST_DEFAULT_S, now_ms);
    } else if (strcmp(name, "start_rest_timer") == 0) {
        json_uint(args, "seconds", &value);
        health_log_rest(log, value, now_ms);
    } else if (strcmp(name, "start_interval_plan") == 0) {
        unsigned work = 0, rest = 0, rounds = 0;
        json_uint(args, "work_s", &work);
        json_uint(args, "rest_s", &rest);
        json_uint(args, "rounds", &rounds);
        if (!health_log_start_interval(log, work, rest, rounds, now_ms)) return false;
    } else if (strcmp(name, "snooze_reminder") == 0) {
        json_uint(args, "minutes", &value);
        health_log_snooze(log, now_ms, value);
    } else if (strcmp(name, "query_today") != 0) {
        return false;
    }
    summary(log, now_ms, out, cap);
    return true;
}
