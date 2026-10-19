#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t run;
    uint32_t turn;
    uint32_t config;
    uint64_t ms;
    const char *stage;
    const char *session;
    const char *input;
    const char *response;
    const char *event;
    const char *call;
    const char *name;
    const char *status;
    const char *reason;
    const char *text;
    const char *args;
    const char *result;
    bool truncated;
    bool has_metrics;
    unsigned record_ms;
    unsigned up_samples;
    unsigned up_frames;
    unsigned received_samples;
    unsigned queued_samples;
    unsigned played_samples;
    unsigned dropped_samples;
    unsigned volume;
    unsigned output_tools;
    unsigned output_messages;
} voice_trace_event_t;

/* FNV-1a over the NUL-terminated bytes, with NULL treated as empty. This stable
 * 32-bit configuration identifier is not a cryptographic hash or authenticator. */
uint32_t voice_trace_fingerprint(const char *text);

/* Write one complete JSON object (no trailing newline); return its byte length,
 * excluding the terminating NUL. Return 0 for insufficient capacity, NULL out/e,
 * or a NULL/empty stage; clear out[0] whenever out != NULL and cap > 0.
 *
 * Metadata strings are omitted when NULL/empty. Non-NULL text/args/result count
 * as provided content, including empty strings: serialize them only when
 * include_content, otherwise emit content_redacted=true. All string VALUES,
 * including stage, run/config identifiers and content, have literal occurrences
 * of a nonempty secret replaced with <redacted> before JSON escaping.
 * Valid UTF-8 input is retained. Inputs must not alias out and must remain
 * unchanged during the call. No allocation or retained per-event state. */
size_t voice_trace_format(char *out, size_t cap, const voice_trace_event_t *e,
                          bool include_content, const char *secret);
