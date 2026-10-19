// 健康记录：喝水、训练组、久坐、组间休息、间歇计划。不依赖 ESP-IDF，可 host 测试。
// 没有可信时钟时 day 保持 0，累计值跨重启保留，不标成「今天」。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HEALTH_SET_CAP 12
#define HEALTH_NAME_MAX 16
#define HEALTH_SIT_MS (UINT64_C(45) * 60 * 1000)
#define HEALTH_WATER_MS (UINT64_C(60) * 60 * 1000)
#define HEALTH_UNDO_MS 5000
#define HEALTH_REST_DEFAULT_S 90

typedef struct {
    char name[HEALTH_NAME_MAX];
    uint16_t weight_x10;
    uint8_t reps;
} health_set_t;

typedef struct {
    uint32_t day;
    uint32_t water_ml;
    uint8_t set_count;
    health_set_t sets[HEALTH_SET_CAP];
} health_persist_t;

typedef struct {
    health_persist_t saved;
    uint64_t last_stand_ms;
    uint64_t last_water_ms;
    uint64_t snooze_until_ms;
    uint64_t rest_until_ms;
    uint64_t undo_until_ms;
    uint32_t undo_water_ml;
    bool undo_is_set;
    bool interval_on;
    bool interval_work;
    uint16_t interval_work_s;
    uint16_t interval_rest_s;
    uint16_t interval_rounds;
    uint16_t interval_round;
    uint64_t interval_until_ms;
} health_log_t;

void health_log_init(health_log_t *log, const health_persist_t *loaded, uint64_t now_ms);
// day 为本地 YYYYMMDD。与已保存的日期不同时清空喝水和训练组。0 表示时钟还不可用。
void health_log_set_day(health_log_t *log, uint32_t day);

bool health_log_add_water(health_log_t *log, uint32_t ml, uint64_t now_ms);
bool health_log_add_set(health_log_t *log, const char *name, unsigned weight_x10,
                        unsigned reps, uint64_t now_ms);
bool health_log_undo(health_log_t *log, uint64_t now_ms);
void health_log_stand(health_log_t *log, uint64_t now_ms);
void health_log_snooze(health_log_t *log, uint64_t now_ms, unsigned minutes);
void health_log_rest(health_log_t *log, unsigned seconds, uint64_t now_ms);
bool health_log_start_interval(health_log_t *log, unsigned work_s, unsigned rest_s,
                               unsigned rounds, uint64_t now_ms);

bool health_log_sitting_due(const health_log_t *log, uint64_t now_ms);
bool health_log_water_due(const health_log_t *log, uint64_t now_ms);
unsigned health_log_rest_left_s(const health_log_t *log, uint64_t now_ms);
bool health_log_undo_open(const health_log_t *log, uint64_t now_ms);

// 间歇阶段到点时推进到下一阶段。返回 true 表示刚发生切换，调用方播放提示音。
bool health_log_tick(health_log_t *log, uint64_t now_ms);

// 执行一个 StepFun 工具调用，把回传 JSON 写到 out。未知工具返回 false。
bool health_log_tool(health_log_t *log, const char *name, const char *args,
                     uint64_t now_ms, char *out, size_t cap);
