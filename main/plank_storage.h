#pragma once

#include "esp_err.h"
#include "plank_core.h"

// Only the storage worker calls these functions. Errors never trigger an erase.
esp_err_t plank_storage_load(plank_history_t *history);
esp_err_t plank_storage_save(const plank_history_t *history);
