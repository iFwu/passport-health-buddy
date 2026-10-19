#include "b64_stream.h"
#include "health_exercise.h"
#include "health_log.h"
#include "portal_form.h"
#include "step_rx.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define EXPECT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

static size_t g_pcm_samples;
static int16_t g_pcm[8];

static void on_pcm(void *user, const int16_t *pcm, size_t samples)
{
    (void)user;
    for (size_t i = 0; i < samples && g_pcm_samples < 8; i++) g_pcm[g_pcm_samples++] = pcm[i];
}

static void test_water_and_undo(void)
{
    health_log_t log;
    health_log_init(&log, NULL, 1000);
    char out[256];
    EXPECT(health_log_tool(&log, "log_water", "{\"ml\":250}", 1000, out, sizeof(out)));
    EXPECT(log.saved.water_ml == 250);
    EXPECT(strstr(out, "\"water_ml\":250"));
    EXPECT(health_log_undo_voice(&log));
    EXPECT(log.saved.water_ml == 0);
    EXPECT(!health_log_undo_voice(&log));
    EXPECT(!health_log_add_water(&log, 0, 3000));
}

static void test_exercise_catalogue_and_sanitize(void)
{
    EXPECT(health_exercise_find("squat") == HEALTH_EX_SQUAT);
    EXPECT(health_exercise_find("bench_press") == HEALTH_EX_BENCH_PRESS);
    EXPECT(health_exercise_find("unknown_action") == HEALTH_EX_OTHER);

    char clean[18];
    health_exercise_sanitize_en("  Hack  Squat  ", clean, sizeof(clean));
    EXPECT(strcmp(clean, "Hack Squat") == 0);

    health_exercise_sanitize_en("", clean, sizeof(clean));
    EXPECT(strcmp(clean, "Workout") == 0);

    health_exercise_sanitize_en("very long workout name that exceeds buffer", clean, sizeof(clean));
    EXPECT(strlen(clean) < sizeof(clean));
}

static void test_day_rollover_and_workout(void)
{
    health_log_t log;
    health_persist_t saved = { .day = 20261019, .water_ml = 500, .entry_count = 1 };
    saved.entries[0].exercise = HEALTH_EX_SQUAT;
    saved.entries[0].weight_x10 = 800;
    saved.entries[0].reps = 10;
    saved.entries[0].sets = 3;
    saved.last_weight_x10[HEALTH_EX_SQUAT] = 800;
    saved.last_reps[HEALTH_EX_SQUAT] = 10;

    health_log_init(&log, &saved, 0);
    EXPECT(log.saved.water_ml == 500);
    EXPECT(log.saved.entry_count == 1);

    health_log_set_day(&log, 20261020);
    EXPECT(log.saved.water_ml == 0);
    EXPECT(log.saved.entry_count == 0);
    EXPECT(log.saved.last_weight_x10[HEALTH_EX_SQUAT] == 800);
    EXPECT(log.saved.last_reps[HEALTH_EX_SQUAT] == 10);

    char out[320];
    EXPECT(health_log_tool(&log, "log_workout",
                           "{\"exercise\":\"squat\",\"weight_kg\":85.0,\"reps\":8,\"sets\":3,\"rest_s\":120}",
                           1000, out, sizeof(out)));
    EXPECT(log.saved.entry_count == 1);
    EXPECT(log.saved.entries[0].weight_x10 == 850);
    EXPECT(log.saved.entries[0].reps == 8);
    EXPECT(log.saved.entries[0].sets == 3);
    EXPECT(health_log_total_sets(&log) == 3);
    EXPECT(health_log_total_volume_kg(&log) == 85 * 8 * 3);
    EXPECT(health_log_rest_left_s(&log, 1000) == 120);
    EXPECT(strstr(out, "\"prev\":\"squat 80.0x10\""));

    EXPECT(health_log_tool(&log, "log_workout",
                           "{\"exercise\":\"squat\",\"weight_kg\":90.0,\"reps\":6,\"sets\":3,\"replace_last\":true}",
                           2000, out, sizeof(out)));
    EXPECT(log.saved.entry_count == 1);
    EXPECT(log.saved.entries[0].weight_x10 == 900);

    EXPECT(health_log_undo_voice(&log));
    EXPECT(log.saved.entries[0].weight_x10 == 850);

    EXPECT(health_log_tool(&log, "undo_last", "{}", 20000, out, sizeof(out)));
    EXPECT(log.saved.entry_count == 0);
}

static void test_timer_and_interval(void)
{
    health_log_t log;
    health_log_init(&log, NULL, 0);
    EXPECT(!health_log_sitting_due(&log, HEALTH_SIT_MS - 1));
    EXPECT(health_log_sitting_due(&log, HEALTH_SIT_MS));
    health_log_snooze(&log, HEALTH_SIT_MS, 10);
    EXPECT(!health_log_sitting_due(&log, HEALTH_SIT_MS + 1000));

    char out[128];
    EXPECT(health_log_tool(&log, "start_timer", "{\"seconds\":10}", 0, out, sizeof(out)));
    EXPECT(log.timer_running);
    EXPECT(!health_log_tick(&log, 9000));
    EXPECT(health_log_tick(&log, 10000));
    EXPECT(!log.timer_running);

    EXPECT(health_log_start_interval(&log, 30, 15, 2, 0));
    EXPECT(!health_log_tick(&log, 29999));
    EXPECT(health_log_tick(&log, 30000));
    EXPECT(!log.interval_work);
    EXPECT(health_log_tick(&log, 45000));
    EXPECT(log.interval_round == 2);
    EXPECT(log.interval_work);
    EXPECT(health_log_tick(&log, 75000));
    EXPECT(!log.interval_work);
    EXPECT(health_log_tick(&log, 90000));
    EXPECT(!log.interval_on);
}

static void test_records_and_reminders_without_navigation(void)
{
    health_log_t log;
    health_log_init(&log, NULL, 0);
    char out[320];
    EXPECT(health_log_tool(&log, "start_timer", "{\"seconds\":90}", 1000, out, sizeof(out)));
    EXPECT(health_log_tool(&log, "log_water", "{\"ml\":250}", 2000, out, sizeof(out)));
    EXPECT(log.mode_request == 0);
    EXPECT(health_log_rest_left_s(&log, 2000) == 89);
    EXPECT(health_log_tool(&log, "set_mode", "{\"mode\":\"gym\"}", 3000, out, sizeof(out)));
    EXPECT(log.mode_request == 2);

    log.mode_request = 0;
    EXPECT(health_log_tool(&log, "log_workout", "{\"exercise\":\"squat\",\"weight_kg\":60,\"reps\":8}", 4000, out, sizeof(out)));
    uint32_t saved_revision = log.revision;
    EXPECT(health_log_tool(&log, "log_workout", "{\"exercise\":\"squat\",\"weight_kg\":65,\"reps\":8,\"replace_last\":true}", 5000, out, sizeof(out)));
    EXPECT(log.revision != saved_revision && log.saved.entries[0].weight_x10 == 650);
    EXPECT(log.mode_request == 0);
    for (unsigned i = 1; i < HEALTH_ENTRY_CAP; i++) {
        EXPECT(health_log_tool(&log, "log_workout", "{\"exercise\":\"squat\",\"reps\":8}", 6000 + i, out, sizeof(out)));
    }
    saved_revision = log.revision;
    EXPECT(health_log_tool(&log, "log_workout", "{\"exercise\":\"squat\",\"weight_kg\":70,\"reps\":8}", 7000, out, sizeof(out)));
    EXPECT(log.saved.entry_count == HEALTH_ENTRY_CAP);
    EXPECT(log.revision != saved_revision && log.saved.entries[HEALTH_ENTRY_CAP - 1].weight_x10 == 700);
    EXPECT(log.saved.entries[0].weight_x10 == 0);

    health_log_init(&log, NULL, 0);
    EXPECT(health_log_take_reminders(&log, HEALTH_SIT_MS) == HEALTH_REMINDER_ACTIVITY);
    EXPECT(health_log_take_reminders(&log, HEALTH_SIT_MS + 1) == 0);
    EXPECT(health_log_take_reminders(&log, HEALTH_WATER_MS) == HEALTH_REMINDER_WATER);
    EXPECT(health_log_take_reminders(&log, HEALTH_SIT_MS * 2) == HEALTH_REMINDER_ACTIVITY);
    health_log_snooze(&log, HEALTH_WATER_MS * 2 - 1000, 10);
    EXPECT(health_log_take_reminders(&log, HEALTH_WATER_MS * 2) == 0);
    EXPECT(health_log_take_reminders(&log, HEALTH_WATER_MS * 2 + 10 * 60000) != 0);
}

static void test_step_events(void)
{
    step_rx_t *rx = step_rx_create(on_pcm, NULL);
    EXPECT(rx);
    const char *call =
        "{\"type\":\"response.function_call_arguments.done\",\"call_id\":\"call_1\","
        "\"name\":\"log_workout\",\"arguments\":\"{\\\"exercise\\\":\\\"squat\\\",\\\"weight_kg\\\":80,\\\"reps\\\":10,\\\"sets\\\":3}\"}";
    EXPECT(step_rx_feed(rx, call, 20));
    EXPECT(step_rx_feed(rx, call + 20, strlen(call) - 20));
    step_msg_t msg;
    EXPECT(step_rx_take(rx, &msg));
    EXPECT(strcmp(msg.name, "log_workout") == 0);
    EXPECT(strcmp(msg.call_id, "call_1") == 0);
    EXPECT(strstr(msg.arguments, "squat"));
    EXPECT(strstr(msg.arguments, "80"));

    const int16_t pcm[] = {1, -2, 300};
    char encoded[32];
    const size_t n = b64_encode((const uint8_t *)pcm, sizeof(pcm), encoded, sizeof(encoded));
    EXPECT(n > 0);
    char json[96];
    snprintf(json, sizeof(json),
             "{\"type\":\"response.audio.delta\",\"delta\":\"%.*s\"}", (int)n, encoded);
    g_pcm_samples = 0;
    EXPECT(step_rx_feed(rx, json, strlen(json)));
    EXPECT(step_rx_take(rx, &msg));
    EXPECT(strcmp(msg.type, "response.audio.delta") == 0);
    EXPECT(g_pcm_samples == 3);
    EXPECT(g_pcm[0] == 1 && g_pcm[1] == -2 && g_pcm[2] == 300);

    const char *updated =
        "{\"event_id\":\"ev_1\",\"type\":\"session.updated\",\"session\":{\"tools\":["
        "{\"type\":\"function\",\"function\":{\"name\":\"log_workout\",\"parameters\":{"
        "\"type\":\"object\",\"properties\":{\"exercise\":{\"type\":\"string\","
        "\"enum\":[\"squat\",\"bench_press\",\"other\"]}}}}}]}}";
    EXPECT(step_rx_feed(rx, updated, strlen(updated)));
    EXPECT(step_rx_take(rx, &msg));
    EXPECT(strcmp(msg.type, "session.updated") == 0);
    step_rx_destroy(rx);
}

static void test_form(void)
{
    char out[32];
    const char *body = "ssid=cafe+wifi&pass=secret12&key=abcdefghijklmnopqrst";
    EXPECT(form_get(body, strlen(body), "ssid", out, sizeof(out)) > 0);
    EXPECT(strcmp(out, "cafe wifi") == 0);
    EXPECT(form_step_key_valid("abcdefghijklmnopqrst", 180));
    EXPECT(!form_step_key_valid("short", 180));
    EXPECT(!form_step_key_valid("has space in the key!!!!", 180));
}

int main(void)
{
    test_water_and_undo();
    test_exercise_catalogue_and_sanitize();
    test_day_rollover_and_workout();
    test_timer_and_interval();
    test_records_and_reminders_without_navigation();
    test_step_events();
    test_form();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("All host tests passed successfully.\n");
    return 0;
}
