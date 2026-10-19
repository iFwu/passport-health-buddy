#pragma once
#include "plank_core.h"

// The caller owns the LVGL lock. The screen lives for the application's lifetime.
void plank_ui_create(void);
void plank_ui_refresh(const plank_model_t *model, uint64_t now, int battery,
                      bool audio_ok, bool buttons_ok, const char *voice_text,
                      uint32_t voice_color, unsigned volume, bool volume_visible);
