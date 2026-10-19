#include "health_wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"

#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>

static const char *TAG = "health_wifi";
static EventGroupHandle_t s_events;
static esp_netif_t *s_sta;
static bool s_connected;
static bool s_auto_reconnect;
static bool s_connecting;

#define GOT_IP BIT0
#define FAILED BIT1

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupSetBits(s_events, FAILED);
        if (s_auto_reconnect && !s_connecting) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        if (s_sta) esp_netif_set_default_netif(s_sta);
        xEventGroupSetBits(s_events, GOT_IP);
        ESP_LOGI(TAG, "Wi-Fi 已拿到地址");
    }
}

esp_err_t health_wifi_init(void)
{
    s_events = xEventGroupCreate();
    if (!s_events) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_sta = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL);
    if (err == ESP_OK)
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    return err;
}

esp_err_t health_wifi_connect(const char *ssid, const char *pass, int timeout_ms)
{
    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, pass ? pass : "", sizeof(sta.sta.password));
    sta.sta.threshold.authmode = (pass && pass[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "连接 Wi-Fi，名称长度 %u", (unsigned)strlen(ssid));
    s_auto_reconnect = true;
    s_connecting = true;
    for (int attempt = 0; attempt < 3; attempt++) {
        xEventGroupClearBits(s_events, GOT_IP | FAILED);
        err = esp_wifi_connect();
        if (err != ESP_OK) {
            s_connecting = false;
            return err;
        }
        const EventBits_t bits = xEventGroupWaitBits(
            s_events, GOT_IP | FAILED, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms / 3));
        if (bits & GOT_IP) {
            s_connecting = false;
            return ESP_OK;
        }
    }
    s_connecting = false;
    return ESP_FAIL;
}

esp_err_t health_wifi_start_ap(const char *ssid, const char *pass)
{
    s_auto_reconnect = false;
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ssid);
    strlcpy((char *)ap.ap.password, pass, sizeof(ap.ap.password));
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err == ESP_OK) ESP_LOGI(TAG, "配网热点已启动");
    return err;
}

bool health_wifi_connected(void)
{
    return s_connected;
}

void health_clock_start(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();
}

uint32_t health_clock_day(void)
{
    time_t now = 0;
    time(&now);
    if (now < 1700000000) return 0;
    struct tm local;
    localtime_r(&now, &local);
    return (uint32_t)((local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday);
}
