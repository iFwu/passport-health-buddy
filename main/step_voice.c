#include "step_voice.h"

#include "audio_dsp.h"
#include "b64_stream.h"
#include "bsp_audio.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "health_cfg.h"
#include "portal_form.h"
#include "step_rx.h"
#include "step_trace.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "step_voice";

#define RATE 16000
#define FRAME_BYTES (RATE * 80 / 1000 * 2)
#define UP_CAP 4096
#define PLAY_CAP (12 * 1024)

static char s_uri[160];

static const char SESSION[] =
    "{\"type\":\"session.update\",\"session\":{"
    "\"modalities\":[\"text\",\"audio\"],"
    "\"instructions\":\"你是随身健康教练，用中文回答，每次不超过20个字，不念工具名、英文字段或JSON。"
    "1 训练用 log_workout。exercise 优先从枚举选；不在枚举时填 other，并给 category 和英文 name_en。"
    "2 自重动作没说重量 weight_kg=0。磅换公斤乘0.45。杠铃说每边X公斤，重量=2X+20。没说组数 sets=1。说了休息时长才填 rest_s。"
    "3 用户纠正上一条，用 log_workout 并设 replace_last=true；要删除用 undo_last。"
    "4 喝水：一口50，一杯或半瓶250，一瓶500，大杯400毫升。"
    "5 一分半=90秒。计时、倒计时用 start_timer。"
    "6 去健身房、办公、居家训练用 set_mode。"
    "7 每句话只调用一个工具。播报只用工具返回的数据；prev 比这次轻就简短夸一句。\","
    "\"voice\":\"qingchunshaonv\","
    "\"input_audio_format\":\"pcm16\",\"output_audio_format\":\"pcm16\","
    "\"tools\":["
    "{\"type\":\"function\",\"function\":{\"name\":\"log_workout\",\"description\":\"记录力量训练动作、重量、次数和组数\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{"
    "\"exercise\":{\"type\":\"string\",\"enum\":[\"bench_press\",\"incline_press\",\"squat\",\"deadlift\",\"romanian_deadlift\",\"overhead_press\",\"barbell_row\",\"dumbbell_row\",\"curl\",\"lateral_raise\",\"hip_thrust\",\"lunge\",\"lat_pulldown\",\"seated_row\",\"leg_press\",\"leg_extension\",\"leg_curl\",\"chest_fly\",\"triceps_pushdown\",\"pull_up\",\"push_up\",\"dip\",\"crunch\",\"other\"]},"
    "\"category\":{\"type\":\"string\",\"enum\":[\"free_weight\",\"machine\",\"cable\",\"bodyweight\"]},"
    "\"name_en\":{\"type\":\"string\"},"
    "\"weight_kg\":{\"type\":\"number\"},"
    "\"reps\":{\"type\":\"integer\"},"
    "\"sets\":{\"type\":\"integer\"},"
    "\"rest_s\":{\"type\":\"integer\"},"
    "\"replace_last\":{\"type\":\"boolean\"}},"
    "\"required\":[\"exercise\",\"reps\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"log_water\",\"description\":\"记录喝水量\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"ml\":{\"type\":\"integer\"}},\"required\":[\"ml\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"start_timer\",\"description\":\"开始休息倒计时或计时器\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"seconds\":{\"type\":\"integer\"}},\"required\":[\"seconds\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"start_interval_plan\",\"description\":\"开始间歇训练跟练\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{"
    "\"work_s\":{\"type\":\"integer\"},\"rest_s\":{\"type\":\"integer\"},\"rounds\":{\"type\":\"integer\"}},"
    "\"required\":[\"work_s\",\"rounds\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"set_mode\",\"description\":\"切换设备模式\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"mode\":{\"type\":\"string\",\"enum\":[\"office\",\"gym\",\"home\"]}},\"required\":[\"mode\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"undo_last\",\"description\":\"撤销刚才记录的喝水或动作\",\"parameters\":{\"type\":\"object\",\"properties\":{}}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"snooze_reminder\",\"description\":\"推迟久坐提醒\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"minutes\":{\"type\":\"integer\"}}}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"query_today\",\"description\":\"查询今天的喝水、训练或总体数据\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"metric\":{\"type\":\"string\"}}}}}"
    "]}}";

typedef struct {
    char call_id[48];
    char result[320];
    uint32_t trace_turn;
} step_result_t;

#define MAX_PENDING_RESULTS 4
static step_result_t s_results[MAX_PENDING_RESULTS];
static int s_result_count;
static portMUX_TYPE s_result_mux = portMUX_INITIALIZER_UNLOCKED;

static esp_websocket_client_handle_t s_ws;
static step_rx_t *s_rx;
static StreamBufferHandle_t s_play;
static QueueHandle_t s_tools;
static dsp_rs_t s_rs;
static char s_auth[HEALTH_KEY_MAX + 32];
static char s_up[UP_CAP];
static atomic_bool s_ptt;
static atomic_bool s_owns;
static atomic_int s_state = STEP_OFF;
static atomic_bool s_hello;
static atomic_bool s_followup;
static atomic_bool s_tool_pending;
static atomic_bool s_response_done;
static atomic_bool s_sent;
static atomic_uint s_trace_turn;
static char s_b64[3500];

static void on_pcm(void *user, const int16_t *pcm, size_t samples)
{
    (void)user;
    int16_t out[48];
    const size_t produced = dsp_rs_24to16(&s_rs, pcm, samples, out);
    const size_t queued = produced && s_play ?
        xStreamBufferSend(s_play, out, produced * sizeof(out[0]), 0) / sizeof(out[0]) : 0;
    step_trace_received(produced, queued);
    atomic_store(&s_owns, true);
    atomic_store(&s_state, STEP_SPEAKING);
}

static bool send_text(const char *json)
{
    if (!s_ws || !esp_websocket_client_is_connected(s_ws)) return false;
    return esp_websocket_client_send_text(s_ws, json, (int)strlen(json), pdMS_TO_TICKS(2000)) > 0;
}

static bool send_control(const char *type, const char *stage, uint32_t turn)
{
    char id[64], frame[192];
    step_trace_event_id(id, sizeof(id));
    snprintf(frame, sizeof(frame), "{\"event_id\":\"%s\",\"type\":\"%s\"}", id, type);
    if (strcmp(type, "input_audio_buffer.commit") == 0) step_trace_expect_input(turn);
    if (strcmp(type, "response.create") == 0) step_trace_expect_response(turn);
    const bool sent = send_text(frame);
    step_trace_sent(turn, stage, id, NULL, sent);
    return sent;
}

static void handle_msg(const step_msg_t *msg)
{
    const uint32_t trace_turn = step_trace_server(msg);
    if (strcmp(msg->type, "session.updated") == 0) {
        atomic_store(&s_state, STEP_READY);
        ESP_LOGI(TAG, "StepFun 会话已就绪");
    } else if (strcmp(msg->type, "response.function_call_arguments.done") == 0 && msg->name[0]) {
        step_tool_t tool = {0};
        snprintf(tool.name, sizeof(tool.name), "%s", msg->name);
        snprintf(tool.call_id, sizeof(tool.call_id), "%s", msg->call_id);
        snprintf(tool.arguments, sizeof(tool.arguments), "%s", msg->arguments);
        tool.trace_turn = trace_turn;
        const bool queued = xQueueSend(s_tools, &tool, 0) == pdTRUE;
        step_trace_tool(trace_turn, "tool.queue", tool.call_id, tool.name, NULL, NULL,
                        queued ? "queued" : "queue_full", NULL);
        atomic_store(&s_tool_pending, true);
        ESP_LOGI(TAG, "工具 %s", tool.name);
    } else if (strcmp(msg->type, "response.done") == 0) {
        atomic_store(&s_response_done, true);
        if (atomic_exchange(&s_tool_pending, false)) atomic_store(&s_followup, true);
    } else if (strcmp(msg->type, "error") == 0) {
        atomic_store(&s_ptt, false);
        atomic_store(&s_owns, false);
        atomic_store(&s_state, STEP_ERROR);
        ESP_LOGW(TAG, "StepFun 返回错误，详情见 voice_trace");
    }
}

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    const esp_websocket_event_data_t *ev = data;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        atomic_store(&s_ptt, false);
        atomic_store(&s_owns, false);
        atomic_store(&s_response_done, false);
        if (s_play) xStreamBufferReset(s_play);
        dsp_rs_init(&s_rs);
        atomic_store(&s_hello, true);
        atomic_store(&s_state, STEP_CONNECTING);
        ESP_LOGI(TAG, "WebSocket 已连接");
        return;
    }
    if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_CLOSED) {
        step_trace_disconnect();
        atomic_store(&s_sent, false);
        atomic_store(&s_ptt, false);
        atomic_store(&s_owns, false);
        ESP_LOGW(TAG, "WebSocket 断开/关闭 (event=%ld)", (long)id);
        if (atomic_load(&s_state) != STEP_ERROR) atomic_store(&s_state, STEP_CONNECTING);
        return;
    }
    if (id == WEBSOCKET_EVENT_ERROR) {
        atomic_store(&s_sent, false);
        atomic_store(&s_ptt, false);
        atomic_store(&s_owns, false);
        ESP_LOGE(TAG, "WebSocket 错误: %d", ev ? ev->error_handle.error_type : -1);
        atomic_store(&s_state, STEP_ERROR);
        return;
    }
    if (id != WEBSOCKET_EVENT_DATA || ev->op_code != 0x1 || ev->data_len <= 0) return;
    if (ev->payload_offset == 0) step_rx_reset(s_rx);
    if (!step_rx_feed(s_rx, ev->data_ptr, (size_t)ev->data_len)) {
        step_trace_parse_error("invalid_or_too_deep_json");
        step_rx_reset(s_rx);
        return;
    }
    if (ev->payload_offset + ev->data_len >= ev->payload_len) {
        step_msg_t msg;
        if (step_rx_take(s_rx, &msg)) handle_msg(&msg);
        else step_trace_parse_error("incomplete_json");
    }
}

static void send_followup(void)
{
    if (!atomic_load(&s_followup)) return;
    portENTER_CRITICAL(&s_result_mux);
    int count = s_result_count;
    portEXIT_CRITICAL(&s_result_mux);
    if (count == 0) return;

    for (int i = 0; i < count; i++) {
        char escaped[400];
        char frame[640], event_id[64];
        step_trace_event_id(event_id, sizeof(event_id));
        form_json_escape(escaped, sizeof(escaped), s_results[i].result, strlen(s_results[i].result));
        snprintf(frame, sizeof(frame),
                 "{\"event_id\":\"%s\",\"type\":\"conversation.item.create\",\"item\":{\"type\":\"function_call_output\","
                 "\"call_id\":\"%s\",\"output\":\"%s\"}}",
                 event_id, s_results[i].call_id, escaped);
        const bool sent = send_text(frame);
        step_trace_sent(s_results[i].trace_turn, "tool.output.sent", event_id, s_results[i].call_id, sent);
        if (!sent) {
            atomic_store(&s_state, STEP_ERROR);
            return;
        }
    }
    if (!send_control("response.create", "response.request", s_results[0].trace_turn)) {
        atomic_store(&s_state, STEP_ERROR);
        return;
    }
    portENTER_CRITICAL(&s_result_mux);
    s_result_count = 0;
    portEXIT_CRITICAL(&s_result_mux);
    atomic_store(&s_followup, false);
    atomic_store(&s_response_done, false);
    atomic_store(&s_state, STEP_THINKING);
}

static void uplink(void *arg)
{
    (void)arg;
    uint8_t pcm[FRAME_BYTES];
    size_t filled = 0;
    bool was_held = false;
    for (;;) {
        if (atomic_load(&s_hello)) {
            char event_id[64];
            step_trace_event_id(event_id, sizeof(event_id));
            snprintf(s_up, sizeof(s_up), "{\"event_id\":\"%s\",%s", event_id, SESSION + 1);
            const bool sent = send_text(s_up);
            step_trace_sent(0, "session.update.sent", event_id, NULL, sent);
            if (sent) {
                ESP_LOGI(TAG, "session.update 发送成功");
                atomic_store(&s_hello, false);
            } else {
                ESP_LOGE(TAG, "session.update 发送失败");
            }
        }
        send_followup();
        const bool held = atomic_load(&s_ptt) && atomic_load(&s_state) != STEP_OFF &&
                          atomic_load(&s_state) != STEP_CONNECTING &&
                          atomic_load(&s_state) != STEP_ERROR;
        if (!held) {
            if (was_held && filled) {
                const size_t n = b64_encode(pcm, filled, s_b64, sizeof(s_b64) - 1);
                s_b64[n] = '\0';
                if (n) {
                    snprintf(s_up, sizeof(s_up),
                             "{\"type\":\"input_audio_buffer.append\",\"audio\":\"%s\"}", s_b64);
                    const bool sent = send_text(s_up);
                    step_trace_upload(filled / sizeof(int16_t), sent);
                }
            }
            if (was_held) {
                if (atomic_load(&s_state) == STEP_RECORDING &&
                    send_control("input_audio_buffer.commit", "input.commit", atomic_load(&s_trace_turn)) &&
                    send_control("response.create", "response.request", atomic_load(&s_trace_turn))) {
                    atomic_store(&s_response_done, false);
                    atomic_store(&s_state, STEP_THINKING);
                    atomic_store(&s_sent, true);
                    ESP_LOGI(TAG, "松手，已提交语音");
                } else if (atomic_load(&s_state) == STEP_RECORDING) {
                    atomic_store(&s_state, STEP_ERROR);
                }
                atomic_store(&s_owns, false);
            }
            was_held = false;
            filled = 0;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!was_held) {
            // Discard queued microphone audio from before the listening signal ended.
            bool microphone_ok = true;
            for (unsigned i = 0; i < 2; i++) {
                if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
                    microphone_ok = false;
                    break;
                }
            }
            if (!microphone_ok) {
                atomic_store(&s_ptt, false);
                atomic_store(&s_owns, false);
                atomic_store(&s_state, STEP_ERROR);
                step_trace_tool(atomic_load(&s_trace_turn), "mic.failed", NULL, NULL,
                                NULL, NULL, "error", "read_failed");
                continue;
            }
            if (!atomic_load(&s_ptt)) {
                atomic_store(&s_owns, false);
                step_trace_tool(atomic_load(&s_trace_turn), "turn.cancelled", NULL, NULL,
                                NULL, NULL, "cancelled", "released_before_mic_ready");
                if (atomic_load(&s_state) == STEP_RECORDING) atomic_store(&s_state, STEP_READY);
                continue;
            }
            was_held = true;
            step_trace_mic_ready();
            ESP_LOGI(TAG, "按住，开始听");
        }
        if (bsp_audio_read(pcm + filled, FRAME_BYTES - filled) != ESP_OK) {
            step_trace_tool(atomic_load(&s_trace_turn), "mic.failed", NULL, NULL,
                            NULL, NULL, "error", "read_failed");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        filled = FRAME_BYTES;
        const size_t n = b64_encode(pcm, filled, s_b64, sizeof(s_b64) - 1);
        s_b64[n] = '\0';
        if (n) {
            snprintf(s_up, sizeof(s_up),
                     "{\"type\":\"input_audio_buffer.append\",\"audio\":\"%s\"}", s_b64);
            const bool sent = send_text(s_up);
            step_trace_upload(filled / sizeof(int16_t), sent);
            if (!sent) {
                atomic_store(&s_state, STEP_ERROR);
                atomic_store(&s_ptt, false);
            }
        }
        filled = 0;
    }
}

size_t step_voice_read_pcm(int16_t *pcm, size_t capacity)
{
    if (!s_play || atomic_load(&s_ptt)) return 0;
    const step_state_t state = atomic_load(&s_state);
    if (state == STEP_OFF || state == STEP_CONNECTING || state == STEP_ERROR) {
        xStreamBufferReset(s_play);
        return 0;
    }
    const size_t n = xStreamBufferReceive(s_play, pcm, capacity * sizeof(*pcm), 0);
    if (!n && atomic_load(&s_response_done) && !atomic_load(&s_followup) &&
        (atomic_load(&s_state) == STEP_SPEAKING || atomic_load(&s_state) == STEP_THINKING)) {
        step_trace_playback_done();
        atomic_store(&s_owns, false);
        atomic_store(&s_state, STEP_READY);
    }
    return n / sizeof(*pcm);
}

bool step_voice_take_sent(void)
{
    return atomic_exchange(&s_sent, false);
}

esp_err_t step_voice_start(const char *api_key)
{
    if (!api_key || !api_key[0]) return ESP_ERR_INVALID_ARG;
    const char *model = CONFIG_HEALTH_STEPFUN_MODEL[0] ? CONFIG_HEALTH_STEPFUN_MODEL
                                                       : "stepaudio-3-realtime-preview";
    step_trace_init(model, SESSION, api_key);
    snprintf(s_uri, sizeof(s_uri), "wss://api.stepfun.com/v1/realtime?model=%s", model);
    snprintf(s_auth, sizeof(s_auth), "Authorization: Bearer %s\r\n", api_key);
    s_rx = step_rx_create(on_pcm, NULL);
    s_play = xStreamBufferCreate(PLAY_CAP, 1);
    s_tools = xQueueCreate(2, sizeof(step_tool_t));
    if (!s_rx || !s_play || !s_tools) return ESP_ERR_NO_MEM;
    const esp_websocket_client_config_t cfg = {
        .uri = s_uri,
        .headers = s_auth,
        .buffer_size = 8192,
        .task_stack = 10240,
        .network_timeout_ms = 15000,
        .reconnect_timeout_ms = 4000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    s_ws = esp_websocket_client_init(&cfg);
    if (!s_ws) return ESP_ERR_NO_MEM;
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    atomic_store(&s_state, STEP_CONNECTING);
    if (xTaskCreate(uplink, "step_up", 8192, NULL, 7, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return esp_websocket_client_start(s_ws);
}

void step_voice_ptt(bool held)
{
    if (held && atomic_load(&s_state) != STEP_READY) return;
    if (held) {
        atomic_store(&s_trace_turn, step_trace_begin());
        atomic_store(&s_owns, true);
        atomic_store(&s_state, STEP_RECORDING);
    }
    atomic_store(&s_ptt, held);
}

step_state_t step_voice_state(void)
{
    return (step_state_t)atomic_load(&s_state);
}

bool step_voice_owns_audio(void)
{
    return atomic_load(&s_owns);
}


bool step_voice_take_tool(step_tool_t *tool)
{
    return s_tools && xQueueReceive(s_tools, tool, 0) == pdTRUE;
}

void step_voice_tool_result(const char *call_id, const char *json)
{
    const uint32_t trace_turn = step_trace_call_turn(call_id);
    portENTER_CRITICAL(&s_result_mux);
    if (s_result_count < MAX_PENDING_RESULTS) {
        snprintf(s_results[s_result_count].call_id, sizeof(s_results[s_result_count].call_id), "%s", call_id);
        s_results[s_result_count].trace_turn = trace_turn;
        snprintf(s_results[s_result_count].result, sizeof(s_results[s_result_count].result), "%s", json);
        s_result_count++;
    }
    portEXIT_CRITICAL(&s_result_mux);
}
