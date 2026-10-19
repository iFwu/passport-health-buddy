#include "voice_trace.h"

#include <string.h>

typedef struct {
    char *out;
    size_t cap;
    size_t len;
    const char *secret;
    size_t secret_len;
    bool failed;
} trace_writer_t;

static void put_char(trace_writer_t *w, char c)
{
    if (w->failed) return;
    if (w->len >= w->cap - 1) {
        w->failed = true;
        return;
    }
    w->out[w->len++] = c;
}

static void put_literal(trace_writer_t *w, const char *s)
{
    while (*s && !w->failed) put_char(w, *s++);
}

static void put_escaped_byte(trace_writer_t *w, unsigned char c)
{
    static const char hex[] = "0123456789abcdef";
    switch (c) {
    case '"': put_literal(w, "\\\""); break;
    case '\\': put_literal(w, "\\\\"); break;
    case '\b': put_literal(w, "\\b"); break;
    case '\f': put_literal(w, "\\f"); break;
    case '\n': put_literal(w, "\\n"); break;
    case '\r': put_literal(w, "\\r"); break;
    case '\t': put_literal(w, "\\t"); break;
    default:
        if (c < 0x20) {
            put_literal(w, "\\u00");
            put_char(w, hex[c >> 4]);
            put_char(w, hex[c & 15]);
        } else {
            put_char(w, (char)c);
        }
        break;
    }
}

static void put_string(trace_writer_t *w, const char *s)
{
    put_char(w, '"');
    while (*s && !w->failed) {
        if (w->secret_len && strncmp(s, w->secret, w->secret_len) == 0) {
            put_literal(w, "<redacted>");
            s += w->secret_len;
        } else {
            put_escaped_byte(w, (unsigned char)*s++);
        }
    }
    put_char(w, '"');
}

static void put_key(trace_writer_t *w, const char *key)
{
    put_char(w, ',');
    put_char(w, '"');
    put_literal(w, key);
    put_literal(w, "\":");
}

static void put_uint(trace_writer_t *w, uint64_t value)
{
    if (w->failed) return;
    char digits[20];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n && !w->failed) put_char(w, digits[--n]);
}

static void put_hex_string(trace_writer_t *w, uint32_t value)
{
    if (w->failed) return;
    static const char hex[] = "0123456789abcdef";
    char digits[9];
    for (size_t i = 0; i < 8; i++) {
        digits[7 - i] = hex[value & 15];
        value >>= 4;
    }
    digits[8] = '\0';
    put_string(w, digits);
}

static void put_metadata(trace_writer_t *w, const char *key, const char *value)
{
    if (value && *value) {
        put_key(w, key);
        put_string(w, value);
    }
}

static void put_content(trace_writer_t *w, const char *key, const char *value)
{
    if (value) {
        put_key(w, key);
        put_string(w, value);
    }
}

static void put_metric(trace_writer_t *w, const char *key, unsigned value)
{
    put_key(w, key);
    put_uint(w, value);
}

uint32_t voice_trace_fingerprint(const char *text)
{
    uint32_t hash = UINT32_C(2166136261);
    if (text) {
        while (*text) {
            hash ^= (unsigned char)*text++;
            hash *= UINT32_C(16777619);
        }
    }
    return hash;
}

size_t voice_trace_format(char *out, size_t cap, const voice_trace_event_t *e,
                          bool include_content, const char *secret)
{
    if (out && cap) out[0] = '\0';
    if (!out || !cap || !e || !e->stage || !*e->stage) return 0;

    trace_writer_t w = {
        .out = out,
        .cap = cap,
        .len = 1,
        .secret = secret,
        .secret_len = secret ? strlen(secret) : 0,
    };
    /* Keep the first byte NUL until success so a failed event never exposes a
     * partial JSON string. The opening brace is committed only at the end. */
    put_literal(&w, "\"run\":");
    put_hex_string(&w, e->run);
    put_key(&w, "turn");
    put_uint(&w, e->turn);
    put_key(&w, "ms");
    put_uint(&w, e->ms);
    put_key(&w, "stage");
    put_string(&w, e->stage);
    if (e->config) {
        put_key(&w, "config");
        put_hex_string(&w, e->config);
    }
    put_metadata(&w, "session", e->session);
    put_metadata(&w, "input", e->input);
    put_metadata(&w, "response", e->response);
    put_metadata(&w, "event", e->event);
    put_metadata(&w, "call", e->call);
    put_metadata(&w, "name", e->name);
    put_metadata(&w, "status", e->status);
    put_metadata(&w, "reason", e->reason);
    if (include_content) {
        put_content(&w, "text", e->text);
        put_content(&w, "args", e->args);
        put_content(&w, "result", e->result);
    } else if (e->text || e->args || e->result) {
        put_literal(&w, ",\"content_redacted\":true");
    }
    if (e->truncated) put_literal(&w, ",\"truncated\":true");
    if (e->has_metrics) {
        put_literal(&w, ",\"metrics\":{\"record_ms\":");
        put_uint(&w, e->record_ms);
        put_metric(&w, "up_samples", e->up_samples);
        put_metric(&w, "up_frames", e->up_frames);
        put_metric(&w, "received_samples", e->received_samples);
        put_metric(&w, "queued_samples", e->queued_samples);
        put_metric(&w, "played_samples", e->played_samples);
        put_metric(&w, "dropped_samples", e->dropped_samples);
        put_metric(&w, "volume", e->volume);
        put_metric(&w, "output_tools", e->output_tools);
        put_metric(&w, "output_messages", e->output_messages);
        put_char(&w, '}');
    }
    put_char(&w, '}');
    if (w.failed) return 0;
    out[w.len] = '\0';
    out[0] = '{';
    return w.len;
}
