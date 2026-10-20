// Exercise the real BSP with a queued-DMA model, not a synchronous flush mock.
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "../components/bsp/src/bsp_display_lvgl.c"

static lv_display_t display;
static lv_obj_t screen;
static uint16_t dma_pixels[BSP_LCD_W * BSP_LVGL_DRAW_BUFFER_LINES];
static int panel_present = 1, lock_depth, port_live, display_live;
static int callback_live, refresh_callback_live;
static int fail_lock, fail_next_lock, fail_port, fail_display, fail_event;
static int init_calls, unlocked_flushes, pending_dma, invalidated;
static int panel_on = 1, panel_sleeping, command_count, draw_calls, dma_completions;
static int fail_command = -2, fail_barrier, fail_draw_at, fail_relock;
static int other_timer_calls, delays, redraws, drawn_rows;
static uint64_t elapsed_ms, panel_ready_ms;
static int panel_token, io_token;

esp_lcd_panel_handle_t bsp_display_panel(void) { return panel_present ? &panel_token : NULL; }
esp_lcd_panel_io_handle_t bsp_display_io(void) { return &io_token; }
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg) {
    (void)cfg; ++init_calls; assert(!port_live);
    if (fail_port) return ESP_ERR_NO_MEM;
    port_live = 1; return ESP_OK;
}
esp_err_t lvgl_port_deinit(void) {
    // The real API is asynchronous: returning does not release the context.
    assert(false && "Display rollback must not deinit/reinitialize the live port");
    return ESP_OK;
}
bool lvgl_port_lock(uint32_t timeout) {
    (void)timeout; assert(port_live);
    if (fail_lock) return false;
    if (fail_next_lock) { fail_next_lock = 0; return false; }
    ++lock_depth; return true;
}
void lvgl_port_unlock(void) {
    assert(lock_depth > 0);
    --lock_depth;
    if (!lock_depth && display_live && !display.timer.paused && invalidated) {
        // Simulate the LVGL task taking the mutex as soon as it is available.
        assert(callback_live && refresh_callback_live && display.flush_cb);
        ++unlocked_flushes;
        ++lock_depth; lv_refr_now(&display); --lock_depth;
    }
}
lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *cfg) {
    assert(lock_depth > 0 && cfg->panel_handle == &panel_token && !display_live);
    assert(!cfg->double_buffer && cfg->flags.swap_bytes);
    assert(lvgl_port_lock(0)); // The real port uses a recursive lock.
    if (!fail_display) {
        display_live = invalidated = 1;
        display.timer.paused = false;
    }
    lvgl_port_unlock();
    return fail_display ? NULL : &display;
}
esp_err_t lvgl_port_remove_disp(lv_display_t *disp) {
    assert(disp == &display && lock_depth && display_live);
    display_live = callback_live = refresh_callback_live = 0; return ESP_OK;
}
void lv_display_add_event_cb(lv_display_t *disp, void (*cb)(lv_event_t *), int code, void *user) {
    (void)user;
    assert(lock_depth && disp == &display);
    if (code == LV_EVENT_FLUSH_START) {
        assert(cb == rounded_flush_event);
        if (fail_event != 1) callback_live = 1;
    } else {
        assert(code == LV_EVENT_REFR_REQUEST && cb == refresh_request_event);
        if (fail_event != 2) refresh_callback_live = 1;
    }
}
uint32_t lv_display_get_event_count(lv_display_t *disp) {
    assert(disp == &display && lock_depth);
    return 1 + callback_live + refresh_callback_live;
}
void lv_display_set_flush_cb(lv_display_t *disp,
                           void (*cb)(lv_display_t *, const lv_area_t *, uint8_t *)) {
    assert(lock_depth && disp == &display); disp->flush_cb = cb;
}
void lv_display_flush_ready(lv_display_t *disp) {
    assert(disp == &display && disp->flushing); disp->flushing = false;
}
static void complete_dma(void) {
    if (!pending_dma) return;
    // ISR completion needs no LVGL mutex, so queue-drain under it cannot deadlock.
    pending_dma = 0; ++dma_completions; lv_display_flush_ready(&display);
}
void lv_draw_sw_rgb565_swap(void *pixels, uint32_t count) {
    uint16_t *p = pixels;
    for (uint32_t i = 0; i < count; ++i) p[i] = (uint16_t)((p[i] >> 8) | (p[i] << 8));
}
lv_timer_t *lv_display_get_refr_timer(lv_display_t *disp) {
    assert(disp == &display && lock_depth); return &display.timer;
}
void lv_timer_pause(lv_timer_t *timer) { assert(lock_depth); timer->paused = true; }
void lv_timer_resume(lv_timer_t *timer) { assert(lock_depth); timer->paused = false; }
void lv_timer_ready(lv_timer_t *timer) { assert(lock_depth && timer == &display.timer); }
lv_obj_t *lv_display_get_screen_active(lv_display_t *disp) {
    assert(disp == &display && lock_depth); return &screen;
}
void lv_obj_invalidate(lv_obj_t *obj) {
    assert(obj == &screen && lock_depth);
    invalidated = 1;
    // LVGL's built-in REFR_REQUEST handler runs BEFORE our registered handler.
    display.timer.paused = false;
    if (refresh_callback_live) {
        lv_event_t ev = { .target = &display };
        refresh_request_event(&ev);
    }
}
void lv_refr_now(lv_display_t *disp) {
    assert(disp == &display && lock_depth && display.flush_cb);
    if (!invalidated) return;
    display.timer.paused = true;
    invalidated = 0; ++redraws;
    for (int y = 0; y < BSP_LCD_H; y += BSP_LVGL_DRAW_BUFFER_LINES) {
        // A single-buffer renderer waits before reusing its DMA buffer.
        complete_dma();
        assert(!display.flushing);
        for (size_t i = 0; i < sizeof(dma_pixels) / sizeof(dma_pixels[0]); ++i) dma_pixels[i] = 0x1234;
        display.buffer = (lv_draw_buf_t){ .data = (uint8_t *)dma_pixels,
                                        .header.stride = BSP_LCD_W * sizeof(uint16_t) };
        lv_area_t area = { .x1 = 0, .y1 = y, .x2 = BSP_LCD_W - 1,
                           .y2 = y + BSP_LVGL_DRAW_BUFFER_LINES - 1 };
        lv_event_t event = { .target = &display, .area = &area };
        rounded_flush_event(&event);
        display.flushing = true;
        display.flush_cb(&display, &area, (uint8_t *)dma_pixels);
        // On rejected submission there will be no ISR: BSP must release LVGL.
        assert(pending_dma || !display.flushing);
    }
    // Like LVGL 9.5 partial refresh, return with the final DMA still in flight.
}
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int command,
                                    const void *param, size_t size) {
    assert(io == &io_token && lock_depth && !param && !size);
    if (command == -1 && fail_barrier) { fail_barrier = 0; return ESP_FAIL; }
    // Actual IDF SPI tx_param drains the color queue before polling a command.
    complete_dma();
    if (command == -1) return ESP_OK;
    ++command_count;
    assert(elapsed_ms >= panel_ready_ms);
    if (command == LCD_CMD_DISPOFF) panel_on = 0;
    else if (command == LCD_CMD_DISPON) { assert(!panel_sleeping); panel_on = 1; }
    else {
        assert(command == LCD_CMD_SLPIN || command == LCD_CMD_SLPOUT);
        panel_sleeping = command == LCD_CMD_SLPIN;
        panel_ready_ms = elapsed_ms + 120;
    }
    // Model ambiguous transport errors: the controller might have accepted it.
    if (fail_command == command) { fail_command = -2; return ESP_FAIL; }
    return ESP_OK;
}
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on) {
    assert(panel == &panel_token);
    return esp_lcd_panel_io_tx_param(&io_token, on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x1, int y1,
                                    int x2, int y2, const void *pixels) {
    assert(panel == &panel_token && lock_depth && !panel_sleeping && panel_on);
    assert(elapsed_ms >= panel_ready_ms && !pending_dma);
    assert(x1 == 0 && x2 == BSP_LCD_W && y2 - y1 == BSP_LVGL_DRAW_BUFFER_LINES);
    assert(((const uint16_t *)pixels)[BSP_LCD_W / 2] == 0x3412);
    if (y1 == 0) {
        assert(((const uint16_t *)pixels)[0] == 0);
        assert(((const uint16_t *)pixels)[BSP_LCD_W - 1] == 0);
    }
    ++draw_calls;
    if (draw_calls == fail_draw_at) return ESP_ERR_NO_MEM;
    drawn_rows |= 1 << (y1 / BSP_LVGL_DRAW_BUFFER_LINES);
    pending_dma = 1; return ESP_OK;
}
void vTaskDelay(TickType_t ticks) {
    assert(!lock_depth && ticks >= pdMS_TO_TICKS(120));
    elapsed_ms += ticks; ++delays;
    // Other LVGL timers continue to mutate/invalidate UI while LCD settles.
    assert(lvgl_port_lock(0));
    ++other_timer_calls; lv_obj_invalidate(&screen);
    assert(display.timer.paused);
    lvgl_port_unlock();
    if (fail_relock) { fail_relock = 0; fail_next_lock = 1; }
}
static void expect_failure(void) {
    assert(bsp_lvgl_init() == NULL);
    assert(!s_disp && !lock_depth && !display_live);
    assert(!bsp_lvgl_lock(0));
}
static void expect_paused(void) {
    assert(s_refresh_paused && !s_panel_asleep && display.timer.paused && !lock_depth);
    const int before = draw_calls;
    assert(lvgl_port_lock(0)); lv_obj_invalidate(&screen); lvgl_port_unlock();
    assert(display.timer.paused && draw_calls == before);
    // Even explicit refresh bypassing the paused timer must not reach SPI.
    assert(lvgl_port_lock(0)); lv_refr_now(&display); lvgl_port_unlock();
    assert(draw_calls == before);
}
static void expect_awake(void) {
    drawn_rows = 0;
    assert(bsp_display_set_sleep(false) == ESP_OK);
    assert(!s_refresh_paused && !s_panel_asleep && !display.timer.paused && !lock_depth);
    assert(!panel_sleeping && panel_on && !pending_dma && !display.flushing);
    assert(drawn_rows == 0xff); // Full 320 rows, not just a stale dirty rectangle.
}
int main(void) {
    assert(bsp_display_set_sleep(true) == ESP_ERR_INVALID_STATE);
    assert(bsp_display_set_sleep(false) == ESP_ERR_INVALID_STATE);
    panel_present = 0; expect_failure(); panel_present = 1;
    fail_port = 1; expect_failure(); fail_port = 0;
    const int failed_init_calls = init_calls;
    expect_failure(); // A partially initialized port cannot safely be re-created.
    assert(init_calls == failed_init_calls);
    s_port_init_failed = false; // Simulate reboot for remaining scenarios.
    fail_lock = 1; expect_failure(); fail_lock = 0;
    const int retained_init_calls = init_calls;
    fail_display = 1; expect_failure(); fail_display = 0;
    fail_event = 1; expect_failure();
    fail_event = 2; expect_failure(); fail_event = 0;
    assert(bsp_lvgl_init() == &display);
    assert(init_calls == retained_init_calls); // Display retries reuse the port.
    assert(callback_live && refresh_callback_live && !lock_depth && unlocked_flushes == 1);
    assert(pending_dma); // Mutex release did NOT imply the last DMA completed.
    const int before = init_calls;
    assert(bsp_lvgl_init() == &display && init_calls == before);
    assert(bsp_lvgl_lock(5)); bsp_lvgl_unlock();

    fail_lock = 1;
    assert(bsp_display_set_sleep(true) == ESP_ERR_INVALID_STATE);
    fail_lock = 0;
    assert(!s_refresh_paused);
    assert(bsp_display_set_sleep(true) == ESP_OK);
    assert(s_panel_asleep && s_refresh_paused && panel_sleeping && !panel_on && !pending_dma);
    const int commands_before = command_count, delays_before = delays;
    assert(bsp_display_set_sleep(true) == ESP_OK);
    assert(command_count == commands_before && delays == delays_before);
    // Unrelated UI invalidation cannot undo sleep; explicit refresh is guarded.
    const int draws_before = draw_calls;
    assert(lvgl_port_lock(0)); lv_obj_invalidate(&screen); lv_refr_now(&display); lvgl_port_unlock();
    assert(draw_calls == draws_before && display.timer.paused);
    expect_awake();
    const int awake_commands = command_count, awake_redraws = redraws;
    assert(bsp_display_set_sleep(false) == ESP_OK);
    assert(command_count == awake_commands && redraws == awake_redraws);

    fail_command = LCD_CMD_DISPOFF;
    assert(bsp_display_set_sleep(true) == ESP_FAIL); expect_paused(); expect_awake();
    fail_command = LCD_CMD_SLPIN;
    assert(bsp_display_set_sleep(true) == ESP_FAIL); expect_paused();
    // Retrying the same sleep target must also be possible after partial error.
    assert(bsp_display_set_sleep(true) == ESP_OK);
    fail_command = LCD_CMD_SLPOUT;
    assert(bsp_display_set_sleep(false) == ESP_FAIL); expect_paused(); expect_awake();
    assert(bsp_display_set_sleep(true) == ESP_OK);
    fail_command = LCD_CMD_DISPON;
    assert(bsp_display_set_sleep(false) == ESP_FAIL); expect_paused(); expect_awake();
    assert(bsp_display_set_sleep(true) == ESP_OK);
    fail_draw_at = draw_calls + 2;
    assert(bsp_display_set_sleep(false) == ESP_ERR_NO_MEM); expect_paused(); expect_awake();
    assert(bsp_display_set_sleep(true) == ESP_OK);
    fail_barrier = 1;
    assert(bsp_display_set_sleep(false) == ESP_FAIL);
    assert(pending_dma); // Retry must also drain a previously failed wake's DMA.
    expect_paused(); expect_awake();
    fail_relock = 1;
    assert(bsp_display_set_sleep(true) == ESP_ERR_INVALID_STATE); expect_paused(); expect_awake();
    fail_relock = 1;
    assert(bsp_display_set_sleep(true) == ESP_ERR_INVALID_STATE); expect_paused();
    fail_relock = 1;
    assert(bsp_display_set_sleep(false) == ESP_ERR_INVALID_STATE); expect_paused(); expect_awake();
    for (int i = 0; i < 20; ++i) {
        assert(bsp_display_set_sleep(true) == ESP_OK); expect_awake();
    }
    assert(other_timer_calls == delays && dma_completions > 0 && !lock_depth);
    puts("BSP LVGL initialization and reversible display sleep tests: PASS");
}
