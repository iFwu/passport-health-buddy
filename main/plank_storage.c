#include "plank_storage.h"

#include "nvs.h"
#include "nvs_flash.h"

static bool s_writable;

esp_err_t plank_storage_load(plank_history_t *history)
{
    s_writable = false;
    *history = (plank_history_t){ .version = 1 };
    esp_err_t err = nvs_flash_init_partition("nvs");
    if (err != ESP_OK) return err;
    nvs_handle_t handle;
    err = nvs_open_from_partition("nvs", "plank", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_writable = true;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    size_t size = sizeof(*history);
    err = nvs_get_blob(handle, "history_v1", history, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *history = (plank_history_t){ .version = 1 };
        err = ESP_OK;
    } else if (err == ESP_OK && (size != sizeof(*history) || !plank_history_valid(history))) {
        err = ESP_ERR_INVALID_STATE;
    }
    s_writable = err == ESP_OK;
    return err;
}

esp_err_t plank_storage_save(const plank_history_t *history)
{
    if (!s_writable || !plank_history_valid(history)) return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("nvs", "plank", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "history_v1", history, sizeof(*history));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
