#include "step_trace.h"
#include "voice_trace.h"
#include "health_cfg.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define TURN_CAP 4
#define LINK_CAP 8

typedef struct {
    uint32_t id;
    char input[STEP_TRACE_ID_MAX];
    uint64_t mic_ms;
    bool mic_ready;
    unsigned record_ms, up_samples, up_frames;
    unsigned received_samples, queued_samples, played_samples, dropped_samples, volume;
} turn_t;

typedef struct {
    char id[STEP_TRACE_ID_MAX];
    uint32_t turn;
} link_t;

static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;
static uint32_t s_run, s_config, s_next_turn, s_active_turn, s_event_sequence;
static turn_t s_turns[TURN_CAP];
static link_t s_responses[LINK_CAP], s_calls[LINK_CAP], s_requests[LINK_CAP];
static unsigned s_response_slot, s_call_slot, s_request_slot;
static uint32_t s_input_pending[TURN_CAP], s_response_pending[TURN_CAP];
static unsigned s_input_count, s_response_count;
static bool s_input_overflow, s_response_overflow;
static uint32_t s_audio_turn;
static char s_session[STEP_TRACE_ID_MAX];
static char s_audio_response[STEP_TRACE_ID_MAX];
static char s_secret[HEALTH_KEY_MAX + 1];
static char s_line[2048];

static unsigned s_output_volume;
static uint64_t clock_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000;
}

static bool lock(void)
{
    return s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE;
}

static void unlock(void)
{
    xSemaphoreGive(s_mutex);
}

static turn_t *find_turn(uint32_t id)
{
    turn_t *turn = &s_turns[id % TURN_CAP];
    return id && turn->id == id ? turn : NULL;
}

static uint32_t find_link(const link_t *links, const char *id)
{
    if (!id || !id[0]) return 0;
    for (unsigned i = 0; i < LINK_CAP; i++) {
        if (strcmp(links[i].id, id) == 0 && find_turn(links[i].turn)) return links[i].turn;
    }
    return 0;
}

static void bind_link(link_t *links, unsigned *slot, const char *id, uint32_t turn)
{
    if (!id[0] || !turn) return;
    link_t *link = &links[(*slot)++ % LINK_CAP];
    snprintf(link->id, sizeof(link->id), "%s", id);
    link->turn = turn;
}

static void pending(uint32_t *queue, unsigned *count, bool *overflow, uint32_t turn)
{
    if (*overflow) return;
    if (*count == TURN_CAP) {
        // Once an expectation is lost, later replies cannot safely be shifted forward.
        *overflow = true;
        *count = 0;
        return;
    }
    queue[(*count)++] = turn;
}

static uint32_t consume(uint32_t *queue, unsigned *count, bool overflow)
{
    if (overflow || !*count) return 0;
    uint32_t turn = queue[0];
    (*count)--;
    memmove(queue, queue + 1, *count * sizeof(*queue));
    return find_turn(turn) ? turn : 0;
}

static voice_trace_event_t event(uint32_t id, const char *stage)
{
    voice_trace_event_t e = {
        .run = s_run, .turn = id, .ms = clock_ms(), .config = s_config,
        .stage = stage, .session = s_session,
    };
    turn_t *turn = find_turn(id);
    if (turn) e.input = turn->input;
    return e;
}

static void metrics(voice_trace_event_t *e)
{
    turn_t *turn = find_turn(e->turn);
    if (!turn) return;
    e->has_metrics = true;
    e->record_ms = turn->record_ms;
    e->up_samples = turn->up_samples;
    e->up_frames = turn->up_frames;
    e->received_samples = turn->received_samples;
    e->queued_samples = turn->queued_samples;
    e->played_samples = turn->played_samples;
    e->dropped_samples = turn->dropped_samples;
    e->volume = turn->volume;
}

static void emit(voice_trace_event_t *e)
{
#ifdef CONFIG_HEALTH_VOICE_TRACE_TEXT
    const bool content = true;
#else
    const bool content = false;
#endif
    if (voice_trace_format(s_line, sizeof(s_line), e, content, s_secret)) {
        ESP_LOGI("voice_trace", "%s", s_line);
    } else {
        ESP_LOGE("voice_trace", "{\"run\":\"%08" PRIx32 "\",\"turn\":%" PRIu32 ",\"stage\":\"trace.format_error\"}", s_run, e->turn);
    }
}

void step_trace_init(const char *model, const char *configuration, const char *secret)
{
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    s_run = esp_random();
    s_config = voice_trace_fingerprint(configuration);
    snprintf(s_secret, sizeof(s_secret), "%s", secret ? secret : "");
    if (!lock()) return;
    voice_trace_event_t e = event(0, "session.config");
    e.name = model;
#ifdef CONFIG_HEALTH_VOICE_TRACE_TEXT
    e.status = "content_enabled";
#else
    e.status = "metadata_only";
#endif
    emit(&e);
    unlock();
}

uint32_t step_trace_begin(void)
{
    if (!lock()) return 0;
    if (++s_next_turn == 0) s_next_turn++;
    s_active_turn = s_next_turn;
    turn_t *turn = &s_turns[s_active_turn % TURN_CAP];
    memset(turn, 0, sizeof(*turn));
    turn->id = s_active_turn;
    turn->volume = s_output_volume;
    voice_trace_event_t e = event(turn->id, "turn.begin");
    emit(&e);
    uint32_t id = turn->id;
    unlock();
    return id;
}

void step_trace_mic_ready(void)
{
    if (!lock()) return;
    turn_t *turn = find_turn(s_active_turn);
    if (turn) { turn->mic_ready = true; turn->mic_ms = clock_ms(); }
    voice_trace_event_t e = event(s_active_turn, "mic.ready");
    emit(&e);
    unlock();
}

void step_trace_upload(size_t samples, bool sent)
{
    if (!lock()) return;
    turn_t *turn = find_turn(s_active_turn);
    if (turn && sent) { turn->up_samples += samples; turn->up_frames++; }
    if (!sent) {
        voice_trace_event_t e = event(s_active_turn, "input.upload_failed");
        e.status = "error";
        metrics(&e);
        emit(&e);
    }
    unlock();
}

void step_trace_expect_input(uint32_t turn)
{
    if (!lock()) return;
    pending(s_input_pending, &s_input_count, &s_input_overflow, turn);
    turn_t *t = find_turn(turn);
    if (t && t->mic_ready) t->record_ms = (unsigned)(clock_ms() - t->mic_ms);
    unlock();
}

void step_trace_expect_response(uint32_t turn)
{
    if (!lock()) return;
    pending(s_response_pending, &s_response_count, &s_response_overflow, turn);
    unlock();
}

void step_trace_event_id(char *out, size_t capacity)
{
    if (!out || !capacity) return;
    out[0] = '\0';
    if (!lock()) return;
    snprintf(out, capacity, "hb-%08" PRIx32 "-%" PRIu32, s_run, ++s_event_sequence);
    unlock();
}

void step_trace_sent(uint32_t turn, const char *stage, const char *event_id,
                     const char *call_id, bool sent)
{
    if (!lock()) return;
    if (event_id) bind_link(s_requests, &s_request_slot, event_id, turn);
    voice_trace_event_t e = event(turn, stage);
    e.event = event_id;
    e.call = call_id;
    e.status = sent ? "sent" : "send_failed";
    metrics(&e);
    emit(&e);
    unlock();
}

uint32_t step_trace_server(const step_msg_t *msg)
{
    if (!lock()) return 0;
    if (msg->session_id[0]) snprintf(s_session, sizeof(s_session), "%s", msg->session_id);
    uint32_t turn = 0;
    if (strcmp(msg->type, "input_audio_buffer.committed") == 0) {
        turn = consume(s_input_pending, &s_input_count, s_input_overflow);
        turn_t *t = find_turn(turn);
        if (t) snprintf(t->input, sizeof(t->input), "%s", msg->item_id);
    } else if (strcmp(msg->type, "response.created") == 0) {
        turn = consume(s_response_pending, &s_response_count, s_response_overflow);
        bind_link(s_responses, &s_response_slot, msg->response_id, turn);
        s_audio_turn = turn;
        snprintf(s_audio_response, sizeof(s_audio_response), "%s", msg->response_id);
    }
    if (!turn) turn = find_link(s_responses, msg->response_id);
    if (!turn) turn = find_link(s_calls, msg->call_id);
    if (!turn) turn = find_link(s_requests, msg->error_event_id);
    if (!turn && msg->item_id[0]) {
        for (unsigned i = 0; i < TURN_CAP; i++) {
            if (strcmp(s_turns[i].input, msg->item_id) == 0) { turn = s_turns[i].id; break; }
        }
    }
    const bool response_event = strncmp(msg->type, "response.", 9) == 0;
    if (!turn && response_event && !msg->response_id[0] && !msg->call_id[0] && !msg->item_id[0])
        turn = find_turn(s_audio_turn) ? s_audio_turn : 0;
    if (msg->call_id[0] && turn) bind_link(s_calls, &s_call_slot, msg->call_id, turn);
    const bool delta = strstr(msg->type, ".delta") != NULL;
    const bool boundary = strcmp(msg->type, "session.created") == 0 ||
        strcmp(msg->type, "session.updated") == 0 ||
        strcmp(msg->type, "input_audio_buffer.committed") == 0 ||
        strcmp(msg->type, "conversation.item.input_audio_transcription.completed") == 0 ||
        strcmp(msg->type, "response.created") == 0 ||
        strcmp(msg->type, "response.done") == 0 ||
        strcmp(msg->type, "response.output_item.added") == 0 ||
        strcmp(msg->type, "response.output_item.done") == 0 ||
        strcmp(msg->type, "response.audio_transcript.done") == 0 ||
        strcmp(msg->type, "response.text.done") == 0 ||
        strcmp(msg->type, "response.function_call_arguments.done") == 0 ||
        strcmp(msg->type, "error") == 0;
    if (boundary && !delta) {
        voice_trace_event_t e = event(turn, msg->type);
        e.event = msg->event_id;
        e.response = msg->response_id;
        e.call = msg->call_id;
        e.name = msg->name;
        e.status = msg->status;
        e.reason = msg->status_reason[0] ? msg->status_reason : msg->item_type;
        e.text = msg->transcript[0] ? msg->transcript : NULL;
        e.args = msg->arguments[0] ? msg->arguments : NULL;
        if (!e.input && strstr(msg->type, "input_audio_transcription")) e.input = msg->item_id;
        if (msg->error_message[0]) e.text = msg->error_message;
        if (msg->error_code[0]) e.reason = msg->error_code;
        e.truncated = msg->truncated != 0;
        metrics(&e);
        e.output_tools = msg->output_tools;
        e.output_messages = msg->output_messages;
        if (strcmp(msg->type, "response.done") == 0) e.has_metrics = true;
        if (strcmp(msg->type, "error") == 0) {
            e.status = msg->error_type;
            e.reason = msg->error_code;
            e.text = msg->error_message;
            e.event = msg->error_event_id[0] ? msg->error_event_id : msg->event_id;
        }
        emit(&e);
    }
    unlock();
    return turn;
}

uint32_t step_trace_call_turn(const char *call_id)
{
    if (!lock()) return 0;
    uint32_t turn = find_link(s_calls, call_id);
    unlock();
    return turn;
}

void step_trace_parse_error(const char *reason)
{
    if (!lock()) return;
    voice_trace_event_t e = event(s_active_turn, "rx.parse_failed");
    e.status = "error";
    e.reason = reason;
    emit(&e);
    unlock();
}

void step_trace_received(size_t samples, size_t queued)
{
    if (!lock()) return;
    turn_t *turn = find_turn(s_audio_turn);
    if (turn) {
        turn->received_samples += samples;
        turn->queued_samples += queued;
        turn->dropped_samples += samples - queued;
    }
    unlock();
}

void step_trace_played(size_t samples, unsigned volume, bool ok)
{
    if (!lock()) return;
    turn_t *turn = find_turn(s_audio_turn);
    if (turn) {
        turn->volume = volume;
        if (ok) turn->played_samples += samples;
    }
    if (!ok) {
        voice_trace_event_t e = event(s_audio_turn, "playback.failed");
        e.status = "error";
        metrics(&e);
        emit(&e);
    }
    unlock();
}
void step_trace_output_volume(unsigned volume)
{
    if (!lock()) return;
    s_output_volume = volume;
    turn_t *turn = find_turn(s_active_turn);
    if (turn) turn->volume = volume;
    unlock();
}

void step_trace_playback_done(void)
{
    if (!lock()) return;
    voice_trace_event_t e = event(s_audio_turn, "playback.done");
    e.response = s_audio_response;
    e.status = "drained";
    metrics(&e);
    emit(&e);
    unlock();
}

void step_trace_tool(uint32_t turn, const char *stage, const char *call_id,
                     const char *name, const char *arguments, const char *result,
                     const char *status, const char *reason)
{
    if (!lock()) return;
    voice_trace_event_t e = event(turn, stage);
    e.call = call_id;
    e.name = name;
    e.args = arguments;
    e.result = result;
    e.status = status;
    e.reason = reason;
    emit(&e);
    unlock();
}

void step_trace_disconnect(void)
{
    if (!lock()) return;
    voice_trace_event_t e = event(s_active_turn, "session.disconnected");
    e.status = "interrupted";
    metrics(&e);
    emit(&e);
    memset(s_responses, 0, sizeof(s_responses));
    memset(s_calls, 0, sizeof(s_calls));
    memset(s_requests, 0, sizeof(s_requests));
    memset(s_turns, 0, sizeof(s_turns));
    s_input_count = s_response_count = 0;
    s_input_overflow = s_response_overflow = false;
    s_active_turn = s_audio_turn = 0;
    s_session[0] = s_audio_response[0] = '\0';
    unlock();
}
