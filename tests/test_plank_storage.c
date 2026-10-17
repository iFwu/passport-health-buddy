#include "plank_storage.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static plank_history_t durable, staged;
static bool exists;
static size_t blob_size;
static esp_err_t init_error, open_error, read_error, write_error, commit_error;
static unsigned writes, commits, opens, closes;

esp_err_t nvs_flash_init_partition(const char *partition)
{
    assert(strcmp(partition, "nvs") == 0);
    return init_error;
}
esp_err_t nvs_open_from_partition(const char *partition, const char *name,
                                   nvs_open_mode_t mode, nvs_handle_t *handle)
{
    assert(strcmp(partition, "nvs") == 0 && strcmp(name, "plank") == 0);
    if (open_error) return open_error;
    if (!exists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 42;
    opens++;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size)
{
    assert(handle == 42 && strcmp(key, "history_v1") == 0);
    if (read_error) return read_error;
    if (*size < blob_size) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, &durable, blob_size);
    *size = blob_size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size)
{
    assert(handle == 42 && strcmp(key, "history_v1") == 0 && size == sizeof(staged));
    writes++;
    if (write_error) return write_error;
    memcpy(&staged, data, size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 42);
    commits++;
    if (commit_error) return commit_error;
    durable = staged;
    blob_size = sizeof(durable);
    exists = true;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle)
{
    assert(handle == 42);
    closes++;
}

int main(void)
{
    plank_history_t loaded;
    assert(plank_storage_load(&loaded) == ESP_OK && loaded.count == 0);
    plank_model_t m;
    plank_init(&m, &loaded, true);
    plank_key(&m, PLANK_OK, 0);
    plank_tick(&m, 5000);
    plank_key(&m, PLANK_OK, 5600);
    assert(plank_storage_save(&m.history) == ESP_OK);
    assert(plank_storage_load(&loaded) == ESP_OK && loaded.duration_ms[0] == 600);

    plank_history_t next = loaded;
    next.count = 2;
    next.revision = 2;
    next.duration_ms[0] = 900;
    next.duration_ms[1] = 600;
    commit_error = ESP_FAIL;
    assert(plank_storage_save(&next) == ESP_FAIL);
    assert(plank_storage_load(&loaded) == ESP_OK && loaded.count == 1);
    commit_error = ESP_OK;
    assert(plank_storage_save(&next) == ESP_OK);
    assert(plank_storage_save(&next) == ESP_OK);
    assert(plank_storage_load(&loaded) == ESP_OK && loaded.count == 2);

    unsigned before = commits;
    write_error = ESP_FAIL;
    assert(plank_storage_save(&next) == ESP_FAIL && commits == before);
    write_error = ESP_OK;
    open_error = ESP_FAIL;
    assert(plank_storage_save(&next) == ESP_FAIL);
    open_error = ESP_OK;

    durable.version = 2;
    before = writes;
    assert(plank_storage_load(&loaded) == ESP_ERR_INVALID_STATE);
    assert(plank_storage_save(&next) == ESP_ERR_INVALID_STATE && writes == before);
    durable.version = 1;
    blob_size--;
    assert(plank_storage_load(&loaded) == ESP_ERR_INVALID_STATE);
    blob_size = sizeof(durable) + 1;
    assert(plank_storage_load(&loaded) == ESP_ERR_NVS_INVALID_LENGTH);
    blob_size = sizeof(durable);
    init_error = ESP_FAIL;
    assert(plank_storage_load(&loaded) == ESP_FAIL);
    assert(plank_storage_save(&next) == ESP_ERR_INVALID_STATE);
    init_error = ESP_OK;
    read_error = ESP_FAIL;
    assert(plank_storage_load(&loaded) == ESP_FAIL);
    assert(plank_storage_save(&next) == ESP_ERR_INVALID_STATE);
    read_error = ESP_ERR_NVS_NOT_FOUND;
    assert(plank_storage_load(&loaded) == ESP_OK && loaded.count == 0);
    assert(opens == closes);
    puts("Plank storage: PASS");
    return 0;
}
