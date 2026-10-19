#include "step_trace.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compile REAL runtime, serializer, and streaming parser together:
 * cc -std=c11 -Wall -Wextra -Werror -DTEST_TRACE_TEXT=0 \
 *   -Itests/trace_stubs -Imain tests/test_step_trace.c main/step_trace.c \
 *   main/voice_trace.c main/step_rx.c main/qjson.c main/b64_stream.c \
 *   -o /tmp/test_step_trace
 * Repeat with -DTEST_TRACE_TEXT=1. --json-lines writes only captured ESP_LOG
 * JSON rows for an independent json.loads consumer; diagnostics use stderr.
 * No substitute parser, formatter, correlation state, or audio decoder here. */

#define ROW_CAP 512
#define LINE_CAP 4096
static char rows[ROW_CAP][LINE_CAP];
static size_t row_count;
static unsigned failures, mutex_depth, critical_depth, mutex_takes, mutex_gives;
static int64_t now_us;
static bool json_lines;
static step_rx_t *rx;
static size_t pcm_samples;
static const char secret[] = "TRACE_FAKE_SECRET_7x";

#define EXPECT(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage)
{
    EXPECT(storage != NULL);
    EXPECT(mutex_depth == 0 && critical_depth == 0);
    if (!storage) return NULL;
    EXPECT(storage->held == 0);
    storage->held = 0;
    return storage;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    EXPECT(semaphore != NULL);
    EXPECT(timeout == portMAX_DELAY);
    EXPECT(critical_depth == 0);
    /* Fail rather than hang when runtime accidentally recursively takes its
     * nonrecursive mutex. Keep the first lock intact for the outer caller. */
    if (!semaphore || semaphore->held || mutex_depth) {
        EXPECT(false);
        return pdFALSE;
    }
    semaphore->held = 1;
    mutex_depth++;
    mutex_takes++;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    EXPECT(semaphore != NULL && semaphore->held == 1);
    EXPECT(mutex_depth == 1);
    if (!semaphore || !semaphore->held || !mutex_depth) return pdFALSE;
    semaphore->held = 0;
    mutex_depth--;
    mutex_gives++;
    return pdTRUE;
}

void trace_stub_enter_critical(void) { critical_depth++; }
void trace_stub_exit_critical(void)
{
    EXPECT(critical_depth != 0);
    if (critical_depth) critical_depth--;
}

uint32_t esp_random(void) { return UINT32_C(0x13579bdf); }
int64_t esp_timer_get_time(void) { return now_us; }

void trace_stub_log(const char *level, const char *tag, const char *format, ...)
{
    char line[LINE_CAP];
    EXPECT(strcmp(tag, "voice_trace") == 0);
    EXPECT(strcmp(level, "I") == 0);
    /* Serializing a shared formatting buffer under a task mutex is allowed;
     * logging with interrupts disabled in a port critical section is not. */
    EXPECT(critical_depth == 0);
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    EXPECT(n > 0 && (size_t)n < sizeof(line));
    if (n <= 0 || (size_t)n >= sizeof(line)) return;
    EXPECT(strstr(line, secret) == NULL);
    if (strstr(line, secret)) return; /* Never retain or print a leaked secret. */
    EXPECT(strchr(line, '\n') == NULL && strchr(line, '\r') == NULL);
    EXPECT(line[0] == '{' && line[n - 1] == '}');
    EXPECT(strstr(line, "\"run\":\"13579bdf\"") != NULL);
    EXPECT(strstr(line, "\"config\":\"4f9f2cab\"") != NULL);
    EXPECT(strstr(line, "trace.format_error") == NULL);
#ifndef CONFIG_HEALTH_VOICE_TRACE_TEXT
    EXPECT(strstr(line, "PRIVATE_SENTINEL") == NULL);
    EXPECT(strstr(line, "\"text\":") == NULL);
    EXPECT(strstr(line, "\"args\":") == NULL);
    EXPECT(strstr(line, "\"result\":") == NULL);
#endif
    EXPECT(row_count < ROW_CAP);
    if (row_count < ROW_CAP) memcpy(rows[row_count++], line, (size_t)n + 1);
}

static const char *last_row(void)
{
    EXPECT(row_count != 0);
    return row_count ? rows[row_count - 1] : "";
}

static void has(const char *field)
{
    EXPECT(strstr(last_row(), field) != NULL);
}

static void lacks(const char *field)
{
    EXPECT(strstr(last_row(), field) == NULL);
}

static void has_uint(const char *key, uint32_t value)
{
    char token[96];
    int n = snprintf(token, sizeof(token), "\"%s\":%" PRIu32, key, value);
    EXPECT(n > 0 && (size_t)n < sizeof(token));
    const char *p = strstr(last_row(), token);
    EXPECT(p != NULL && (p[n] == ',' || p[n] == '}'));
}

static void unlocked(void)
{
    EXPECT(mutex_depth == 0 && critical_depth == 0);
    EXPECT(mutex_takes == mutex_gives);
}

static void pcm(void *user, const int16_t *samples, size_t count)
{
    EXPECT(user == &pcm_samples && samples != NULL);
    pcm_samples += count;
}

static uint32_t receive(const char *json)
{
    step_msg_t message;
    size_t len = strlen(json);
    /* Split every message inside keys, escaped values, and identifiers. */
    for (size_t offset = 0; offset < len;) {
        size_t size = len - offset < 3 ? len - offset : 3;
        bool ok = step_rx_feed(rx, json + offset, size);
        EXPECT(ok);
        if (!ok) {
            step_rx_reset(rx);
            return UINT32_MAX;
        }
        offset += size;
        if (offset < len) EXPECT(!step_rx_take(rx, &message));
    }
    bool complete = step_rx_take(rx, &message);
    EXPECT(complete);
    if (!complete) return UINT32_MAX;
    EXPECT(!step_rx_take(rx, &message));
    uint32_t turn = step_trace_server(&message);
    unlocked();
    return turn;
}

static void test_conversation(void)
{
    now_us = 100000;
    step_trace_init("test-model", "hello", secret);
    has("\"stage\":\"session.config\"");
    has_uint("ms", 100);
#ifdef CONFIG_HEALTH_VOICE_TRACE_TEXT
    has("\"status\":\"content_enabled\"");
#else
    has("\"status\":\"metadata_only\"");
#endif
    EXPECT(receive("{\"type\":\"session.created\",\"session\":{\"id\":\"session-one\"}}") == 0);
    step_trace_output_volume(65);
    uint32_t one = step_trace_begin();
    EXPECT(one == 1);
    step_trace_mic_ready();
    step_trace_upload(1600, true);
    step_trace_upload(800, true);
    step_trace_upload(400, false);
    has("\"stage\":\"input.upload_failed\"");
    has_uint("up_samples", 2400);
    has_uint("up_frames", 2);
    now_us = 350000;
    step_trace_expect_input(one);
    char event1[64], event2[64];
    step_trace_event_id(event1, sizeof(event1));
    step_trace_event_id(event2, sizeof(event2));
    EXPECT(strcmp(event1, "hb-13579bdf-1") == 0);
    EXPECT(strcmp(event2, "hb-13579bdf-2") == 0);
    step_trace_sent(one, "input.commit", event1, NULL, true);
    has("\"event\":\"hb-13579bdf-1\"");
    has_uint("record_ms", 250);
    has_uint("volume", 65);
    EXPECT(receive("{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"input-one\"}") == one);
    has("\"input\":\"input-one\"");
    step_trace_expect_response(one);
    EXPECT(receive("{\"type\":\"response.created\",\"response\":{\"id\":\"response-one\",\"status\":\"in_progress\"}}") == one);
    has_uint("turn", one);
    has("\"session\":\"session-one\"");

    /* A new recording must not steal delayed transcript or tool joins. */
    uint32_t two = step_trace_begin();
    EXPECT(two == 2);
    EXPECT(receive("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"input-one\",\"transcript\":\"PRIVATE_SENTINEL ASR\\n\\\"quote\\\"\\\\ TRACE_FAKE_SECRET_7x\"}") == one);
    has_uint("turn", one);
    has("\"input\":\"input-one\"");
#ifdef CONFIG_HEALTH_VOICE_TRACE_TEXT
    has("\"text\":\"PRIVATE_SENTINEL ASR\\n\\\"quote\\\"\\\\ <redacted>\"");
#else
    has("\"content_redacted\":true");
#endif
    step_trace_expect_input(two);
    EXPECT(receive("{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"input-two\"}") == two);
    step_trace_expect_response(two);
    EXPECT(receive("{\"type\":\"response.created\",\"response\":{\"id\":\"response-two\"}}") == two);

    EXPECT(receive("{\"type\":\"response.output_item.added\",\"response_id\":\"response-one\",\"item\":{\"id\":\"tool-item-one\",\"type\":\"function_call\",\"call_id\":\"call-one\",\"name\":\"log_water\"}}") == one);
    has_uint("turn", one);
    has("\"call\":\"call-one\"");
    EXPECT(step_trace_call_turn("call-one") == one);
    EXPECT(step_trace_call_turn("not-known") == 0);
    EXPECT(receive("{\"type\":\"response.function_call_arguments.done\",\"call_id\":\"call-one\",\"name\":\"log_water\",\"arguments\":\"{\\\"note\\\":\\\"PRIVATE_SENTINEL TRACE_FAKE_SECRET_7x\\\",\\\"ml\\\":250}\"}") == one);
    has_uint("turn", one);
    has("\"call\":\"call-one\"");
    step_trace_tool(step_trace_call_turn("call-one"), "tool.result", "call-one", "log_water",
                    "{\"note\":\"PRIVATE_SENTINEL TRACE_FAKE_SECRET_7x\"}",
                    "{\"ok\":true,\"note\":\"PRIVATE_SENTINEL TRACE_FAKE_SECRET_7x\"}\n", "ok", NULL);
    has_uint("turn", one);
    has("\"call\":\"call-one\"");
#ifdef CONFIG_HEALTH_VOICE_TRACE_TEXT
    has("\"args\":\"{\\\"note\\\":\\\"PRIVATE_SENTINEL <redacted>\\\"}\"");
    has("\"result\":\"{\\\"ok\\\":true,\\\"note\\\":\\\"PRIVATE_SENTINEL <redacted>\\\"}\\n\"");
#endif
    step_trace_sent(one, "tool.output", event2, "call-one", true);
    has_uint("turn", one);
    has("\"call\":\"call-one\"");
    has("\"status\":\"sent\"");
    EXPECT(receive("{\"type\":\"response.done\",\"response\":{\"id\":\"response-one\",\"status\":\"completed\",\"output\":[{\"type\":\"function_call\"},{\"type\":\"message\"}]}}") == one);
    has_uint("output_tools", 1);
    has_uint("output_messages", 1);
    EXPECT(receive("{\"type\":\"response.done\",\"response\":{\"id\":\"response-two\",\"status\":\"completed\",\"output\":[]}}") == two);
    has_uint("output_tools", 0);
    has_uint("output_messages", 0);

    /* Follow-up response belongs to the tool's turn, not active recording. */
    step_trace_expect_response(step_trace_call_turn("call-one"));
    EXPECT(receive("{\"type\":\"response.created\",\"response\":{\"id\":\"response-followup\"}}") == one);
    has_uint("turn", one);
    has("\"input\":\"input-one\"");
    step_trace_output_volume(0);
    size_t before_audio = row_count;
    EXPECT(receive("{\"type\":\"response.audio.delta\",\"response_id\":\"response-followup\",\"delta\":\"AQACAAMABAA=\"}") == one);
    EXPECT(pcm_samples == 4);
    EXPECT(row_count == before_audio); /* Never trace PCM/base64 or each delta. */
    step_trace_received(600, 400);
    step_trace_played(200, 0, true);
    step_trace_played(100, 0, false);
    has("\"stage\":\"playback.failed\"");
    has_uint("played_samples", 200);
    step_trace_playback_done();
    has("\"stage\":\"playback.done\"");
    has_uint("turn", one);
    has("\"response\":\"response-followup\"");
    has_uint("received_samples", 600);
    has_uint("queued_samples", 400);
    has_uint("dropped_samples", 200);
    has_uint("played_samples", 200);
    has_uint("volume", 0);
    EXPECT(receive("{\"type\":\"response.audio_transcript.done\",\"response_id\":\"response-followup\",\"transcript\":\"PRIVATE_SENTINEL spoken\"}") == one);
    has_uint("turn", one);
    EXPECT(receive("{\"type\":\"response.done\",\"response\":{\"id\":\"response-followup\",\"status\":\"completed\",\"output\":[{\"type\":\"message\"}]}}") == one);
    has_uint("output_tools", 0);
    has_uint("output_messages", 1);
    unlocked();
}

static void test_disconnect_and_early_asr(void)
{
    step_trace_disconnect();
    has("\"stage\":\"session.disconnected\"");
    EXPECT(step_trace_call_turn("call-one") == 0);
    EXPECT(receive("{\"type\":\"response.done\",\"response\":{\"id\":\"response-one\",\"output\":[]}}") == 0);
    has_uint("turn", 0);
    has("\"response\":\"response-one\"");
    lacks("\"session\":");
    lacks("\"input\":");
    EXPECT(receive("{\"type\":\"response.text.done\",\"text\":\"PRIVATE_SENTINEL stale\"}") == 0);
    has_uint("turn", 0);
    EXPECT(receive("{\"type\":\"session.updated\",\"session\":{\"id\":\"session-two\"}}") == 0);
    uint32_t turn = step_trace_begin();
    step_trace_expect_input(turn);
    step_trace_expect_response(turn);
    EXPECT(receive("{\"type\":\"response.created\",\"response\":{\"id\":\"response-early\"}}") == turn);
    EXPECT(receive("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"input-early\",\"transcript\":\"PRIVATE_SENTINEL early\"}") == 0);
    has_uint("turn", 0);
    has("\"input\":\"input-early\""); /* A consumer can join the later commit. */
    EXPECT(receive("{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"input-early\"}") == turn);
    has_uint("turn", turn);
    has("\"input\":\"input-early\"");
    EXPECT(receive("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"input-early\",\"transcript\":\"PRIVATE_SENTINEL late\"}") == turn);
    has_uint("turn", turn);
    unlocked();
}

static void test_response_failure_details(void)
{
    EXPECT(receive("{\"type\":\"response.done\",\"response\":{\"id\":\"response-early\",\"status\":\"failed\",\"status_details\":{\"type\":\"failed\",\"error\":{\"type\":\"server_error\",\"code\":\"model_unavailable\",\"message\":\"PRIVATE_SENTINEL TRACE_FAKE_SECRET_7x\"}},\"output\":[]}}") != 0);
    has("\"status\":\"failed\"");
    has("model_unavailable");
#ifdef CONFIG_HEALTH_VOICE_TRACE_TEXT
    has("PRIVATE_SENTINEL <redacted>");
#endif
    EXPECT(receive("{\"type\":\"response.done\",\"response\":{\"id\":\"response-early\",\"status\":\"incomplete\",\"status_details\":{\"type\":\"incomplete\",\"reason\":\"max_output_tokens\"},\"output\":[]}}") != 0);
    has("\"status\":\"incomplete\"");
    has("\"reason\":\"max_output_tokens\"");
    EXPECT(receive("{\"type\":\"error\",\"event_id\":\"server-error-event\",\"error\":{\"type\":\"invalid_request_error\",\"code\":\"bad_input\",\"event_id\":\"client-error-event\",\"message\":\"PRIVATE_SENTINEL TRACE_FAKE_SECRET_7x\"}}") == 0);
    has("\"event\":\"client-error-event\"");
    has("\"status\":\"invalid_request_error\"");
    has("\"reason\":\"bad_input\"");
    step_trace_parse_error("invalid_json");
    has("\"stage\":\"rx.parse_failed\"");
    has("\"reason\":\"invalid_json\"");
}

static void test_bounded_history(void)
{
    /* Four later recordings evict input/response/call joins. Old IDs remain
     * visible but may NEVER be guessed to belong to the newest recording. */
    for (unsigned i = 0; i < 4; i++) EXPECT(step_trace_begin() != 0);
    EXPECT(receive("{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"input-early\",\"transcript\":\"PRIVATE_SENTINEL evicted\"}") == 0);
    has_uint("turn", 0);
    has("\"input\":\"input-early\"");
    EXPECT(receive("{\"type\":\"response.done\",\"response\":{\"id\":\"response-early\",\"output\":[]}}") == 0);
    has_uint("turn", 0);
    has("\"response\":\"response-early\"");
    step_trace_disconnect();

    uint32_t turn = step_trace_begin();
    for (unsigned i = 0; i < 10; i++) {
        char json[256];
        step_trace_expect_response(turn);
        snprintf(json, sizeof(json), "{\"type\":\"response.created\",\"response\":{\"id\":\"bounded-response-%u\"}}", i);
        EXPECT(receive(json) == turn);
        snprintf(json, sizeof(json), "{\"type\":\"response.output_item.added\",\"response_id\":\"bounded-response-%u\",\"item\":{\"type\":\"function_call\",\"call_id\":\"bounded-call-%u\",\"name\":\"log_water\"}}", i, i);
        EXPECT(receive(json) == turn);
    }
    EXPECT(step_trace_call_turn("bounded-call-0") == 0);
    EXPECT(step_trace_call_turn("bounded-call-9") == turn);
    EXPECT(receive("{\"type\":\"response.done\",\"response\":{\"id\":\"bounded-response-0\",\"output\":[]}}") == 0);
    has_uint("turn", 0);
    has("\"response\":\"bounded-response-0\"");
    EXPECT(receive("{\"type\":\"response.function_call_arguments.done\",\"call_id\":\"bounded-call-0\",\"arguments\":\"{}\"}") == 0);
    has_uint("turn", 0);
    has("\"call\":\"bounded-call-0\"");
    EXPECT(receive("{\"type\":\"response.done\",\"response\":{\"id\":\"bounded-response-9\",\"output\":[]}}") == turn);
    has_uint("turn", turn);
    step_trace_disconnect();
    unlocked();
}

static void test_pending_saturation(void)
{
    for (unsigned i = 0; i < 5; i++) {
        uint32_t turn = step_trace_begin();
        step_trace_expect_input(turn);
        step_trace_expect_response(turn);
    }
    /* Once capacity is exceeded, FIFO alignment is unprovable. Keep the
     * queue poisoned until disconnect instead of shifting late old events
     * onto newer turns. Test both first overflow and subsequent expectations. */
    for (unsigned i = 0; i < 6; i++) {
        if (i == 5) {
            uint32_t turn = step_trace_begin();
            step_trace_expect_input(turn);
            step_trace_expect_response(turn);
        }
        char json[192];
        snprintf(json, sizeof(json), "{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"overflow-input-%u\"}", i);
        EXPECT(receive(json) == 0);
        has_uint("turn", 0);
        snprintf(json, sizeof(json), "{\"type\":\"response.created\",\"response\":{\"id\":\"overflow-response-%u\"}}", i);
        EXPECT(receive(json) == 0);
        has_uint("turn", 0);
    }
    step_trace_disconnect();
    uint32_t fresh = step_trace_begin();
    step_trace_expect_input(fresh);
    step_trace_expect_response(fresh);
    EXPECT(receive("{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"input-recovered\"}") == fresh);
    EXPECT(receive("{\"type\":\"response.created\",\"response\":{\"id\":\"response-recovered\"}}") == fresh);
    has_uint("turn", fresh);
    has("\"input\":\"input-recovered\"");
    unlocked();
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--json-lines") == 0) json_lines = true;
    else if (argc != 1) {
        fprintf(stderr, "usage: %s [--json-lines]\n", argv[0]);
        return 2;
    }
    rx = step_rx_create(pcm, &pcm_samples);
    if (!rx) return 2;
    test_conversation();
    test_disconnect_and_early_asr();
    test_response_failure_details();
    test_bounded_history();
    test_pending_saturation();
    step_rx_destroy(rx);
    unlocked();
    EXPECT(mutex_takes > 0);
    if (json_lines) {
        for (size_t i = 0; i < row_count; i++) puts(rows[i]);
    } else if (!failures) {
        puts("step_trace runtime tests passed");
    }
    if (failures) fprintf(stderr, "%u runtime trace assertion(s) failed\n", failures);
    return failures ? 1 : 0;
}
