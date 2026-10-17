#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "plank_core.h"
#include "plank_audio.h"
#include "plank_storage.h"
#include "plank_ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdatomic.h>

typedef struct { plank_key_t key; uint64_t at_ms; } key_event_t;
typedef struct { plank_history_t history; esp_err_t error; } storage_result_t;
typedef struct { unsigned effect, generation; uint64_t at_ms; } audio_event_t;

static const char *TAG = "plank";
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
    if (event != BSP_BTN_PRESS) return;
    key_event_t key = { .at_ms = now_ms() };
    switch (button) {
        case BSP_BTN_UP: key.key = PLANK_UP; break;
        case BSP_BTN_DOWN: key.key = PLANK_DOWN; break;
        default: key.key = PLANK_OK; break;
    }
    // Keep the first pending press and its stop timestamp; coalesce further presses.
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
        while (event.generation == atomic_load(&s_audio_generation) &&
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
    if (bsp_i2c_init() != ESP_OK || bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    s_keys = xQueueCreate(1, sizeof(key_event_t));
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
    int battery = -1;
    uint64_t battery_at = 0;
    for (;;) {
        key_event_t key;
        if (s_buttons_ok && xQueueReceive(s_keys, &key, 0) == pdTRUE) {
            plank_state_t previous = s_model.state;
            effects(plank_key(&s_model, key.key, key.at_ms));
            if ((previous == PLANK_PREPARING && s_model.state == PLANK_READY) || !s_model.sound)
                atomic_fetch_add(&s_audio_generation, 1);
        }
        uint64_t now = now_ms();
        effects(plank_tick(&s_model, now));
        storage_result_t saved;
        if (xQueueReceive(s_storage_results, &saved, 0) == pdTRUE)
            plank_saved(&s_model, saved.error == ESP_OK);
        xQueueReceive(s_audio_status, &s_audio_ok, 0);
        if (s_audio_queue_failed) s_audio_ok = false;
        if (now >= battery_at) {
            battery = s_battery_ok ? bsp_battery_soc() : -1;
            battery_at = now + 10000;
        }
        if (bsp_lvgl_lock(50)) {
            plank_ui_refresh(&s_model, now, battery, s_audio_ok, s_buttons_ok);
            bsp_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
