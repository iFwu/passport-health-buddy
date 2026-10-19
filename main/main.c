#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "plank_core.h"
#include "plank_audio.h"
#include "plank_storage.h"
#include "plank_ui.h"
#include "health_cfg.h"
#include "health_ui.h"
#include "health_wifi.h"
#include "health_portal.h"
#include "health_input.h"
#include "step_voice.h"
#include "step_trace.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "plank";
static health_log_t s_log;
static char s_ssid[HEALTH_SSID_MAX + 1];
static char s_pass[HEALTH_PASS_MAX + 1];
static char s_key[HEALTH_KEY_MAX + 1];
static uint32_t s_saved_revision;
static uint64_t s_save_retry_at;
static atomic_uint s_volume = 80;
static atomic_bool s_voice_wanted;
static health_input_t s_input;

static bool stepfun_resolved(void)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    for (int attempt = 0; attempt < 8; attempt++) {
        struct addrinfo *res = NULL;
        if (getaddrinfo("api.stepfun.com", "443", &hints, &res) == 0 && res) {
            freeaddrinfo(res);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    return false;
}

static void net_task(void *arg)
{
    (void)arg;
    if (health_wifi_connect(s_ssid, s_pass, 20000) == ESP_OK) {
        health_clock_start();
        if (!stepfun_resolved()) ESP_LOGW(TAG, "StepFun 域名暂时解析失败，仍会重试连接");
        else ESP_LOGI(TAG, "StepFun 域名已解析");
        if (step_voice_start(s_key) != ESP_OK) ESP_LOGE(TAG, "语音启动失败");
    } else {
        ESP_LOGW(TAG, "Wi-Fi 连接失败，计时和手动记录仍可用");
    }
    memset(s_key, 0, sizeof(s_key));
    vTaskDelete(NULL);
}

#ifdef CONFIG_HEALTH_UART_PROVISION
static void uart_task(void *arg)
{
    (void)arg;
    char line[200];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "ssid ", 5) == 0) snprintf(s_ssid, sizeof(s_ssid), "%s", line + 5);
        else if (strncmp(line, "pass ", 5) == 0) snprintf(s_pass, sizeof(s_pass), "%s", line + 5);
        else if (strncmp(line, "key ", 4) == 0) snprintf(s_key, sizeof(s_key), "%s", line + 4);
        else if (strcmp(line, "go") == 0 && s_ssid[0] && s_key[0]) {
            if (health_cfg_save_net(s_ssid, s_pass, s_key) == ESP_OK) esp_restart();
        }
    }
    vTaskDelete(NULL);
}
#endif

typedef struct { bsp_btn_t button; bsp_btn_ev_t event; uint64_t at_ms; } key_event_t;
typedef struct { plank_history_t history; esp_err_t error; } storage_result_t;
typedef struct { unsigned effect, generation; uint64_t at_ms; } audio_event_t;

static QueueHandle_t s_keys, s_save_requests, s_storage_results, s_audio, s_audio_status;
static plank_model_t s_model;
static bool s_buttons_ok, s_audio_ok, s_battery_ok;
static atomic_uint s_audio_generation;

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000;
}

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    const key_event_t key = { .button = button, .event = event, .at_ms = now_ms() };
    xQueueSend(s_keys, &key, 0);
}

static void storage_worker(void *arg)
{
    (void)arg;
    storage_result_t result;
    result.error = plank_storage_load(&result.history);
    xQueueSend(s_storage_results, &result, portMAX_DELAY);
    for (;;) {
        xQueueReceive(s_save_requests, &result.history, portMAX_DELAY);
        result.error = plank_storage_save(&result.history);
        if (result.error != ESP_OK) ESP_LOGE(TAG, "Save: %s", esp_err_to_name(result.error));
        xQueueSend(s_storage_results, &result, portMAX_DELAY);
    }
}

static void audio_worker(void *arg)
{
    (void)arg;
    bool ok = bsp_audio_init() == ESP_OK;
    if (ok) ok = bsp_audio_set_format(PLANK_AUDIO_RATE, 16, 1) == ESP_OK;
    unsigned volume = atomic_load(&s_volume);
    if (ok) bsp_audio_set_volume(volume);
    xQueueOverwrite(s_audio_status, &ok);
    audio_event_t event = {0};
    size_t offset = 0;
    bool listening = false, recording = false;
    int16_t pcm[PLANK_AUDIO_CHUNK];
    for (;;) {
        if (volume != atomic_load(&s_volume)) {
            volume = atomic_load(&s_volume);
            if (ok) bsp_audio_set_volume(volume);
            step_trace_output_volume(volume);
        }
        const bool wanted = atomic_load(&s_voice_wanted);
        if (!wanted && recording) {
            step_voice_ptt(false);
            recording = false;
        }
        if (!wanted && listening) { listening = false; event.effect = 0; }
        if (wanted && !recording && !listening) {
            if (ok && step_voice_state() == STEP_READY) {
                listening = true;
                event.effect = HEALTH_SOUND_LISTEN;
                event.generation = atomic_fetch_add(&s_audio_generation, 1) + 1;
                event.at_ms = now_ms();
                offset = 0;
            } else {
                atomic_store(&s_voice_wanted, false);
            }
        }
        if (!ok) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        size_t count = 0;
        bool voice_chunk = false;
        if (listening) {
            count = plank_audio_render(event.effect, offset, pcm, PLANK_AUDIO_CHUNK);
            if (!count) {
                // Queue silence beyond the 90 ms I2S DMA span before opening the mic.
                memset(pcm, 0, sizeof(pcm));
                for (unsigned i = 0; i < 4; i++) {
                    if (bsp_audio_write(pcm, sizeof(pcm)) != ESP_OK) {
                        ok = false;
                        atomic_store(&s_voice_wanted, false);
                        ESP_LOGE(TAG, "Audio listening signal failed");
                        xQueueOverwrite(s_audio_status, &ok);
                        break;
                    }
                }
                if (atomic_load(&s_voice_wanted)) {
                    step_trace_output_volume(volume);
                    step_voice_ptt(true);
                    recording = step_voice_state() == STEP_RECORDING;
                }
                listening = false;
                event.effect = 0;
                continue;
            }
        } else {
            if (step_voice_take_sent()) {
                event = (audio_event_t){ HEALTH_SOUND_SENT, atomic_load(&s_audio_generation), now_ms() };
                offset = 0;
            }
            count = step_voice_read_pcm(pcm, PLANK_AUDIO_CHUNK);
            if (count) {
                voice_chunk = true;
                event.effect = 0;
            } else if (step_voice_owns_audio()) {
                event.effect = 0;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            } else {
                if (!event.effect) {
                    if (xQueueReceive(s_audio, &event, pdMS_TO_TICKS(10)) != pdTRUE) continue;
                    offset = 0;
                    if (now_ms() - event.at_ms > 500) { event.effect = 0; continue; }
                }
                if (event.generation != atomic_load(&s_audio_generation)) {
                    event.effect = 0;
                    continue;
                }
                count = plank_audio_render(event.effect, offset, pcm, PLANK_AUDIO_CHUNK);
                if (!count) { event.effect = 0; continue; }
            }
        }
        const bool written = bsp_audio_write(pcm, count * sizeof(*pcm)) == ESP_OK;
        if (voice_chunk) step_trace_played(count, volume, written);
        if (!written) {
            ok = false;
            step_voice_ptt(false);
            atomic_store(&s_voice_wanted, false);
            ESP_LOGE(TAG, "Audio output failed");
            xQueueOverwrite(s_audio_status, &ok);
            recording = false;
        }
        offset += count;
    }
}

static void effects(unsigned effect)
{
    if (effect & (PLANK_EFFECT_START | PLANK_EFFECT_STOP)) atomic_fetch_add(&s_audio_generation, 1);
    if (effect & PLANK_EFFECT_SAVE) {
        if (xQueueSend(s_save_requests, &s_model.history, 0) != pdTRUE)
            plank_saved(&s_model, false);
    }
    unsigned cue = effect & ~PLANK_EFFECT_SAVE;
    if (cue && s_audio_ok) {
        audio_event_t event = { .effect = cue, .at_ms = now_ms(),
                               .generation = atomic_load(&s_audio_generation) };
        if (xQueueSend(s_audio, &event, 0) != pdTRUE)
            ESP_LOGW(TAG, "Audio cue skipped: queue full");
    }
}

static void notice(const char *text, uint64_t now)
{
    if (bsp_lvgl_lock(100)) {
        health_ui_notice(text, now);
        bsp_lvgl_unlock();
    }
}

static bool persist_health(uint64_t now)
{
    if (s_log.revision == s_saved_revision) return true;
    if (now < s_save_retry_at) return false;
    const esp_err_t err = health_cfg_save_pending(&s_log, &s_saved_revision);
    if (err != ESP_OK) {
        s_save_retry_at = now + 1000;
        ESP_LOGE(TAG, "Health record save failed: %s", esp_err_to_name(err));
        return false;
    }
    s_saved_revision = s_log.revision;
    s_save_retry_at = 0;
    return true;
}

static health_voice_state_t ui_voice_state(void)
{
    switch (step_voice_state()) {
    case STEP_CONNECTING: return HEALTH_VOICE_CONNECTING;
    case STEP_READY: return HEALTH_VOICE_READY;
    case STEP_RECORDING: return HEALTH_VOICE_RECORDING;
    case STEP_THINKING: return HEALTH_VOICE_THINKING;
    case STEP_SPEAKING: return HEALTH_VOICE_SPEAKING;
    case STEP_ERROR: return HEALTH_VOICE_ERROR;
    default: return HEALTH_VOICE_OFF;
    }
}

void app_main(void)
{
    if (health_cfg_init() != ESP_OK) ESP_LOGW(TAG, "NVS 初始化失败，配置和记录只保存在本次开机");
    atomic_store(&s_volume, health_cfg_load_volume());
    if (bsp_i2c_init() != ESP_OK || bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    s_keys = xQueueCreate(12, sizeof(key_event_t));
    s_save_requests = xQueueCreate(1, sizeof(plank_history_t));
    s_storage_results = xQueueCreate(1, sizeof(storage_result_t));
    s_audio = xQueueCreate(8, sizeof(audio_event_t));
    s_audio_status = xQueueCreate(1, sizeof(bool));
    if (!s_keys || !s_save_requests || !s_storage_results || !s_audio || !s_audio_status) {
        ESP_LOGE(TAG, "Queue allocation failed");
        return;
    }
    storage_result_t loaded = { .error = ESP_ERR_NO_MEM };
    if (xTaskCreate(storage_worker, "plank_store", 4096, NULL, 2, NULL) == pdPASS)
        xQueueReceive(s_storage_results, &loaded, portMAX_DELAY);
    plank_init(&s_model, &loaded.history, loaded.error == ESP_OK);
    if (loaded.error != ESP_OK) ESP_LOGE(TAG, "History load: %s", esp_err_to_name(loaded.error));
    if (xTaskCreate(audio_worker, "health_audio", 4096, NULL, 8, NULL) == pdPASS)
        xQueueReceive(s_audio_status, &s_audio_ok, portMAX_DELAY);
    s_battery_ok = bsp_battery_init() == ESP_OK;
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock failed");
        return;
    }
    plank_ui_create();
    bsp_lvgl_unlock();
    s_buttons_ok = bsp_button_init(on_key, NULL) == ESP_OK;
    bsp_display_backlight(80);
    ESP_LOGI(TAG, "Ready: buttons=%d audio=%d storage=%d heap=%lu",
             s_buttons_ok, s_audio_ok, s_model.storage_available,
             (unsigned long)esp_get_free_heap_size());
    health_persist_t stored;
    const bool loaded_log = health_cfg_load_log(&stored);
    health_log_init(&s_log, loaded_log ? &stored : NULL, now_ms());
    s_saved_revision = s_log.revision;
    if (bsp_lvgl_lock(1000)) {
        health_ui_create(lv_scr_act());
        health_ui_set_volume(atomic_load(&s_volume), now_ms());
        bsp_lvgl_unlock();
    }
    const bool wifi_ok = health_wifi_init() == ESP_OK;
    bool have_net = false;
    if (wifi_ok && CONFIG_HEALTH_WIFI_SSID[0] && CONFIG_HEALTH_STEPFUN_API_KEY[0]) {
        snprintf(s_ssid, sizeof(s_ssid), "%s", CONFIG_HEALTH_WIFI_SSID);
        snprintf(s_pass, sizeof(s_pass), "%s", CONFIG_HEALTH_WIFI_PASSWORD);
        snprintf(s_key, sizeof(s_key), "%s", CONFIG_HEALTH_STEPFUN_API_KEY);
        have_net = true;
        ESP_LOGI(TAG, "使用 sdkconfig.local，名称长度 %u", (unsigned)strlen(s_ssid));
    } else if (wifi_ok) {
        have_net = health_cfg_load_net(s_ssid, sizeof(s_ssid), s_pass, sizeof(s_pass),
                                        s_key, sizeof(s_key));
    }
    if (!wifi_ok) {
        ESP_LOGE(TAG, "Wi-Fi 初始化失败");
    } else if (!have_net) {
#ifdef CONFIG_HEALTH_UART_PROVISION
        if (bsp_lvgl_lock(200)) {
            health_ui_set_portal("串口", "见日志", "等待 ssid/pass/key/go");
            bsp_lvgl_unlock();
        }
        xTaskCreate(uart_task, "uart_cfg", 4096, NULL, 3, NULL);
#else
        char ap_pass[9];
        snprintf(ap_pass, sizeof(ap_pass), "%08u", (unsigned)(esp_random() % 100000000));
        if (health_portal_start("HealthBuddy", ap_pass) == ESP_OK && bsp_lvgl_lock(200)) {
            health_ui_set_portal("HealthBuddy", ap_pass, health_portal_status());
            bsp_lvgl_unlock();
        }
#endif
    } else {
        xTaskCreate(net_task, "health_net", 8192, NULL, 4, NULL);
    }
    int battery = -1;
    uint64_t battery_at = 0;
    unsigned saved_volume = atomic_load(&s_volume);
    uint64_t volume_save_at = 0;
    unsigned previous_rest = 0;
    for (;;) {
        key_event_t key;
        while (s_buttons_ok && xQueueReceive(s_keys, &key, 0) == pdTRUE) {
            health_key_event_t event;
            if (key.event == BSP_BTN_PRESS) event = HEALTH_KEY_PRESS;
            else if (key.event == BSP_BTN_LONG) event = HEALTH_KEY_LONG;
            else if (key.event == BSP_BTN_RELEASE) event = HEALTH_KEY_RELEASE;
            else continue;
            const health_key_t button = key.button == BSP_BTN_UP ? HEALTH_KEY_UP :
                                        key.button == BSP_BTN_DOWN ? HEALTH_KEY_DOWN : HEALTH_KEY_OK;
            const health_action_t action = health_input_event(&s_input, button, event);
            if (action == HEALTH_ACTION_NONE) continue;
            if (action == HEALTH_ACTION_VOICE_STOP) {
                atomic_store(&s_voice_wanted, false);
                continue;
            }
            if (action == HEALTH_ACTION_VOICE_START) {
                if (s_audio_ok && step_voice_state() == STEP_READY) {
                    atomic_store(&s_voice_wanted, true);
                    notice("提示音后说话", key.at_ms);
                } else {
                    notice(step_voice_state() == STEP_THINKING || step_voice_state() == STEP_SPEAKING ?
                           "请等回复结束" : "语音暂不可用", key.at_ms);
                    effects(HEALTH_SOUND_ERROR);
                }
                continue;
            }
            if (action == HEALTH_ACTION_VOLUME_UP || action == HEALTH_ACTION_VOLUME_DOWN) {
                unsigned volume = atomic_load(&s_volume);
                volume = action == HEALTH_ACTION_VOLUME_UP ? (volume < 90 ? volume + 10 : 100) :
                         (volume > 10 ? volume - 10 : 0);
                atomic_store(&s_volume, volume);
                volume_save_at = key.at_ms + 1000;
                if (bsp_lvgl_lock(100)) {
                    health_ui_set_volume(volume, key.at_ms);
                    bsp_lvgl_unlock();
                }
                effects(HEALTH_SOUND_VOLUME);
                continue;
            }
            unsigned effect = 0;
            const plank_state_t previous = s_model.state;
            if (bsp_lvgl_lock(100)) {
                effect = health_ui_action(action, &s_log, &s_model, key.at_ms);
                bsp_lvgl_unlock();
            }
            if (previous == PLANK_PREPARING && s_model.state == PLANK_READY)
                atomic_fetch_add(&s_audio_generation, 1);
            if ((effect & HEALTH_SOUND_OK) && !persist_health(key.at_ms)) {
                effect = HEALTH_SOUND_ERROR;
                notice("未保存 暂存本次", key.at_ms);
            }
            effects(effect);
        }
        uint64_t now = now_ms();
        effects(plank_tick(&s_model, now));
        health_log_set_day(&s_log, health_clock_day());
        const bool had_interval = s_log.interval_on;
        const bool was_work = s_log.interval_work;
        const bool had_timer = s_log.timer_running;
        const unsigned rest = health_log_rest_left_s(&s_log, now);
        if (had_timer && rest > 0 && rest <= 3 && rest != previous_rest) effects(PLANK_EFFECT_PREP_1);
        previous_rest = rest;
        if (health_log_tick(&s_log, now)) {
            if (had_interval && !s_log.interval_on) effects(HEALTH_SOUND_FINISH);
            else if (had_interval && was_work != s_log.interval_work)
                effects(s_log.interval_work ? PLANK_EFFECT_START : HEALTH_SOUND_RELAX);
            else if (had_interval) effects(PLANK_EFFECT_START);
            else if (had_timer) effects(PLANK_EFFECT_START);
        }
        const unsigned reminders = health_log_take_reminders(&s_log, now);
        if (reminders && health_ui_mode() == HEALTH_UI_EVERYDAY) {
            notice(reminders & HEALTH_REMINDER_ACTIVITY ? "活动一下吧" : "喝点水吧", now);
            effects(HEALTH_SOUND_REMIND);
        }
        step_tool_t tool;
        while (step_voice_take_tool(&tool)) {
            char out[320];
            const char *storage = "not_required";
            step_trace_tool(tool.trace_turn, "tool.execute", tool.call_id, tool.name,
                            tool.arguments, NULL, "started", NULL);
            const uint32_t revision = s_log.revision;
            if (!health_activity_tool(&s_log, &s_model, tool.name, tool.arguments, now, out, sizeof(out)))
                snprintf(out, sizeof(out), "{\"ok\":false,\"reason\":\"unknown_tool\"}");
            if (s_log.revision != revision) {
                if (persist_health(now)) {
                    storage = "committed";
                    notice("记录已保存", now);
                    effects(HEALTH_SOUND_OK);
                } else {
                    storage = "save_failed";
                    snprintf(out, sizeof(out), "{\"ok\":false,\"reason\":\"storage_error\",\"kept_in_ram\":true}");
                    notice("未保存 暂存本次", now);
                    effects(HEALTH_SOUND_ERROR);
                }
            } else if (strstr(out, "\"ok\":false")) effects(HEALTH_SOUND_ERROR);
            step_trace_tool(tool.trace_turn, "tool.result", tool.call_id, tool.name,
                            NULL, out, strstr(out, "\"ok\":true") ? "ok" : "error", storage);
            step_voice_tool_result(tool.call_id, out);
        }
        if (s_log.mode_request != 0) {
            health_ui_mode_t target = (s_log.mode_request == 1) ? HEALTH_UI_EVERYDAY :
                                      (s_log.mode_request == 2) ? HEALTH_UI_GYM : HEALTH_UI_HOME;
            s_log.mode_request = 0;
            if (bsp_lvgl_lock(100)) {
                health_ui_set_mode(target);
                bsp_lvgl_unlock();
            }
        }
        storage_result_t saved;
        if (xQueueReceive(s_storage_results, &saved, 0) == pdTRUE)
            plank_saved(&s_model, saved.error == ESP_OK);
        xQueueReceive(s_audio_status, &s_audio_ok, 0);
        if (now >= battery_at) {
            battery = s_battery_ok ? bsp_battery_soc() : -1;
            battery_at = now + 10000;
        }
        if (s_log.revision != s_saved_revision && now >= s_save_retry_at && persist_health(now))
            notice("记录已保存", now);
        if (volume_save_at && now >= volume_save_at && saved_volume != atomic_load(&s_volume)) {
            unsigned volume = atomic_load(&s_volume);
            if (health_cfg_save_volume(volume) == ESP_OK) {
                saved_volume = volume;
                volume_save_at = 0;
            } else {
                volume_save_at = now + 1000;
                notice("音量设置未保存", now);
            }
        }
        if (bsp_lvgl_lock(50)) {
            health_ui_refresh(&s_log, &s_model, now, battery, health_portal_status(),
                              s_audio_ok, s_buttons_ok, ui_voice_state());
            bsp_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
