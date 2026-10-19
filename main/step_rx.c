#include "step_rx.h"

#include "b64_stream.h"
#include "qjson.h"

#include <stdlib.h>
#include <string.h>

enum {
    FIELD_NAME = 1u,
    FIELD_CALL = 2u,
    FIELD_ARGS = 4u,
    FIELD_ITEM = 8u,
    FIELD_RESPONSE = 16u,
};

struct step_rx {
    qj_parser_t parser;
    step_pcm_cb pcm_cb;
    void *user;
    b64_decoder_t b64;
    uint8_t pending;
    bool has_pending;
    bool decoding_audio;
    step_msg_t msg;
    unsigned root_fields;
    unsigned item_fields;
    unsigned item_truncated;
    char output_type[24];
    bool string_active;
    bool counting_output;
    bool dst_item;
    bool dst_clipped;
    char *dst;
    size_t dst_len;
    size_t dst_cap;
    unsigned dst_flag;
};

static void mark_truncated(step_rx_t *rx)
{
    rx->dst_clipped = true;
    if (rx->dst_item) rx->item_truncated |= rx->dst_flag;
    else rx->msg.truncated |= rx->dst_flag;
}

static void append_text(step_rx_t *rx, const char *data, size_t len)
{
    size_t room = rx->dst_cap - 1 - rx->dst_len;
    if (len > room) {
        mark_truncated(rx);
        len = room;
    }
    memcpy(rx->dst + rx->dst_len, data, len);
    rx->dst_len += len;
    rx->dst[rx->dst_len] = '\0';
}

static void finish_text(step_rx_t *rx)
{
    // qjson chunks may split UTF-8 code points. Trim only at value end, after
    // every fragment has had a chance to complete the retained final code point.
    if (!rx->dst_len) return;
    size_t start = rx->dst_len - 1;
    while (start && ((unsigned char)rx->dst[start] & 0xc0) == 0x80) start--;
    unsigned char lead = (unsigned char)rx->dst[start];
    size_t width = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
    if (rx->dst_len - start < width) {
        rx->dst_len = start;
        rx->dst[start] = '\0';
        mark_truncated(rx);
    }
}

static void select_text(step_rx_t *rx, char *dst, size_t cap, unsigned flag,
                        unsigned field, bool from_item)
{
    // Canonical root correlation/execution fields win over nested alternatives,
    // independent of JSON member order. Item fields are discarded at take() for
    // events other than response.output_item.*.
    if (from_item && (rx->root_fields & field)) return;
    if (from_item) rx->item_fields |= field;
    else {
        rx->root_fields |= field;
        rx->item_fields &= ~field;
    }
    rx->dst = dst;
    rx->dst_cap = cap;
    rx->dst_len = 0;
    rx->dst_flag = flag;
    rx->dst_item = from_item;
    rx->dst_clipped = false;
    dst[0] = '\0';
}

#define SELECT(member, flag, field, item) \
    select_text(rx, rx->msg.member, sizeof(rx->msg.member), flag, field, item)

static void select_value(step_rx_t *rx, qj_parser_t *parser)
{
    const int depth = qj_depth(parser);
    if (qj_in_array(parser, 1) || qj_in_array(parser, depth)) return;
    const char *key = qj_key(parser, depth);
    if (depth == 1) {
        if (strcmp(key, "delta") == 0 && strcmp(rx->msg.type, "response.audio.delta") == 0) {
            b64_decoder_init(&rx->b64);
            rx->has_pending = false;
            rx->decoding_audio = true;
        } else if (strcmp(key, "type") == 0) {
            SELECT(type, STEP_RX_TRUNC_META, 0, false);
        } else if (strcmp(key, "event_id") == 0) {
            SELECT(event_id, STEP_RX_TRUNC_META, 0, false);
        } else if (strcmp(key, "response_id") == 0) {
            SELECT(response_id, STEP_RX_TRUNC_META, FIELD_RESPONSE, false);
        } else if (strcmp(key, "item_id") == 0) {
            SELECT(item_id, STEP_RX_TRUNC_META, FIELD_ITEM, false);
        } else if (strcmp(key, "name") == 0) {
            SELECT(name, STEP_RX_TRUNC_META, FIELD_NAME, false);
        } else if (strcmp(key, "call_id") == 0) {
            SELECT(call_id, STEP_RX_TRUNC_META, FIELD_CALL, false);
        } else if (strcmp(key, "arguments") == 0) {
            SELECT(arguments, STEP_RX_TRUNC_ARGS, FIELD_ARGS, false);
        } else if (strcmp(key, "transcript") == 0 || strcmp(key, "text") == 0) {
            SELECT(transcript, STEP_RX_TRUNC_TEXT, 0, false);
        }
        return;
    }
    const char *parent = qj_key(parser, 1);
    if (depth == 2) {
        if (strcmp(parent, "session") == 0 && strcmp(key, "id") == 0) {
            SELECT(session_id, STEP_RX_TRUNC_META, 0, false);
        } else if (strcmp(parent, "response") == 0) {
            if (strcmp(key, "id") == 0 && !(rx->root_fields & FIELD_RESPONSE)) {
                SELECT(response_id, STEP_RX_TRUNC_META, 0, false);
            } else if (strcmp(key, "status") == 0) {
                SELECT(status, STEP_RX_TRUNC_META, 0, false);
            }
        } else if (strcmp(parent, "item") == 0) {
            if (strcmp(key, "id") == 0) {
                SELECT(item_id, STEP_RX_TRUNC_META, FIELD_ITEM, true);
            } else if (strcmp(key, "type") == 0) {
                SELECT(item_type, STEP_RX_TRUNC_META, 0, true);
            } else if (strcmp(key, "name") == 0) {
                SELECT(name, STEP_RX_TRUNC_META, FIELD_NAME, true);
            } else if (strcmp(key, "call_id") == 0) {
                SELECT(call_id, STEP_RX_TRUNC_META, FIELD_CALL, true);
            } else if (strcmp(key, "arguments") == 0) {
                SELECT(arguments, STEP_RX_TRUNC_ARGS, FIELD_ARGS, true);
            }
        } else if (strcmp(parent, "error") == 0) {
            if (strcmp(key, "type") == 0) {
                SELECT(error_type, STEP_RX_TRUNC_ERROR, 0, false);
            } else if (strcmp(key, "code") == 0) {
                SELECT(error_code, STEP_RX_TRUNC_ERROR, 0, false);
            } else if (strcmp(key, "message") == 0) {
                SELECT(error_message, STEP_RX_TRUNC_ERROR, 0, false);
            } else if (strcmp(key, "event_id") == 0) {
                SELECT(error_event_id, STEP_RX_TRUNC_ERROR, 0, false);
            }
        }
    } else if (depth == 3 && strcmp(parent, "response") == 0 &&
               strcmp(qj_key(parser, 2), "status_details") == 0) {
        if (strcmp(key, "reason") == 0 ||
            (strcmp(key, "type") == 0 && !rx->msg.status_reason[0]))
            SELECT(status_reason, STEP_RX_TRUNC_ERROR, 0, false);
    } else if (depth == 4 && strcmp(parent, "response") == 0 &&
               strcmp(qj_key(parser, 2), "status_details") == 0 &&
               strcmp(qj_key(parser, 3), "error") == 0) {
        if (strcmp(key, "type") == 0) SELECT(error_type, STEP_RX_TRUNC_ERROR, 0, false);
        else if (strcmp(key, "code") == 0) SELECT(error_code, STEP_RX_TRUNC_ERROR, 0, false);
        else if (strcmp(key, "message") == 0) SELECT(error_message, STEP_RX_TRUNC_ERROR, 0, false);
    } else if (depth == 4 && strcmp(parent, "response") == 0 &&
               strcmp(qj_key(parser, 2), "output") == 0 &&
               !qj_in_array(parser, 2) && qj_in_array(parser, 3) &&
               strcmp(key, "type") == 0) {
        rx->counting_output = true;
        select_text(rx, rx->output_type, sizeof(rx->output_type), 0, 0, false);
    }
}

#undef SELECT

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
    if (ev != QJ_EV_STR_CHUNK && ev != QJ_EV_STR_END) return;
    if (!rx->string_active) {
        rx->string_active = true;
        select_value(rx, parser);
    }
    if (ev == QJ_EV_STR_END) {
        if (rx->dst) finish_text(rx);
        if (rx->counting_output && !rx->dst_clipped) {
            if (strcmp(rx->output_type, "function_call") == 0) rx->msg.output_tools++;
            else if (strcmp(rx->output_type, "message") == 0) rx->msg.output_messages++;
        }
        rx->dst = NULL;
        rx->string_active = false;
        rx->decoding_audio = false;
        rx->counting_output = false;
        return;
    }
    if (rx->decoding_audio) {
        uint8_t raw[80];
        const size_t n = b64_decode_chunk(&rx->b64, data, len, raw, sizeof(raw));
        if (n) emit_pcm(rx, raw, n);
    } else if (rx->dst) {
        append_text(rx, data, len);
    }
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
    if (strcmp(rx->msg.type, "response.output_item.added") == 0 ||
        strcmp(rx->msg.type, "response.output_item.done") == 0) {
        rx->msg.truncated |= rx->item_truncated;
    } else {
        if (rx->item_fields & FIELD_NAME) rx->msg.name[0] = '\0';
        if (rx->item_fields & FIELD_CALL) rx->msg.call_id[0] = '\0';
        if (rx->item_fields & FIELD_ARGS) rx->msg.arguments[0] = '\0';
        if (rx->item_fields & FIELD_ITEM) rx->msg.item_id[0] = '\0';
        rx->msg.item_type[0] = '\0';
    }
    *msg = rx->msg;
    step_rx_reset(rx);
    return true;
}
