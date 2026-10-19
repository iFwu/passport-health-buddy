#include "step_rx.h"

#include "b64_stream.h"
#include "qjson.h"

#include <stdlib.h>
#include <string.h>

struct step_rx {
    qj_parser_t parser;
    step_pcm_cb pcm_cb;
    void *user;
    b64_decoder_t b64;
    uint8_t pending;
    bool has_pending;
    bool decoding_audio;
    char type[STEP_TYPE_MAX];
    char name[STEP_NAME_MAX];
    char call_id[STEP_ID_MAX];
    char arguments[STEP_ARGS_MAX];
    char transcript[STEP_TEXT_MAX];
    char *dst;
    size_t dst_len;
    size_t dst_cap;
};

static void append_text(step_rx_t *rx, const char *data, size_t len)
{
    size_t room = rx->dst_cap - 1 - rx->dst_len;
    if (len > room) len = room;
    memcpy(rx->dst + rx->dst_len, data, len);
    rx->dst_len += len;
    rx->dst[rx->dst_len] = '\0';
}

static void emit_pcm(step_rx_t *rx, const uint8_t *raw, size_t n)
{
    uint8_t joined[128];
    size_t count = 0;
    if (rx->has_pending) joined[count++] = rx->pending;
    if (n > sizeof(joined) - 1) n = sizeof(joined) - 1;
    memcpy(joined + count, raw, n);
    count += n;
    size_t even = count & ~1u;
    if (even && rx->pcm_cb) rx->pcm_cb(rx->user, (const int16_t *)joined, even / 2);
    rx->has_pending = count != even;
    if (rx->has_pending) rx->pending = joined[count - 1];
}

static void on_json(void *user, qj_parser_t *parser, qj_event_t ev, const char *data, size_t len)
{
    step_rx_t *rx = user;
    if (qj_depth(parser) != 1) return;
    if (ev == QJ_EV_STR_END) {
        rx->dst = NULL;
        rx->decoding_audio = false;
        return;
    }
    if (ev != QJ_EV_STR_CHUNK) return;
    const char *key = qj_key(parser, 1);
    if (strcmp(key, "delta") == 0 && strcmp(rx->type, "response.audio.delta") == 0) {
        uint8_t raw[80];
        if (!rx->decoding_audio) {
            b64_decoder_init(&rx->b64);
            rx->has_pending = false;
            rx->decoding_audio = true;
        }
        const size_t n = b64_decode_chunk(&rx->b64, data, len, raw, sizeof(raw));
        if (n) emit_pcm(rx, raw, n);
        return;
    }
    if (rx->dst) {
        append_text(rx, data, len);
        return;
    }
    if (strcmp(key, "type") == 0) {
        rx->dst = rx->type;
        rx->dst_cap = sizeof(rx->type);
    } else if (strcmp(key, "name") == 0) {
        rx->dst = rx->name;
        rx->dst_cap = sizeof(rx->name);
    } else if (strcmp(key, "call_id") == 0) {
        rx->dst = rx->call_id;
        rx->dst_cap = sizeof(rx->call_id);
    } else if (strcmp(key, "arguments") == 0) {
        rx->dst = rx->arguments;
        rx->dst_cap = sizeof(rx->arguments);
    } else if (strcmp(key, "transcript") == 0) {
        rx->dst = rx->transcript;
        rx->dst_cap = sizeof(rx->transcript);
    } else {
        return;
    }
    rx->dst_len = 0;
    rx->dst[0] = '\0';
    append_text(rx, data, len);
}

step_rx_t *step_rx_create(step_pcm_cb cb, void *user)
{
    step_rx_t *rx = calloc(1, sizeof(*rx));
    if (!rx) return NULL;
    rx->pcm_cb = cb;
    rx->user = user;
    qj_init(&rx->parser, on_json, rx);
    return rx;
}

void step_rx_destroy(step_rx_t *rx)
{
    free(rx);
}

void step_rx_reset(step_rx_t *rx)
{
    step_pcm_cb cb = rx->pcm_cb;
    void *user = rx->user;
    memset(rx, 0, sizeof(*rx));
    rx->pcm_cb = cb;
    rx->user = user;
    qj_init(&rx->parser, on_json, rx);
}

bool step_rx_feed(step_rx_t *rx, const char *data, size_t len)
{
    return qj_feed(&rx->parser, data, len);
}

bool step_rx_take(step_rx_t *rx, step_msg_t *msg)
{
    if (!qj_done(&rx->parser)) return false;
    memset(msg, 0, sizeof(*msg));
    memcpy(msg->type, rx->type, sizeof(msg->type));
    memcpy(msg->name, rx->name, sizeof(msg->name));
    memcpy(msg->call_id, rx->call_id, sizeof(msg->call_id));
    memcpy(msg->arguments, rx->arguments, sizeof(msg->arguments));
    memcpy(msg->transcript, rx->transcript, sizeof(msg->transcript));
    step_rx_reset(rx);
    return true;
}
