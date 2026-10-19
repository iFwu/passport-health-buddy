#include "health_portal.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "health_cfg.h"
#include "health_wifi.h"
#include "portal_form.h"
#include "esp_system.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "health_portal";
static char s_status[48] = "等待浏览器打开 192.168.4.1";
static httpd_handle_t s_http;

extern const char portal_html_start[] asm("_binary_portal_html_start");
extern const char portal_html_end[] asm("_binary_portal_html_end");

const char *health_portal_status(void)
{
    return s_status;
}

static bool key_accepted(const char *key)
{
    esp_http_client_config_t cfg = {
        .url = "https://api.stepfun.com/v1/models",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;
    char header[HEALTH_KEY_MAX + 16];
    snprintf(header, sizeof(header), "Bearer %s", key);
    esp_http_client_set_header(client, "Authorization", header);
    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "Key 校验 HTTP %d (%s)", status, esp_err_to_name(err));
    return err == ESP_OK && status == 200;
}

static void save_worker(void *arg)
{
    char *body = arg;
    char ssid[HEALTH_SSID_MAX + 1], pass[HEALTH_PASS_MAX + 1], key[HEALTH_KEY_MAX + 1];
    const size_t len = strlen(body);
    const bool fields = form_get(body, len, "ssid", ssid, sizeof(ssid)) > 0 &&
                        form_get(body, len, "pass", pass, sizeof(pass)) >= 0 &&
                        form_get(body, len, "key", key, sizeof(key)) > 0;
    free(body);
    if (!fields || !form_ssid_valid(ssid) || !form_wifi_pass_valid(pass) ||
        !form_step_key_valid(key, HEALTH_KEY_MAX)) {
        snprintf(s_status, sizeof(s_status), "名称、密码或 Key 格式不对");
        vTaskDelete(NULL);
        return;
    }
    snprintf(s_status, sizeof(s_status), "正在连接 Wi-Fi…");
    if (health_wifi_connect(ssid, pass, 20000) != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "Wi-Fi 连接失败");
        vTaskDelete(NULL);
        return;
    }
    snprintf(s_status, sizeof(s_status), "正在验证 Key…");
    if (!key_accepted(key)) {
        snprintf(s_status, sizeof(s_status), "Key 无效或暂时连不上");
        vTaskDelete(NULL);
        return;
    }
    if (health_cfg_save_net(ssid, pass, key) != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "保存失败");
        vTaskDelete(NULL);
        return;
    }
    snprintf(s_status, sizeof(s_status), "已保存，正在重启");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, portal_html_start, portal_html_end - portal_html_start - 1);
}

static esp_err_t save_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad form");
        return ESP_FAIL;
    }
    char *body = calloc(1, req->content_len + 1);
    if (!body) return ESP_FAIL;
    int got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) {
            free(body);
            return ESP_FAIL;
        }
        got += n;
    }
    if (xTaskCreate(save_worker, "portal", 8192, body, 5, NULL) != pdPASS) {
        free(body);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"message\":\"已收到，设备正在连接\"}");
}

static esp_err_t status_get(httpd_req_t *req)
{
    char json[96];
    snprintf(json, sizeof(json), "{\"message\":\"%s\"}", s_status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

esp_err_t health_portal_start(const char *ap_ssid, const char *ap_pass)
{
    esp_err_t err = health_wifi_start_ap(ap_ssid, ap_pass);
    if (err != ESP_OK) return err;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 4096;
    cfg.lru_purge_enable = true;
    err = httpd_start(&s_http, &cfg);
    if (err != ESP_OK) return err;
    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get};
    const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post};
    const httpd_uri_t status = {.uri = "/status", .method = HTTP_GET, .handler = status_get};
    httpd_register_uri_handler(s_http, &root);
    httpd_register_uri_handler(s_http, &save);
    httpd_register_uri_handler(s_http, &status);
    return ESP_OK;
}
