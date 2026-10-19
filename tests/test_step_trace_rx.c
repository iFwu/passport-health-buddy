#include "b64_stream.h"
#include "step_rx.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;

#define EXPECT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static bool parse_chunks(step_rx_t *rx, const char *json, size_t chunk, step_msg_t *msg)
{
    size_t len = strlen(json);
    for (size_t pos = 0; pos < len;) {
        size_t n = len - pos < chunk ? len - pos : chunk;
        if (!step_rx_feed(rx, json + pos, n)) {
            EXPECT(false);
            step_rx_reset(rx);
            return false;
        }
        pos += n;
        if (pos < len) EXPECT(!step_rx_take(rx, msg));
    }
    bool complete = step_rx_take(rx, msg);
    EXPECT(complete);
    EXPECT(!step_rx_take(rx, msg));
    return complete;
}

static void expect_empty_trace(const step_msg_t *msg)
{
    EXPECT(!msg->event_id[0] && !msg->session_id[0] && !msg->response_id[0]);
    EXPECT(!msg->item_id[0] && !msg->status[0] && !msg->item_type[0]);
    EXPECT(!msg->error_type[0] && !msg->error_code[0]);
    EXPECT(!msg->error_message[0] && !msg->error_event_id[0]);
    EXPECT(!msg->name[0] && !msg->call_id[0] && !msg->arguments[0]);
    EXPECT(!msg->transcript[0]);
    EXPECT(msg->output_tools == 0 && msg->output_messages == 0 && msg->truncated == 0);
}

static void test_fragmented_identifiers(step_rx_t *rx)
{
    const char *session =
        "{\"event_id\":\"event_session_0123456789abcdefghijklmnopqrstuvwxyz\","
        "\"session\":{\"id\":\"session_0123456789abcdefghijklmnopqrstuvwxyz\","
        "\"tools\":[{\"type\":\"function\",\"function\":{\"name\":\"not_a_call\","
        "\"parameters\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}}}}}],"
        "\"metadata\":{\"id\":\"wrong_session\",\"status\":\"wrong_status\"}},"
        "\"type\":\"session.updated\"}";
    const char *response =
        "{\"response\":{\"id\":\"response_0123456789abcdefghijklmnopqrstuvwxyz\","
        "\"status\":\"completed\",\"metadata\":{\"id\":\"wrong_response\","
        "\"status\":\"failed\",\"type\":\"function_call\"}},"
        "\"event_id\":\"event_response\",\"type\":\"response.done\"}";
    const char *messages[] = {session, response};
    step_msg_t msg;
    for (size_t m = 0; m < sizeof(messages) / sizeof(messages[0]); m++) {
        const char *json = messages[m];
        const size_t len = strlen(json);
        // Every possible two-fragment boundary, including inside keys/escapes.
        for (size_t split = 0; split <= len; split++) {
            EXPECT(step_rx_feed(rx, json, split));
            if (split < len) EXPECT(!step_rx_take(rx, &msg));
            EXPECT(step_rx_feed(rx, json + split, len - split));
            EXPECT(step_rx_take(rx, &msg));
            if (m == 0) {
                EXPECT(strcmp(msg.type, "session.updated") == 0);
                EXPECT(strcmp(msg.event_id, "event_session_0123456789abcdefghijklmnopqrstuvwxyz") == 0);
                EXPECT(strcmp(msg.session_id, "session_0123456789abcdefghijklmnopqrstuvwxyz") == 0);
                EXPECT(!msg.response_id[0] && !msg.status[0] && !msg.name[0]);
            } else {
                EXPECT(strcmp(msg.type, "response.done") == 0);
                EXPECT(strcmp(msg.response_id, "response_0123456789abcdefghijklmnopqrstuvwxyz") == 0);
                EXPECT(strcmp(msg.status, "completed") == 0);
                EXPECT(strcmp(msg.event_id, "event_response") == 0);
                EXPECT(!msg.session_id[0]);
            }
            EXPECT(msg.truncated == 0);
        }
        EXPECT(parse_chunks(rx, json, 1, &msg));
    }
}

static void test_paths_and_execution(step_rx_t *rx)
{
    step_msg_t msg;
    const char *item =
        "{\"item\":{\"id\":\"item_tool\",\"type\":\"function_call\","
        "\"name\":\"log_workout\",\"call_id\":\"call_tool\","
        "\"arguments\":\"{\\\"exercise\\\":\\\"bench_press\\\",\\\"weight_kg\\\":60,\\\"reps\\\":8}\","
        "\"metadata\":{\"type\":\"message\",\"id\":\"wrong_item\",\"name\":\"wrong_name\"}},"
        "\"response_id\":\"response_tool\",\"event_id\":\"event_tool\","
        "\"type\":\"response.output_item.done\"}";
    for (size_t chunk = 1; chunk <= strlen(item); chunk++) {
        if (!parse_chunks(rx, item, chunk, &msg)) continue;
        EXPECT(strcmp(msg.type, "response.output_item.done") == 0);
        EXPECT(strcmp(msg.item_type, "function_call") == 0);
        EXPECT(strcmp(msg.item_id, "item_tool") == 0);
        EXPECT(strcmp(msg.response_id, "response_tool") == 0);
        EXPECT(strcmp(msg.event_id, "event_tool") == 0);
        EXPECT(strcmp(msg.call_id, "call_tool") == 0);
        EXPECT(strcmp(msg.name, "log_workout") == 0);
        EXPECT(strcmp(msg.arguments, "{\"exercise\":\"bench_press\",\"weight_kg\":60,\"reps\":8}") == 0);
        EXPECT(msg.truncated == 0);
    }
    EXPECT(parse_chunks(rx,
        "{\"type\":\"response.output_item.added\",\"item\":{\"type\":\"message\",\"id\":\"item_reply\"}}",
        1, &msg));
    EXPECT(strcmp(msg.type, "response.output_item.added") == 0);
    EXPECT(strcmp(msg.item_type, "message") == 0 && strcmp(msg.item_id, "item_reply") == 0);
    EXPECT(!msg.arguments[0] && !msg.name[0] && !msg.call_id[0]);

    EXPECT(parse_chunks(rx,
        "{\"type\":\"response.function_call_arguments.done\",\"name\":\"log_water\","
        "\"call_id\":\"call_root\",\"arguments\":\"{\\\"ml\\\":250}\","
        "\"response_id\":\"response_root\",\"item_id\":\"item_root\","
        "\"item\":{\"id\":\"wrong_item\",\"name\":\"wrong_name\",\"call_id\":\"wrong_call\","
        "\"arguments\":\"wrong_args\",\"type\":\"function_call\"},"
        "\"response\":{\"id\":\"wrong_response\"}}", 1, &msg));
    EXPECT(strcmp(msg.name, "log_water") == 0 && strcmp(msg.call_id, "call_root") == 0);
    EXPECT(strcmp(msg.arguments, "{\"ml\":250}") == 0);
    EXPECT(strcmp(msg.item_id, "item_root") == 0 && strcmp(msg.response_id, "response_root") == 0);
    EXPECT(!msg.item_type[0]);

    EXPECT(parse_chunks(rx,
        "{\"item\":{\"id\":\"wrong\",\"type\":\"function_call\",\"name\":\"wrong\","
        "\"call_id\":\"wrong\",\"arguments\":\"wrong\"},\"type\":\"conversation.item.created\"}",
        1, &msg));
    expect_empty_trace(&msg);

    EXPECT(parse_chunks(rx,
        "{\"type\":\"response.done\",\"response\":{\"id\":\"nested\",\"status\":\"cancelled\"},"
        "\"response_id\":\"canonical\",\"status\":\"wrong_root_status\","
        "\"metadata\":{\"event_id\":\"wrong\",\"item_id\":\"wrong\",\"transcript\":\"wrong\","
        "\"text\":\"wrong\",\"error\":{\"message\":\"wrong\"}}}", 1, &msg));
    EXPECT(strcmp(msg.response_id, "canonical") == 0 && strcmp(msg.status, "cancelled") == 0);
    EXPECT(!msg.event_id[0] && !msg.item_id[0] && !msg.transcript[0] && !msg.error_message[0]);
}

static void test_errors_and_asr(step_rx_t *rx)
{
    step_msg_t msg;
    const char *error =
        "{\"event_id\":\"server_error_event\",\"type\":\"error\",\"error\":{"
        "\"type\":\"invalid_request_error\",\"code\":\"invalid_parameter\","
        "\"message\":\"Invalid \\\"sample\\\"\\nrate: \\u4e2d\\u6587\","
        "\"event_id\":\"client_request_event\",\"details\":{\"type\":\"wrong\","
        "\"code\":\"wrong\",\"message\":\"wrong\",\"event_id\":\"wrong\"}},"
        "\"message\":\"wrong_root\",\"code\":\"wrong_root\"}";
    for (size_t chunk = 1; chunk <= strlen(error); chunk++) {
        if (!parse_chunks(rx, error, chunk, &msg)) continue;
        EXPECT(strcmp(msg.type, "error") == 0);
        EXPECT(strcmp(msg.event_id, "server_error_event") == 0);
        EXPECT(strcmp(msg.error_event_id, "client_request_event") == 0);
        EXPECT(strcmp(msg.error_type, "invalid_request_error") == 0);
        EXPECT(strcmp(msg.error_code, "invalid_parameter") == 0);
        EXPECT(strcmp(msg.error_message, "Invalid \"sample\"\nrate: 中文") == 0);
        EXPECT(msg.truncated == 0);
    }
    EXPECT(parse_chunks(rx,
        "{\"type\":\"conversation.item.input_audio_transcription.completed\","
        "\"event_id\":\"asr_async_event\",\"item_id\":\"user_audio_item\","
        "\"content_index\":0,\"transcript\":\"卧推60公斤8个\","
        "\"metadata\":{\"item_id\":\"wrong\",\"transcript\":\"wrong\"}}", 1, &msg));
    EXPECT(strcmp(msg.item_id, "user_audio_item") == 0);
    EXPECT(strcmp(msg.event_id, "asr_async_event") == 0);
    EXPECT(strcmp(msg.transcript, "卧推60公斤8个") == 0);
    EXPECT(!msg.response_id[0] && !msg.error_type[0] && !msg.error_message[0]);
    EXPECT(msg.truncated == 0);
    EXPECT(parse_chunks(rx, "{\"type\":\"response.text.done\",\"text\":\"已记录\\ud83d\\udcaa\"}", 1, &msg));
    EXPECT(strcmp(msg.transcript, "已记录💪") == 0);
    EXPECT(parse_chunks(rx, "{\"type\":\"error\",\"error\":{\"code\":null}}", 1, &msg));
    expect_empty_trace(&msg);
}

static void test_response_output_counts(step_rx_t *rx)
{
    const char *json =
        "{\"type\":\"response.done\",\"event_id\":\"done_event\",\"response\":{"
        "\"id\":\"done_response\",\"status\":\"completed\",\"output\":["
        "{\"id\":\"tool_one\",\"type\":\"function_\\u0063all\",\"name\":\"log_workout\","
        "\"call_id\":\"call_one\",\"arguments\":\"{}\"},"
        "{\"type\":\"message\",\"content\":[{\"type\":\"audio\",\"transcript\":\"not_root\","
        "\"text\":\"not_root\",\"delta\":\"AQD+/ywB\"}]},"
        "{\"type\":\"function_call\",\"metadata\":{\"type\":\"message\"}},"
        "{\"type\":\"function_call_with_unrecognized_suffix\"},"
        "{\"type\":\"message\"},{\"type\":\"audio\"},\"message\",null,"
        "[{\"type\":\"message\"}]],\"metadata\":{\"output\":[{\"type\":\"message\"}]}},"
        "\"output\":[{\"type\":\"function_call\"}],\"metadata\":{\"response\":{"
        "\"output\":[{\"type\":\"function_call\"}]}}}";
    step_msg_t msg;
    for (size_t chunk = 1; chunk <= strlen(json); chunk++) {
        if (!parse_chunks(rx, json, chunk, &msg)) continue;
        EXPECT(strcmp(msg.type, "response.done") == 0);
        EXPECT(strcmp(msg.response_id, "done_response") == 0 && strcmp(msg.status, "completed") == 0);
        EXPECT(msg.output_tools == 2 && msg.output_messages == 2);
        EXPECT(!msg.name[0] && !msg.call_id[0] && !msg.arguments[0]);
        EXPECT(!msg.transcript[0] && !msg.item_id[0] && !msg.item_type[0]);
        EXPECT(msg.truncated == 0);
    }
    EXPECT(parse_chunks(rx, "{\"type\":\"response.done\",\"response\":{\"output\":[]}}", 1, &msg));
    EXPECT(msg.output_tools == 0 && msg.output_messages == 0);
    EXPECT(!msg.response_id[0] && !msg.status[0]);
}

static void fill_utf8(char *out, size_t cap)
{
    size_t pos = 0;
    // Mixed code point lengths put retained boundaries inside a multibyte value.
    const char *unit = "a卧💪";
    while (pos + strlen(unit) < cap) {
        memcpy(out + pos, unit, strlen(unit));
        pos += strlen(unit);
    }
    out[pos] = '\0';
}

static bool utf8_prefix(const char *actual, const char *source)
{
    size_t len = strlen(actual);
    if (strncmp(actual, source, len) != 0) return false;
    if (!len) return true;
    size_t start = len - 1;
    while (start && ((unsigned char)actual[start] & 0xc0) == 0x80) start--;
    unsigned char lead = (unsigned char)actual[start];
    size_t width = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
    return len - start == width;
}

static void test_truncation_and_reset(step_rx_t *rx)
{
    const struct {
        const char *before;
        const char *after;
        size_t offset;
        size_t cap;
        unsigned flag;
    } cases[] = {
#define FIELD(member) offsetof(step_msg_t, member), sizeof(((step_msg_t *)0)->member)
        {"{\"type\":\"", "\"}", FIELD(type), STEP_RX_TRUNC_META},
        {"{\"name\":\"", "\"}", FIELD(name), STEP_RX_TRUNC_META},
        {"{\"call_id\":\"", "\"}", FIELD(call_id), STEP_RX_TRUNC_META},
        {"{\"type\":\"response.done\",\"event_id\":\"", "\"}", FIELD(event_id), STEP_RX_TRUNC_META},
        {"{\"session\":{\"id\":\"", "\"}}", FIELD(session_id), STEP_RX_TRUNC_META},
        {"{\"response\":{\"id\":\"", "\"}}", FIELD(response_id), STEP_RX_TRUNC_META},
        {"{\"response_id\":\"", "\"}", FIELD(response_id), STEP_RX_TRUNC_META},
        {"{\"item_id\":\"", "\"}", FIELD(item_id), STEP_RX_TRUNC_META},
        {"{\"response\":{\"status\":\"", "\"}}", FIELD(status), STEP_RX_TRUNC_META},
        {"{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"", "\"}}", FIELD(item_id), STEP_RX_TRUNC_META},
        {"{\"type\":\"response.output_item.done\",\"item\":{\"type\":\"", "\"}}", FIELD(item_type), STEP_RX_TRUNC_META},
        {"{\"type\":\"response.output_item.done\",\"item\":{\"name\":\"", "\"}}", FIELD(name), STEP_RX_TRUNC_META},
        {"{\"type\":\"response.output_item.done\",\"item\":{\"call_id\":\"", "\"}}", FIELD(call_id), STEP_RX_TRUNC_META},
        {"{\"error\":{\"type\":\"", "\"}}", FIELD(error_type), STEP_RX_TRUNC_ERROR},
        {"{\"error\":{\"code\":\"", "\"}}", FIELD(error_code), STEP_RX_TRUNC_ERROR},
        {"{\"error\":{\"message\":\"", "\"}}", FIELD(error_message), STEP_RX_TRUNC_ERROR},
        {"{\"error\":{\"event_id\":\"", "\"}}", FIELD(error_event_id), STEP_RX_TRUNC_ERROR},
        {"{\"arguments\":\"", "\"}", FIELD(arguments), STEP_RX_TRUNC_ARGS},
        {"{\"type\":\"response.output_item.done\",\"item\":{\"arguments\":\"", "\"}}", FIELD(arguments), STEP_RX_TRUNC_ARGS},
        {"{\"transcript\":\"", "\"}", FIELD(transcript), STEP_RX_TRUNC_TEXT},
        {"{\"text\":\"", "\"}", FIELD(transcript), STEP_RX_TRUNC_TEXT},
#undef FIELD
    };
    char content[STEP_TEXT_MAX * 3];
    char json[sizeof(content) + 256];
    fill_utf8(content, sizeof(content));
    step_msg_t msg;
    const size_t chunks[] = {1, 7, 31, 95, 96, 97, sizeof(json)};
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        int n = snprintf(json, sizeof(json), "%s%s%s", cases[c].before, content, cases[c].after);
        EXPECT(n > 0 && (size_t)n < sizeof(json));
        for (size_t k = 0; k < sizeof(chunks) / sizeof(chunks[0]); k++) {
            if (!parse_chunks(rx, json, chunks[k], &msg)) continue;
            const char *value = (const char *)&msg + cases[c].offset;
            EXPECT(strlen(value) > 0 && strlen(value) < cases[c].cap);
            EXPECT(utf8_prefix(value, content));
            EXPECT(msg.truncated == cases[c].flag);
            EXPECT(parse_chunks(rx, "{\"type\":\"response.created\"}", 1, &msg));
            expect_empty_trace(&msg);
        }
    }

    // Exact-capacity ASCII values are not falsely marked truncated.
    memset(content, 'x', STEP_TRACE_ID_MAX - 1);
    content[STEP_TRACE_ID_MAX - 1] = '\0';
    snprintf(json, sizeof(json), "{\"event_id\":\"%s\"}", content);
    EXPECT(parse_chunks(rx, json, 1, &msg));
    EXPECT(strcmp(msg.event_id, content) == 0 && msg.truncated == 0);

    fill_utf8(content, sizeof(content));
    char combined[sizeof(content) * 4 + 256];
    int n = snprintf(combined, sizeof(combined),
        "{\"event_id\":\"%s\",\"transcript\":\"%s\",\"arguments\":\"%s\",\"error\":{\"message\":\"%s\"}}",
        content, content, content, content);
    EXPECT(n > 0 && (size_t)n < sizeof(combined));
    EXPECT(parse_chunks(rx, combined, 1, &msg));
    EXPECT(msg.truncated == (STEP_RX_TRUNC_META | STEP_RX_TRUNC_TEXT | STEP_RX_TRUNC_ARGS | STEP_RX_TRUNC_ERROR));
    EXPECT(utf8_prefix(msg.event_id, content) && utf8_prefix(msg.transcript, content));
    EXPECT(utf8_prefix(msg.arguments, content) && utf8_prefix(msg.error_message, content));
    // Oversized ignored item data must not contaminate a non-output-item event.
    snprintf(json, sizeof(json), "{\"item\":{\"arguments\":\"%s\"},\"type\":\"conversation.item.created\"}", content);
    EXPECT(parse_chunks(rx, json, 1, &msg));
    expect_empty_trace(&msg);
    // Explicit reset during an unfinished value also clears trace/truncation state.
    EXPECT(step_rx_feed(rx, "{\"event_id\":\"unfinished", strlen("{\"event_id\":\"unfinished")));
    step_rx_reset(rx);
    EXPECT(parse_chunks(rx, "{\"type\":\"response.created\"}", 1, &msg));
    expect_empty_trace(&msg);
}

#define PCM_SAMPLES 4097
static int16_t expected_pcm[PCM_SAMPLES];
static char encoded_pcm[(PCM_SAMPLES * sizeof(int16_t) + 2) / 3 * 4 + 1];
static char audio_json[sizeof(encoded_pcm) + 1024];

typedef struct {
    size_t samples;
    bool mismatch;
} pcm_capture_t;

static void on_pcm(void *user, const int16_t *pcm, size_t samples)
{
    pcm_capture_t *capture = user;
    for (size_t i = 0; i < samples; i++) {
        if (capture->samples >= PCM_SAMPLES || pcm[i] != expected_pcm[capture->samples]) {
            capture->mismatch = true;
        }
        capture->samples++;
    }
}

static void test_large_audio(step_rx_t *rx, pcm_capture_t *capture)
{
    for (size_t i = 0; i < PCM_SAMPLES; i++) expected_pcm[i] = (int16_t)((int32_t)((i * 97u) % 60001u) - 30000);
    size_t n = b64_encode((const uint8_t *)expected_pcm, sizeof(expected_pcm), encoded_pcm, sizeof(encoded_pcm));
    EXPECT(n > 0 && n < sizeof(encoded_pcm));
    encoded_pcm[n] = '\0';
    int written = snprintf(audio_json, sizeof(audio_json),
        "{\"type\":\"response.audio.delta\",\"event_id\":\"audio_event\","
        "\"response_id\":\"audio_response\",\"item_id\":\"audio_item\",\"content_index\":0,"
        "\"metadata\":{\"delta\":\"AQD+/ywB\",\"audio\":\"AQD+/ywB\",\"text\":\"wrong\","
        "\"transcript\":\"wrong\",\"session\":{\"id\":\"wrong\"}},\"delta\":\"%s\","
        "\"response\":{\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"audio\","
        "\"delta\":\"AQD+/ywB\",\"text\":\"wrong\",\"transcript\":\"wrong\"}]}]}}", encoded_pcm);
    EXPECT(written > 0 && (size_t)written < sizeof(audio_json));
    const size_t chunks[] = {1, 2, 3, 5, 79, 96, 127, 1024, sizeof(audio_json)};
    step_msg_t msg;
    for (size_t k = 0; k < sizeof(chunks) / sizeof(chunks[0]); k++) {
        capture->samples = 0;
        capture->mismatch = false;
        EXPECT(parse_chunks(rx, audio_json, chunks[k], &msg));
        EXPECT(capture->samples == PCM_SAMPLES && !capture->mismatch);
        EXPECT(strcmp(msg.type, "response.audio.delta") == 0);
        EXPECT(strcmp(msg.event_id, "audio_event") == 0 && strcmp(msg.response_id, "audio_response") == 0);
        EXPECT(strcmp(msg.item_id, "audio_item") == 0);
        EXPECT(!msg.transcript[0] && !msg.arguments[0] && !msg.session_id[0]);
        EXPECT(msg.output_tools == 0 && msg.output_messages == 1 && msg.truncated == 0);
    }
    capture->samples = 0;
    EXPECT(parse_chunks(rx,
        "{\"type\":\"session.updated\",\"delta\":\"AQD+/ywB\",\"audio\":\"AQD+/ywB\","
        "\"session\":{\"delta\":\"AQD+/ywB\",\"text\":\"wrong\",\"transcript\":\"wrong\"}}", 1, &msg));
    EXPECT(capture->samples == 0);
    expect_empty_trace(&msg);
}

int main(void)
{
    pcm_capture_t capture = {0};
    step_rx_t *rx = step_rx_create(on_pcm, &capture);
    if (!rx) return 1;
    test_fragmented_identifiers(rx);
    test_paths_and_execution(rx);
    test_errors_and_asr(rx);
    test_response_output_counts(rx);
    EXPECT(capture.samples == 0);
    test_truncation_and_reset(rx);
    test_large_audio(rx, &capture);
    step_rx_destroy(rx);
    if (failures) fprintf(stderr, "%u step trace parser checks failed\n", failures);
    return failures ? 1 : 0;
}
