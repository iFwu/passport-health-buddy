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
    "\"instructions\":\"你是随身健康助手，回答简短。喝水、训练或休息必须调用工具，不要把工具名念出来。一杯水记 250 毫升。记一组要给出动作、重量和次数。\","
    "\"voice\":\"qingchunshaonv\","
    "\"input_audio_format\":\"pcm16\",\"output_audio_format\":\"pcm16\","
    "\"tools\":["
    "{\"type\":\"function\",\"function\":{\"name\":\"log_water\",\"description\":\"记录喝水量\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"ml\":{\"type\":\"integer\"}},\"required\":[\"ml\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"log_set\",\"description\":\"记录一组力量训练\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{"
    "\"exercise\":{\"type\":\"string\"},\"weight_kg\":{\"type\":\"number\"},\"reps\":{\"type\":\"integer\"}},"
    "\"required\":[\"exercise\",\"reps\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"start_rest_timer\",\"description\":\"开始组间休息\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"seconds\":{\"type\":\"integer\"}}}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"start_interval_plan\",\"description\":\"开始间歇训练\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{"
    "\"work_s\":{\"type\":\"integer\"},\"rest_s\":{\"type\":\"integer\"},\"rounds\":{\"type\":\"integer\"}},"
    "\"required\":[\"work_s\",\"rounds\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"snooze_reminder\",\"description\":\"推迟久坐提醒\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"minutes\":{\"type\":\"integer\"}}}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"query_today\",\"description\":\"查询今天的喝水和训练\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"metric\":{\"type\":\"string\"}}}}}"
    "]}}";

static esp_websocket_client_handle_t s_ws;
static step_rx_t *s_rx;
static StreamBufferHandle_t s_play;
static QueueHandle_t s_tools;
static dsp_rs_t s_rs;
static char s_auth[HEALTH_KEY_MAX + 32];
static char s_up[UP_CAP];
static char s_result[256];
static char s_result_id[48];
static char s_text[STEP_TEXT_MAX];
static atomic_bool s_ptt;
static atomic_bool s_owns;
static atomic_int s_state = STEP_OFF;
static atomic_bool s_hello;
static atomic_bool s_result_ready;
static atomic_bool s_followup;
static atomic_bool s_tool_pending;
static char s_b64[3500];

static void on_pcm(void *user, const int16_t *pcm, size_t samples)
{
    (void)user;
    int16_t out[48];
    const size_t produced = dsp_rs_24to16(&s_rs, pcm, samples, out);
    if (produced && s_play) xStreamBufferSend(s_play, out, produced * sizeof(out[0]), 0);
    atomic_store(&s_owns, true);
    atomic_store(&s_state, STEP_SPEAKING);
}

static bool send_text(const char *json)
{
    if (!s_ws || !esp_websocket_client_is_connected(s_ws)) return false;
    return esp_websocket_client_send_text(s_ws, json, (int)strlen(json), pdMS_TO_TICKS(2000)) > 0;
}

static void handle_msg(const step_msg_t *msg)
{
    if (msg->transcript[0]) snprintf(s_text, sizeof(s_text), "%s", msg->transcript);
    if (strcmp(msg->type, "session.updated") == 0) {
        atomic_store(&s_state, STEP_READY);
        ESP_LOGI(TAG, "StepFun 会话已就绪");
    } else if (strcmp(msg->type, "response.function_call_arguments.done") == 0 && msg->name[0]) {
        step_tool_t tool = {0};
        snprintf(tool.name, sizeof(tool.name), "%s", msg->name);
        snprintf(tool.call_id, sizeof(tool.call_id), "%s", msg->call_id);
        snprintf(tool.arguments, sizeof(tool.arguments), "%s", msg->arguments);
        xQueueSend(s_tools, &tool, 0);
        atomic_store(&s_tool_pending, true);
        ESP_LOGI(TAG, "工具 %s", tool.name);
    } else if (strcmp(msg->type, "response.done") == 0) {
        if (atomic_exchange(&s_tool_pending, false)) atomic_store(&s_followup, true);
    } else if (strcmp(msg->type, "error") == 0) {
        atomic_store(&s_state, STEP_ERROR);
        ESP_LOGW(TAG, "StepFun 返回错误");
    }
}

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    const esp_websocket_event_data_t *ev = data;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        dsp_rs_init(&s_rs);
        atomic_store(&s_hello, true);
        atomic_store(&s_state, STEP_CONNECTING);
        ESP_LOGI(TAG, "WebSocket 已连接");
        return;
    }
    if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_CLOSED) {
        if (atomic_load(&s_state) != STEP_ERROR) atomic_store(&s_state, STEP_CONNECTING);
        return;
    }
    if (id != WEBSOCKET_EVENT_DATA || ev->op_code != 0x1 || ev->data_len <= 0) return;
    if (ev->payload_offset == 0) step_rx_reset(s_rx);
    if (!step_rx_feed(s_rx, ev->data_ptr, (size_t)ev->data_len)) {
        step_rx_reset(s_rx);
        return;
    }
    if (ev->payload_offset + ev->data_len >= ev->payload_len) {
        step_msg_t msg;
        if (step_rx_take(s_rx, &msg)) handle_msg(&msg);
    }
}

static void send_followup(void)
{
    if (!atomic_load(&s_followup) || !atomic_load(&s_result_ready)) return;
    char escaped[320];
    char frame[512];
    form_json_escape(escaped, sizeof(escaped), s_result, strlen(s_result));
    snprintf(frame, sizeof(frame),
             "{\"type\":\"conversation.item.create\",\"item\":{\"type\":\"function_call_output\","
             "\"call_id\":\"%s\",\"output\":\"%s\"}}",
             s_result_id, escaped);
    if (!send_text(frame) || !send_text("{\"type\":\"response.create\"}")) {
        atomic_store(&s_state, STEP_ERROR);
        return;
    }
    atomic_store(&s_result_ready, false);
    atomic_store(&s_followup, false);
    atomic_store(&s_state, STEP_THINKING);
}

static void uplink(void *arg)
{
    (void)arg;
    uint8_t pcm[FRAME_BYTES];
    size_t filled = 0;
    bool was_held = false;
    for (;;) {
        if (atomic_load(&s_hello) && send_text(SESSION)) atomic_store(&s_hello, false);
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
                    send_text(s_up);
                }
            }
            if (was_held) {
                send_text("{\"type\":\"input_audio_buffer.commit\"}");
                send_text("{\"type\":\"response.create\"}");
                atomic_store(&s_state, STEP_THINKING);
                atomic_store(&s_owns, false);
                ESP_LOGI(TAG, "松手，已提交语音");
            }
            was_held = false;
            filled = 0;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!was_held) {
            xStreamBufferReset(s_play);
            dsp_rs_init(&s_rs);
            atomic_store(&s_state, STEP_RECORDING);
            atomic_store(&s_owns, true);
            was_held = true;
            ESP_LOGI(TAG, "按住，开始听");
        }
        if (bsp_audio_read(pcm + filled, FRAME_BYTES - filled) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        filled = FRAME_BYTES;
        const size_t n = b64_encode(pcm, filled, s_b64, sizeof(s_b64) - 1);
        s_b64[n] = '\0';
        if (n) {
            snprintf(s_up, sizeof(s_up),
                     "{\"type\":\"input_audio_buffer.append\",\"audio\":\"%s\"}", s_b64);
            send_text(s_up);
        }
        filled = 0;
    }
}

static void playback(void *arg)
{
    (void)arg;
    uint8_t pcm[640];
    for (;;) {
        if (atomic_load(&s_ptt)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        const size_t n = xStreamBufferReceive(s_play, pcm, sizeof(pcm), pdMS_TO_TICKS(200));
        if (!n) {
            if (atomic_load(&s_state) == STEP_SPEAKING && xStreamBufferBytesAvailable(s_play) == 0) {
                atomic_store(&s_owns, false);
                atomic_store(&s_state, STEP_READY);
            }
            continue;
        }
        bsp_audio_write(pcm, n);
    }
}

esp_err_t step_voice_start(const char *api_key)
{
    if (!api_key || !api_key[0]) return ESP_ERR_INVALID_ARG;
    const char *model = CONFIG_HEALTH_STEPFUN_MODEL[0] ? CONFIG_HEALTH_STEPFUN_MODEL
                                                       : "stepaudio-3-realtime-preview";
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
    if (xTaskCreate(playback, "step_play", 4096, NULL, 8, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreate(uplink, "step_up", 8192, NULL, 7, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return esp_websocket_client_start(s_ws);
}

void step_voice_ptt(bool held)
{
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

void step_voice_text(char *out, size_t cap)
{
    snprintf(out, cap, "%s", s_text);
}

bool step_voice_take_tool(step_tool_t *tool)
{
    return s_tools && xQueueReceive(s_tools, tool, 0) == pdTRUE;
}

void step_voice_tool_result(const char *call_id, const char *json)
{
    snprintf(s_result_id, sizeof(s_result_id), "%s", call_id);
    snprintf(s_result, sizeof(s_result), "%s", json);
    atomic_store(&s_result_ready, true);
}
