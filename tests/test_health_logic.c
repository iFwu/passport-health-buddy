#include "b64_stream.h"
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
    char out[128];
    EXPECT(health_log_tool(&log, "log_water", "{\"ml\":250}", 1000, out, sizeof(out)));
    EXPECT(log.saved.water_ml == 250);
    EXPECT(strstr(out, "\"water_ml\":250"));
    EXPECT(health_log_undo(&log, 2000));
    EXPECT(log.saved.water_ml == 0);
    EXPECT(!health_log_undo(&log, 2000));
    EXPECT(!health_log_add_water(&log, 0, 3000));
}

static void test_day_rollover_and_sets(void)
{
    health_log_t log;
    health_persist_t saved = { .day = 20261019, .water_ml = 500, .set_count = 1 };
    memcpy(saved.sets[0].name, "squat", 6);
    health_log_init(&log, &saved, 0);
    EXPECT(log.saved.water_ml == 500);
    health_log_set_day(&log, 20261020);
    EXPECT(log.saved.water_ml == 0);
    EXPECT(log.saved.set_count == 0);
    char out[160];
    EXPECT(health_log_tool(&log, "log_set",
                           "{\"exercise\":\"bench\",\"weight_kg\":60.5,\"reps\":8}",
                           1000, out, sizeof(out)));
    EXPECT(log.saved.set_count == 1);
    EXPECT(log.saved.sets[0].weight_x10 == 605);
    EXPECT(log.saved.sets[0].reps == 8);
    EXPECT(health_log_rest_left_s(&log, 1000) == 90);
    EXPECT(health_log_undo_open(&log, 4000));
    EXPECT(health_log_undo(&log, 4000));
    EXPECT(log.saved.set_count == 0);
}

static void test_interval_and_sitting(void)
{
    health_log_t log;
    health_log_init(&log, NULL, 0);
    EXPECT(!health_log_sitting_due(&log, HEALTH_SIT_MS - 1));
    EXPECT(health_log_sitting_due(&log, HEALTH_SIT_MS));
    health_log_snooze(&log, HEALTH_SIT_MS, 10);
    EXPECT(!health_log_sitting_due(&log, HEALTH_SIT_MS + 1000));
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

static void test_step_events(void)
{
    step_rx_t *rx = step_rx_create(on_pcm, NULL);
    EXPECT(rx);
    const char *call =
        "{\"type\":\"response.function_call_arguments.done\",\"call_id\":\"call_1\","
        "\"name\":\"log_water\",\"arguments\":\"{\\\"ml\\\": 250}\"}";
    EXPECT(step_rx_feed(rx, call, 20));
    EXPECT(step_rx_feed(rx, call + 20, strlen(call) - 20));
    step_msg_t msg;
    EXPECT(step_rx_take(rx, &msg));
    EXPECT(strcmp(msg.name, "log_water") == 0);
    EXPECT(strcmp(msg.call_id, "call_1") == 0);
    EXPECT(strstr(msg.arguments, "250"));

    const int16_t pcm[] = {1, -2, 300};
    char encoded[32];
    const size_t n = b64_encode((const uint8_t *)pcm, sizeof(pcm), encoded, sizeof(encoded));
    EXPECT(n > 0);
    char json[96];
    snprintf(json, sizeof(json),
             "{\"type\":\"response.audio.delta\",\"delta\":\"%s\"}", encoded);
    g_pcm_samples = 0;
    EXPECT(step_rx_feed(rx, json, strlen(json)));
    EXPECT(step_rx_take(rx, &msg));
    EXPECT(strcmp(msg.type, "response.audio.delta") == 0);
    EXPECT(g_pcm_samples == 3);
    EXPECT(g_pcm[0] == 1 && g_pcm[1] == -2 && g_pcm[2] == 300);
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
    test_day_rollover_and_sets();
    test_interval_and_sitting();
    test_step_events();
    test_form();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
