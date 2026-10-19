#include "health_cfg.h"
#include "nvs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static health_persist_t durable_log, pending_log;
static uint8_t durable_volume, pending_volume;
static bool has_log, has_volume, log_pending, volume_pending;
static esp_err_t commit_error;

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    (void)name;
    (void)mode;
    *handle = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle)
{
    (void)handle;
    log_pending = volume_pending = false;
}
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *length)
{
    (void)handle; (void)key; (void)out; (void)length;
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    (void)handle; (void)key; (void)value;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length)
{
    (void)handle; (void)key;
    if (!has_log) return ESP_ERR_NVS_NOT_FOUND;
    if (*length < sizeof(durable_log)) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, &durable_log, sizeof(durable_log));
    *length = sizeof(durable_log);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
    (void)handle; (void)key;
    assert(length == sizeof(pending_log));
    memcpy(&pending_log, value, length);
    log_pending = true;
    return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    (void)handle; (void)key;
    if (!has_volume) return ESP_ERR_NVS_NOT_FOUND;
    *value = durable_volume;
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    (void)handle; (void)key;
    pending_volume = value;
    volume_pending = true;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    if (commit_error) return commit_error;
    if (log_pending) { durable_log = pending_log; has_log = true; }
    if (volume_pending) { durable_volume = pending_volume; has_volume = true; }
    return ESP_OK;
}

static void save_if_dirty(health_log_t *log, uint32_t *revision)
{
    health_cfg_save_pending(log, revision);
}

int main(void)
{
    assert(health_cfg_init() == ESP_OK);
    assert(health_cfg_save_volume(50) == ESP_OK);
    assert(health_cfg_load_volume() == 50);
    assert(health_cfg_save_volume(0) == ESP_OK);
    assert(health_cfg_load_volume() == 0);
    assert(health_cfg_save_volume(101) == ESP_ERR_INVALID_ARG);
    assert(health_cfg_load_volume() == 0);
    commit_error = ESP_FAIL;
    assert(health_cfg_save_volume(90) == ESP_FAIL);
    assert(health_cfg_load_volume() == 0);
    commit_error = ESP_OK;

    health_log_t log;
    health_log_init(&log, NULL, 0);
    uint32_t revision = log.revision;
    health_entry_t entry = { .exercise = HEALTH_EX_SQUAT, .reps = 8, .sets = 1, .weight_x10 = 600 };
    assert(health_log_add_entry(&log, &entry, false, 1000));
    save_if_dirty(&log, &revision);
    entry.weight_x10 = 650;
    assert(health_log_add_entry(&log, &entry, true, 2000));
    commit_error = ESP_FAIL;
    save_if_dirty(&log, &revision);
    assert(revision != log.revision);
    health_persist_t loaded;
    assert(health_cfg_load_log(&loaded) && loaded.entries[0].weight_x10 == 600);
    commit_error = ESP_OK;
    save_if_dirty(&log, &revision);
    assert(health_cfg_load_log(&loaded) && loaded.entries[0].weight_x10 == 650);
    for (unsigned i = 1; i < HEALTH_ENTRY_CAP; i++) {
        entry.weight_x10 = 700;
        assert(health_log_add_entry(&log, &entry, false, 3000 + i));
    }
    save_if_dirty(&log, &revision);
    entry.weight_x10 = 750;
    assert(health_log_add_entry(&log, &entry, false, 4000));
    save_if_dirty(&log, &revision);
    assert(health_cfg_load_log(&loaded));
    assert(loaded.entry_count == HEALTH_ENTRY_CAP && loaded.entries[0].weight_x10 == 700);
    assert(loaded.entries[HEALTH_ENTRY_CAP - 1].weight_x10 == 750);
    assert(health_log_undo_voice(&log));
    save_if_dirty(&log, &revision);
    assert(health_cfg_load_log(&loaded) && loaded.entry_count == HEALTH_ENTRY_CAP - 1);
    health_log_t reboot;
    health_log_init(&reboot, &loaded, 0);
    assert(reboot.saved.entries[reboot.saved.entry_count - 1].weight_x10 == 700);
    assert(health_cfg_load_volume() == 0);
    puts("Health persistence: PASS (mute, correction, full history, undo, commit failure)");
    return 0;
}
