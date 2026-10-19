#pragma once

#include "health_input.h"
#include "health_log.h"
#include "plank_core.h"

#include "plank_ui.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HEALTH_UI_PORTAL,
    HEALTH_UI_MENU,
    HEALTH_UI_EVERYDAY,
    HEALTH_UI_GYM,
    HEALTH_UI_HOME,
    HEALTH_UI_HELP,
} health_ui_mode_t;

typedef enum {
    HEALTH_VOICE_OFF,
    HEALTH_VOICE_CONNECTING,
    HEALTH_VOICE_READY,
    HEALTH_VOICE_RECORDING,
    HEALTH_VOICE_THINKING,
    HEALTH_VOICE_SPEAKING,
    HEALTH_VOICE_ERROR,
} health_voice_state_t;

// The caller holds the LVGL lock for every UI API.
void health_ui_create(lv_obj_t *plank_screen);
void health_ui_set_portal(const char *ssid, const char *password, const char *status);

health_ui_mode_t health_ui_mode(void);
void health_ui_set_mode(health_ui_mode_t mode);

// Only mode navigation and confirmation mutate UI/application state here.
unsigned health_ui_action(health_action_t action, health_log_t *log,
                          plank_model_t *model, uint64_t now_ms);
// Call after create with the persisted volume, before the first refresh, then
// after volume changes. Zero keeps the mute badge visible beyond the overlay.
void health_ui_set_volume(unsigned percent, uint64_t now_ms);
void health_ui_notice(const char *text, uint64_t now_ms);
void health_ui_refresh(const health_log_t *log, const plank_model_t *model,
                       uint64_t now_ms, int battery, const char *status,
                       bool audio_ok, bool buttons_ok, health_voice_state_t voice);
