#include "health_cfg.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

static const char *NS = "health";

esp_err_t health_cfg_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) return err;
    return err;
}

static esp_err_t read_str(nvs_handle_t handle, const char *key, char *out, size_t cap)
{
    size_t len = cap;
    out[0] = '\0';
    esp_err_t err = nvs_get_str(handle, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        out[0] = '\0';
        return ESP_OK;
    }
    return err;
}

bool health_cfg_load_net(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap,
                         char *key, size_t key_cap)
{
    nvs_handle_t handle;
    if (nvs_open(NS, NVS_READONLY, &handle) != ESP_OK) return false;
    bool ok = read_str(handle, "ssid", ssid, ssid_cap) == ESP_OK &&
              read_str(handle, "pass", pass, pass_cap) == ESP_OK &&
              read_str(handle, "key", key, key_cap) == ESP_OK;
    nvs_close(handle);
    return ok && ssid[0] && key[0];
}

esp_err_t health_cfg_save_net(const char *ssid, const char *pass, const char *key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "pass", pass ? pass : "");
    if (err == ESP_OK) err = nvs_set_str(handle, "key", key);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

bool health_cfg_load_log(health_persist_t *log)
{
    memset(log, 0, sizeof(*log));
    nvs_handle_t handle;
    if (nvs_open(NS, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t len = sizeof(*log);
    esp_err_t err = nvs_get_blob(handle, "log_v1", log, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(*log) || log->set_count > HEALTH_SET_CAP) {
        memset(log, 0, sizeof(*log));
        return false;
    }
    return true;
}

esp_err_t health_cfg_save_log(const health_persist_t *log)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "log_v1", log, sizeof(*log));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
