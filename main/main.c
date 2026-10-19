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
#include "step_voice.h"
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
static uint32_t s_saved_water;
static uint8_t s_saved_sets;
static uint32_t s_saved_day;

static plank_key_t as_plank_key(bsp_btn_t button)
{
    if (button == BSP_BTN_UP) return PLANK_UP;
    if (button == BSP_BTN_DOWN) return PLANK_DOWN;
    return PLANK_OK;
}

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
static bool s_buttons_ok, s_audio_ok, s_battery_ok, s_audio_queue_failed;
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

static bool is_periodic_cue(unsigned effect)
{
    return (effect & (PLANK_EFFECT_CUE_10 | PLANK_EFFECT_CUE_30 |
                      PLANK_EFFECT_CUE_60)) != 0;
}

static void audio_worker(void *arg)
{
    (void)arg;
    bool ok = bsp_audio_init() == ESP_OK;
    if (ok) ok = bsp_audio_set_format(PLANK_AUDIO_RATE, 16, 1) == ESP_OK;
    if (ok) bsp_audio_set_volume(PLANK_AUDIO_VOLUME);
    xQueueOverwrite(s_audio_status, &ok);
    audio_event_t event;
    for (;;) {
        xQueueReceive(s_audio, &event, portMAX_DELAY);
        if (!ok) continue;
        bool preparation = (event.effect & (PLANK_EFFECT_PREP_1 | PLANK_EFFECT_PREP_2 |
                            PLANK_EFFECT_PREP_3 | PLANK_EFFECT_PREP_4)) != 0;
        if ((preparation || is_periodic_cue(event.effect)) && now_ms() - event.at_ms > 500) continue;
        int16_t pcm[PLANK_AUDIO_CHUNK];
        size_t offset = 0, count;
        while (!step_voice_owns_audio() && event.generation == atomic_load(&s_audio_generation) &&
               (count = plank_audio_render(event.effect, offset, pcm, PLANK_AUDIO_CHUNK)) > 0) {
            if (bsp_audio_write(pcm, count * sizeof(pcm[0])) != ESP_OK) { ok = false; break; }
            offset += count;
        }
        if (!ok) ESP_LOGE(TAG, "Audio unavailable");
        else ESP_LOGI(TAG, "Audio cue: effect=%u samples=%u volume=%u",
                      event.effect, (unsigned)offset, PLANK_AUDIO_VOLUME);
        xQueueOverwrite(s_audio_status, &ok);
    }
}

static void effects(unsigned effect)
{
    if (effect & (PLANK_EFFECT_START | PLANK_EFFECT_STOP)) atomic_fetch_add(&s_audio_generation, 1);
    if (effect & PLANK_EFFECT_SAVE) {
        if (xQueueSend(s_save_requests, &s_model.history, 0) != pdTRUE)
            plank_saved(&s_model, false);
    }
    unsigned cue = effect & (PLANK_EFFECT_START | PLANK_EFFECT_STOP |
                             PLANK_EFFECT_PREP_1 | PLANK_EFFECT_PREP_2 |
                             PLANK_EFFECT_PREP_3 | PLANK_EFFECT_PREP_4 |
                             PLANK_EFFECT_CUE_10 | PLANK_EFFECT_CUE_30 |
                             PLANK_EFFECT_CUE_60);
    if (cue && s_model.sound && s_audio_ok) {
        audio_event_t event = { .effect = cue, .at_ms = now_ms(),
                               .generation = atomic_load(&s_audio_generation) };
        if (xQueueSend(s_audio, &event, 0) != pdTRUE) {
            ESP_LOGE(TAG, "Audio queue full; cue not delivered");
            s_audio_queue_failed = true;
            s_audio_ok = false;
        }
    }
}

void app_main(void)
{
    if (health_cfg_init() != ESP_OK) ESP_LOGW(TAG, "NVS 初始化失败，配置和记录只保存在本次开机");
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
    if (xTaskCreate(audio_worker, "plank_audio", 4096, NULL, 2, NULL) == pdPASS)
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
    s_saved_water = s_log.saved.water_ml;
    s_saved_sets = s_log.saved.set_count;
    s_saved_day = s_log.saved.day;
    if (bsp_lvgl_lock(1000)) {
        health_ui_create(lv_scr_act());
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
    bool reminded = false;
    for (;;) {
        key_event_t key;
        while (s_buttons_ok && xQueueReceive(s_keys, &key, 0) == pdTRUE) {
            if (!health_ui_key(key.button, key.event, &s_log, key.at_ms) &&
                health_ui_mode() == HEALTH_UI_HOME) {
                plank_state_t previous = s_model.state;
                effects(plank_key(&s_model, as_plank_key(key.button), key.at_ms));
                if ((previous == PLANK_PREPARING && s_model.state == PLANK_READY) || !s_model.sound)
                    atomic_fetch_add(&s_audio_generation, 1);
            }
        }
        uint64_t now = now_ms();
        if (health_ui_mode() == HEALTH_UI_HOME) effects(plank_tick(&s_model, now));
        health_log_set_day(&s_log, health_clock_day());
        if (health_log_tick(&s_log, now)) effects(PLANK_EFFECT_CUE_10);
        const bool due = health_log_sitting_due(&s_log, now) || health_log_water_due(&s_log, now);
        if (due && !reminded) effects(PLANK_EFFECT_CUE_60);
        reminded = due;
        step_tool_t tool;
        while (step_voice_take_tool(&tool)) {
            char out[160];
            if (health_log_tool(&s_log, tool.name, tool.arguments, now, out, sizeof(out)))
                step_voice_tool_result(tool.call_id, out);
        }
        storage_result_t saved;
        if (xQueueReceive(s_storage_results, &saved, 0) == pdTRUE)
            plank_saved(&s_model, saved.error == ESP_OK);
        xQueueReceive(s_audio_status, &s_audio_ok, 0);
        if (s_audio_queue_failed) s_audio_ok = false;
        if (now >= battery_at) {
            battery = s_battery_ok ? bsp_battery_soc() : -1;
            battery_at = now + 10000;
        }
        if (s_log.saved.water_ml != s_saved_water || s_log.saved.set_count != s_saved_sets ||
            s_log.saved.day != s_saved_day) {
            if (health_cfg_save_log(&s_log.saved) == ESP_OK) {
                s_saved_water = s_log.saved.water_ml;
                s_saved_sets = s_log.saved.set_count;
                s_saved_day = s_log.saved.day;
            }
        }
        if (bsp_lvgl_lock(50)) {
            if (health_ui_mode() == HEALTH_UI_HOME)
                plank_ui_refresh(&s_model, now, battery, s_audio_ok, s_buttons_ok);
            else
                health_ui_refresh(&s_log, now, battery, health_portal_status());
            bsp_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
