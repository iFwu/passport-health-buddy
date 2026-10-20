#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t, bool);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void *);
