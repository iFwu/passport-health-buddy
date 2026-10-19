#include "voice_trace.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool emit_json;

/* Emit actual/expected JSON pairs for an independent semantic comparison.
 * Field order is not part of the public trace contract. */
static void expect_format(const voice_trace_event_t *e, bool content,
                          const char *secret, const char *expected)
{
    char out[4096];
    memset(out, 'X', sizeof(out));
    size_t n = voice_trace_format(out, sizeof(out), e, content, secret);
    assert(n > 0 && n == strlen(out));
    assert(out[n] == '\0');
    assert(out[n + 1] == 'X');
    assert(strchr(out, '\n') == NULL);
    assert(strchr(out, '\r') == NULL);
    if (emit_json) printf("{\"actual\":%s,\"expected\":%s}\n", out, expected);
}

static void test_fingerprint(void)
{
    assert(voice_trace_fingerprint(NULL) == UINT32_C(0x811c9dc5));
    assert(voice_trace_fingerprint("") == UINT32_C(0x811c9dc5));
    assert(voice_trace_fingerprint("a") == UINT32_C(0xe40c292c));
    assert(voice_trace_fingerprint("hello") == UINT32_C(0x4f9f2cab));
    assert(voice_trace_fingerprint("foobar") == UINT32_C(0xbf9cf968));
}

static void test_escaping_and_metrics(void)
{
    voice_trace_event_t e = {
        .run = UINT32_C(0x12ab), .turn = 7, .config = UINT32_C(0x89abcdef),
        .ms = UINT64_C(1234567890123), .stage = "response.done",
        .session = "s-1", .input = "i-2", .response = "r-3", .event = "e-4",
        .call = "c-5", .name = "record_gym", .status = "completed",
        .reason = "quote\" slash\\", .text = "卧推60公斤8个\nI (0) forged\r\n\"} {\"x\":1}\t\b\f\001\037",
        .args = "{\"nested\":{\"weight\":60},\"list\":[1,2]}",
        .result = "{\"ok\":true}\nnext", .truncated = true, .has_metrics = true,
        .record_ms = 1000, .up_samples = 16000, .up_frames = 50,
        .received_samples = 32000, .queued_samples = 30000,
        .played_samples = 28000, .dropped_samples = 2000, .volume = 70,
        .output_tools = 1, .output_messages = 2,
    };
    expect_format(&e, true, NULL,
        "{\"run\":\"000012ab\",\"turn\":7,\"ms\":1234567890123,\"stage\":\"response.done\","
        "\"config\":\"89abcdef\",\"session\":\"s-1\",\"input\":\"i-2\",\"response\":\"r-3\","
        "\"event\":\"e-4\",\"call\":\"c-5\",\"name\":\"record_gym\",\"status\":\"completed\","
        "\"reason\":\"quote\\\" slash\\\\\",\"text\":\"卧推60公斤8个\\nI (0) forged\\r\\n"
        "\\\"} {\\\"x\\\":1}\\t\\b\\f\\u0001\\u001f\","
        "\"args\":\"{\\\"nested\\\":{\\\"weight\\\":60},\\\"list\\\":[1,2]}\","
        "\"result\":\"{\\\"ok\\\":true}\\nnext\",\"truncated\":true,"
        "\"metrics\":{\"record_ms\":1000,\"up_samples\":16000,\"up_frames\":50,"
        "\"received_samples\":32000,\"queued_samples\":30000,\"played_samples\":28000,"
        "\"dropped_samples\":2000,\"volume\":70,\"output_tools\":1,\"output_messages\":2}}");
}

static void test_content_switch_and_turns(void)
{
    voice_trace_event_t e = {
        .run = 42, .turn = 1, .ms = 9, .stage = "asr.final", .session = "s",
        .text = "private words", .args = "{\"private\":1}", .result = "private result",
    };
    expect_format(&e, false, NULL,
        "{\"run\":\"0000002a\",\"turn\":1,\"ms\":9,\"stage\":\"asr.final\","
        "\"session\":\"s\",\"content_redacted\":true}");
    /* A new turn has neither content nor identifiers from the preceding event.
     * Empty metadata is omitted. */
    e = (voice_trace_event_t){.run = 42, .turn = 2, .stage = "record.start", .input = ""};
    expect_format(&e, false, NULL,
        "{\"run\":\"0000002a\",\"turn\":2,\"ms\":0,\"stage\":\"record.start\"}");
    e.text = "";
    e.args = "";
    e.result = "";
    expect_format(&e, true, "",
        "{\"run\":\"0000002a\",\"turn\":2,\"ms\":0,\"stage\":\"record.start\","
        "\"text\":\"\",\"args\":\"\",\"result\":\"\"}");
    expect_format(&e, false, "",
        "{\"run\":\"0000002a\",\"turn\":2,\"ms\":0,\"stage\":\"record.start\","
        "\"content_redacted\":true}");
}

static void test_secret_scrubbing(void)
{
    const char *secret = "SENTINEL_KEY";
    voice_trace_event_t e = {
        .stage = "preSENTINEL_KEYpost", .session = secret, .input = secret,
        .response = secret, .event = secret, .call = secret, .name = secret,
        .status = secret, .reason = "first SENTINEL_KEYSENTINEL_KEY last",
        .text = "SENTINEL_KEY 中文",
        .args = "{\"outer\":{\"key\":\"SENTINEL_KEY\"}}",
        .result = "{\"items\":[\"SENTINEL_KEY\",\"SENTINEL_KEY\"]}",
    };
    expect_format(&e, true, secret,
        "{\"run\":\"00000000\",\"turn\":0,\"ms\":0,\"stage\":\"pre<redacted>post\","
        "\"session\":\"<redacted>\",\"input\":\"<redacted>\",\"response\":\"<redacted>\","
        "\"event\":\"<redacted>\",\"call\":\"<redacted>\",\"name\":\"<redacted>\","
        "\"status\":\"<redacted>\",\"reason\":\"first <redacted><redacted> last\","
        "\"text\":\"<redacted> 中文\",\"args\":\"{\\\"outer\\\":{\\\"key\\\":\\\"<redacted>\\\"}}\","
        "\"result\":\"{\\\"items\\\":[\\\"<redacted>\\\",\\\"<redacted>\\\"]}\"}");
    expect_format(&e, false, secret,
        "{\"run\":\"00000000\",\"turn\":0,\"ms\":0,\"stage\":\"pre<redacted>post\","
        "\"session\":\"<redacted>\",\"input\":\"<redacted>\",\"response\":\"<redacted>\","
        "\"event\":\"<redacted>\",\"call\":\"<redacted>\",\"name\":\"<redacted>\","
        "\"status\":\"<redacted>\",\"reason\":\"first <redacted><redacted> last\","
        "\"content_redacted\":true}");
    e = (voice_trace_event_t){
        .stage = "metadata", .run = UINT32_C(0xdeadbeef),
        .config = UINT32_C(0xdeadbeef), .reason = "a\"\\\nb", .text = "a\"\\\nb",
    };
    expect_format(&e, true, "a\"\\\nb",
        "{\"run\":\"deadbeef\",\"turn\":0,\"ms\":0,\"stage\":\"metadata\","
        "\"config\":\"deadbeef\",\"reason\":\"<redacted>\",\"text\":\"<redacted>\"}");
    e.reason = NULL;
    e.text = NULL;
    expect_format(&e, false, "deadbeef",
        "{\"run\":\"<redacted>\",\"turn\":0,\"ms\":0,\"stage\":\"metadata\","
        "\"config\":\"<redacted>\"}");
    e = (voice_trace_event_t){.stage = "raw", .text = "SENTINEL_KEY"};
    expect_format(&e, true, "",
        "{\"run\":\"00000000\",\"turn\":0,\"ms\":0,\"stage\":\"raw\",\"text\":\"SENTINEL_KEY\"}");
}

static void test_integer_boundaries(void)
{
    voice_trace_event_t e = {
        .run = UINT32_MAX, .turn = UINT32_MAX, .config = UINT32_MAX,
        .ms = UINT64_MAX, .stage = "max",
    };
    expect_format(&e, true, NULL,
        "{\"run\":\"ffffffff\",\"turn\":4294967295,\"ms\":18446744073709551615,"
        "\"stage\":\"max\",\"config\":\"ffffffff\"}");
    e = (voice_trace_event_t){.stage = "metrics", .has_metrics = true, .record_ms = UINT_MAX};
    char expected[512];
    int n = snprintf(expected, sizeof(expected),
        "{\"run\":\"00000000\",\"turn\":0,\"ms\":0,\"stage\":\"metrics\",\"metrics\":{"
        "\"record_ms\":%u,\"up_samples\":0,\"up_frames\":0,\"received_samples\":0,"
        "\"queued_samples\":0,\"played_samples\":0,\"dropped_samples\":0,\"volume\":0,"
        "\"output_tools\":0,\"output_messages\":0}}", UINT_MAX);
    assert(n > 0 && (size_t)n < sizeof(expected));
    expect_format(&e, false, NULL, expected);
}

static void test_all_controls(void)
{
    voice_trace_event_t e = {
        .stage = "controls",
        .text = "\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017"
                "\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037",
    };
    expect_format(&e, true, NULL,
        "{\"run\":\"00000000\",\"turn\":0,\"ms\":0,\"stage\":\"controls\",\"text\":\""
        "\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\b\\t\\n\\u000b\\f\\r"
        "\\u000e\\u000f\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017"
        "\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f\"}");
}

static void test_capacity_and_invalid_events(void)
{
    voice_trace_event_t e = {
        .run = 1, .turn = 1, .stage = "capacity", .text = "卧推\n\"\\",
        .args = "{\"a\":\"SENTINEL_KEY\"}", .result = "done", .has_metrics = true,
    };
    char expected[4096];
    size_t needed = voice_trace_format(expected, sizeof(expected), &e, true, "SENTINEL_KEY");
    assert(needed > 0);
    char out[4096];
    for (size_t cap = 0; cap <= needed; cap++) {
        memset(out, 'X', sizeof(out));
        assert(voice_trace_format(out, cap, &e, true, "SENTINEL_KEY") == 0);
        if (cap) assert(out[0] == '\0');
        assert(out[cap] == 'X');
    }
    memset(out, 'X', sizeof(out));
    assert(voice_trace_format(out, needed + 1, &e, true, "SENTINEL_KEY") == needed);
    assert(strcmp(out, expected) == 0);
    assert(out[needed + 1] == 'X');
    /* Reuse this exact buffer for a shorter next turn, then a failed event. */
    e = (voice_trace_event_t){.stage = "next", .turn = 2};
    assert(voice_trace_format(out, sizeof(out), &e, false, NULL) > 0);
    assert(strcmp(out, "{\"run\":\"00000000\",\"turn\":2,\"ms\":0,\"stage\":\"next\"}") == 0);
    assert(strstr(out, "SENTINEL") == NULL && strstr(out, "text") == NULL);
    assert(voice_trace_format(NULL, 0, &e, true, NULL) == 0);
    assert(voice_trace_format(NULL, sizeof(out), &e, true, NULL) == 0);
    assert(voice_trace_format(out, sizeof(out), NULL, true, NULL) == 0 && !out[0]);
    e.stage = NULL;
    out[0] = 'X';
    assert(voice_trace_format(out, sizeof(out), &e, true, NULL) == 0 && !out[0]);
    e.stage = "";
    out[0] = 'X';
    assert(voice_trace_format(out, sizeof(out), &e, true, NULL) == 0 && !out[0]);
}

int main(int argc, char **argv)
{
    assert(argc == 1 || (argc == 2 && strcmp(argv[1], "--json-lines") == 0));
    emit_json = argc == 2;
    test_fingerprint();
    test_escaping_and_metrics();
    test_content_switch_and_turns();
    test_secret_scrubbing();
    test_integer_boundaries();
    test_all_controls();
    test_capacity_and_invalid_events();
    if (!emit_json) puts("Voice trace: PASS (JSON strings, redaction, identifiers, metrics, bounds)");
    return 0;
}
