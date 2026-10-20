// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入单独成文件:不用 LVGL 的开发者删掉本文件 + idf_component.yml 里的两条依赖即可。
#include "bsp_display.h"
#include "bsp_display_rounding.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "bsp_lvgl";

#define BSP_LVGL_DRAW_BUFFER_LINES 40

static lv_display_t *s_disp;
static bool s_port_initialized;
static bool s_port_init_failed;
static bool s_refresh_paused;
static bool s_panel_asleep;
static esp_err_t s_flush_error;

// LVGL 9.5's internal REFR_REQUEST callback resumes the refresh timer, even
// when an object only marks its layout dirty. Run after it so UI/animation
// timers may keep changing objects without restarting an asleep display.
static void refresh_request_event(lv_event_t *event)
{
    if (s_refresh_paused) {
        lv_timer_pause(lv_display_get_refr_timer(lv_event_get_target(event)));
    }
}

static void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels)
{
    // Also protect against an explicit lv_refr_now() while the panel is asleep.
    if (s_refresh_paused) {
        lv_display_flush_ready(disp);
        return;
    }
    // This BSP uses partial, single-buffer RGB565 with no software rotation.
    // Port 2.9's equivalent callback discards draw_bitmap's error, leaving
    // LVGL waiting forever for a DMA completion that will never arrive.
    lv_draw_sw_rgb565_swap(pixels, lv_area_get_size(area));
    esp_err_t err = esp_lcd_panel_draw_bitmap(bsp_display_panel(), area->x1, area->y1,
                                             area->x2 + 1, area->y2 + 1, pixels);
    if (err != ESP_OK) {
        if (s_flush_error == ESP_OK) s_flush_error = err;
        lv_display_flush_ready(disp);
        ESP_LOGE(TAG, "LCD flush failed: %s", esp_err_to_name(err));
    }
}

static void rounded_flush_event(lv_event_t *event)
{
    lv_display_t *disp = lv_event_get_target(event);
    const lv_area_t *area = lv_event_get_param(event);
    lv_draw_buf_t *draw_buf = lv_display_get_buf_active(disp);
    if (!area || !draw_buf || !draw_buf->data ||
        lv_display_get_color_format(disp) != LV_COLOR_FORMAT_RGB565) {
        return;
    }

    const int32_t width = lv_area_get_width(area);
    if (draw_buf->header.stride < (uint32_t)width * sizeof(uint16_t)) return;

    for (int32_t y = area->y1; y <= area->y2; ++y) {
        uint16_t *row = (uint16_t *)(draw_buf->data +
                                     (y - area->y1) * draw_buf->header.stride);
        int32_t visible_x1;
        int32_t visible_x2;
        if (!bsp_display_rounded_row_span(y, BSP_LCD_W, BSP_LCD_H,
                                          BSP_LVGL_SCREEN_RADIUS, &visible_x1,
                                          &visible_x2)) {
            memset(row, 0, (size_t)width * sizeof(uint16_t));
            continue;
        }
        // Only clear pixels outside the visible span. The port swaps RGB565
        // bytes after this event; black is 0 in either byte order.
        const int32_t clear_left_end = visible_x1 > area->x2 ? area->x2 : visible_x1 - 1;
        const int32_t clear_right_start = visible_x2 < area->x1 ? area->x1 : visible_x2 + 1;
        for (int32_t x = area->x1; x <= clear_left_end; ++x) {
            row[x - area->x1] = 0;
        }
        for (int32_t x = clear_right_start; x <= area->x2; ++x) {
            row[x - area->x1] = 0;
        }
    }

}

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    if (!s_port_initialized) {
        // Port 2.9.0 has no public completion handshake for asynchronous deinit.
        // Never overwrite a possibly live context after a partial port failure.
        if (s_port_init_failed) {
            ESP_LOGE(TAG, "LVGL port 初始化未完成，需重启后重试");
            return NULL;
        }
        const lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
        if (lvgl_port_init(&pc) != ESP_OK) {
            s_port_init_failed = true;
            ESP_LOGE(TAG, "lvgl_port_init 失败，需重启后重试");
            return NULL;
        }
        s_port_initialized = true;
    }

    const lvgl_port_display_cfg_t dc = {
        .panel_handle = bsp_display_panel(),
        .io_handle    = bsp_display_io(),
        // ⚠ C3 无 PSRAM,DMA 只能用内部 RAM。40 行单缓冲约 19.2KB，
        // 可减少窗口命令和队列提交次数；仍保留单缓冲，避免双缓冲挤压音频/Wi-Fi。
        .buffer_size   = (uint32_t)BSP_LCD_W * BSP_LVGL_DRAW_BUFFER_LINES,
        .double_buffer = false,
        .hres = BSP_LCD_W, .vres = BSP_LCD_H,
        // 旋转/镜像必须在这里配:esp_lvgl_port 注册显示时会重新下发 MADCTL,
        // 覆盖 bsp_display.c 里 esp_lcd_panel_mirror() 的设置。
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        // swap_bytes:LVGL 输出小端 RGB565,ST7789 走 SPI 要大端 → 需交换高低字节。
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    // The port mutex is recursive. Keep registration and the mask callback in
    // one critical section, before the new display can produce its first flush.
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "LVGL 初始化加锁失败");
        return NULL;
    }
    lv_display_t *disp = lvgl_port_add_disp(&dc);
    bool callbacks_registered = false;
    if (disp) {
        // LVGL 9.5 returns void here; check both registrations under the lock.
        const uint32_t count = lv_display_get_event_count(disp);
        lv_display_add_event_cb(disp, rounded_flush_event, LV_EVENT_FLUSH_START, NULL);
        lv_display_add_event_cb(disp, refresh_request_event, LV_EVENT_REFR_REQUEST, NULL);
        callbacks_registered = lv_display_get_event_count(disp) == count + 2;
    }
    if (!callbacks_registered) {
        ESP_LOGE(TAG, "LVGL display 或圆角回调注册失败");
        if (disp) lvgl_port_remove_disp(disp);
        lvgl_port_unlock();
        // Retain the initialized port for retry. Deinit is asynchronous and can
        // race the next init (or even run before the task sets running=true).
        return NULL;
    }

    // Mask the final RGB565 flush instead of using root-screen clip_corner.
    // Full-screen rounded clipping creates an ARGB layer that does not fit the
    // 24 KB LVGL pool reliably on this no-PSRAM target.
    s_disp = disp;
    lv_display_set_flush_cb(disp, display_flush);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "LVGL 就绪，全局圆角=%d，外部填充=黑色", BSP_LVGL_SCREEN_RADIUS);
    return s_disp;
}

esp_err_t bsp_display_set_sleep(bool asleep)
{
    if (!s_disp || !bsp_display_panel() || !bsp_display_io()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lvgl_port_lock(0)) return ESP_ERR_INVALID_STATE;
    if ((asleep && s_panel_asleep) || (!asleep && !s_refresh_paused)) {
        lvgl_port_unlock();
        return ESP_OK;
    }

    s_refresh_paused = true;
    s_panel_asleep = false; // Until the entire requested transition succeeds.
    lv_timer_pause(lv_display_get_refr_timer(s_disp));

    // The mutex only excludes new rendering, NOT the final queued SPI DMA.
    // IDF 5.5.3 tx_param drains all color transactions before polling its
    // command. DISPOFF therefore waits for the final flush before display-off.
    // The port's DMA ISR only calls flush_ready; it never takes the LVGL lock.
    esp_err_t err = ESP_OK;
    if (asleep) err = esp_lcd_panel_disp_on_off(bsp_display_panel(), false);
    if (err == ESP_OK) {
        // IDF's ST7789 disp_sleep adds only 100ms. Send the same command here
        // with a full 120ms settling interval outside the LVGL lock, also
        // covering an immediate reverse request and a potentially sent error.
        err = esp_lcd_panel_io_tx_param(bsp_display_io(),
                                       asleep ? LCD_CMD_SLPIN : LCD_CMD_SLPOUT, NULL, 0);
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(120) + 1);
        if (err != ESP_OK) return err;
        if (!lvgl_port_lock(0)) return ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK && asleep) {
        s_panel_asleep = true;
    } else if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(bsp_display_panel(), true);
        if (err == ESP_OK) {
            s_refresh_paused = false;
            s_flush_error = ESP_OK;
            lv_obj_invalidate(lv_display_get_screen_active(s_disp));
            lv_refr_now(s_disp);
            s_refresh_paused = true;
            lv_timer_pause(lv_display_get_refr_timer(s_disp));
            // A partial-buffer refresh returns with its final DMA outstanding.
            // -1 is the documented commandless tx_param queue-drain barrier.
            esp_err_t drain_err = esp_lcd_panel_io_tx_param(bsp_display_io(), -1, NULL, 0);
            err = s_flush_error != ESP_OK ? s_flush_error : drain_err;
            if (err == ESP_OK) {
                s_refresh_paused = false;
                lv_timer_resume(lv_display_get_refr_timer(s_disp));
                lv_timer_ready(lv_display_get_refr_timer(s_disp));
            }
        }
    }
    // Every failure leaves refresh paused and the state non-idempotent so a
    // later wake repeats the hardware sequence rather than claiming success.
    lvgl_port_unlock();
    return err;
}

bool bsp_lvgl_lock(int timeout_ms) {
    if (!s_disp) return false;
    return lvgl_port_lock(timeout_ms);
}
void bsp_lvgl_unlock(void) {
    if (s_disp) lvgl_port_unlock();
}
