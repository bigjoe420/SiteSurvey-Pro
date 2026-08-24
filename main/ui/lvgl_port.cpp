#include "lvgl_port.h"

#include "board_pins.h"
#include "display.h"
#include "touch.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "lvgl_port";

// 1/10th of the screen per buffer, double buffered, in internal RAM
static constexpr size_t BUF_ROWS = 48;
static constexpr size_t BUF_SIZE = SSP_TFT_WIDTH * BUF_ROWS * sizeof(uint16_t);

static esp_lcd_panel_handle_t s_panel;
static lv_display_t* s_disp;

// Backlight is held off until LVGL's first full frame is physically on the
// glass, so the panel never lights up on unfinished pixels.
static volatile bool s_first_frame_done;
static bool s_bl_on;

static void backlight_on_once(const char* why)
{
    if (s_bl_on) return;
    s_bl_on = true;
    display_set_backlight(true);
    ESP_LOGI(TAG, "backlight on: %s", why);
}

// DMA completion callback — called from ISR/task context when the SPI color
// transfer finishes. Signals LVGL that the buffer is free for the next flush.
static bool IRAM_ATTR flush_done_cb(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx)
{
    lv_display_t* disp = (lv_display_t*)user_ctx;
    if (lv_display_flush_is_last(disp)) {
        s_first_frame_done = true;
    }
    lv_display_flush_ready(disp);
    return false;  // no need to yield
}

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    // Non-blocking: draw_bitmap queues the DMA transfer and returns.
    // flush_done_cb fires when the transfer completes.
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
    // lv_display_flush_ready() is NOT called here — it is called from the
    // DMA completion callback. This keeps lv_timer_handler() from blocking on
    // the ~12 ms SPI transfer, so touch reads never get starved.
}

static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    int64_t t0 = esp_timer_get_time();
    int16_t x, y;
    bool pressed = touch_read(&x, &y);
    int64_t elapsed = esp_timer_get_time() - t0;
    if (elapsed > 500) {
        ESP_LOGW(TAG, "touch_read() took %lld us (>500 us) — SPI contention?", elapsed);
    }
    if (pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void tick_cb(void*)
{
    lv_tick_inc(1);
}

static void ui_task(void*)
{
    while (true) {
        int64_t t0 = esp_timer_get_time();
        lv_timer_handler();
        int64_t elapsed = esp_timer_get_time() - t0;
        if (elapsed > 5000) {
            ESP_LOGW(TAG, "lv_timer_handler() took %lld us (>5 ms)", elapsed);
        }
        if (s_first_frame_done) {
            s_first_frame_done = false;
            backlight_on_once("first full frame drawn");
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t lvgl_port_init(void)
{
    ESP_RETURN_ON_ERROR(display_init(&s_panel), TAG, "display init failed");
    ESP_RETURN_ON_ERROR(touch_init(), TAG, "touch init failed");

    void* buf1 = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_INTERNAL);
    void* buf2 = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "RAM buffer alloc failed");

    lv_init();

    s_disp = lv_display_create(SSP_TFT_WIDTH, SSP_TFT_HEIGHT);
    // ST7789 takes RGB565 MSB-first over SPI; render pre-swapped so flush
    // can hand the buffer to esp_lcd untouched
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(s_disp, buf1, buf2, BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);

    // Register DMA completion callback so flush_cb() can be non-blocking.
    ESP_RETURN_ON_ERROR(
        display_register_flush_done_cb(flush_done_cb, s_disp),
        TAG, "flush done cb register failed");

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    // Touch polling at 100 Hz, independent of display refresh (60 Hz)
    lv_timer_set_period(lv_indev_get_read_timer(indev), 10);

    esp_timer_create_args_t tick_args = {};
    tick_args.callback = tick_cb;
    tick_args.name = "lv_tick";
    esp_timer_handle_t tick_timer;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), TAG, "tick timer failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, 1000), TAG, "tick start failed");

    ESP_LOGI(TAG, "LVGL up: %dx%d, 2x%u KB draw buffers in RAM",
             SSP_TFT_WIDTH, SSP_TFT_HEIGHT, (unsigned)(BUF_SIZE / 1024));
    return ESP_OK;
}

void lvgl_port_start_ui_task(void)
{
    // Prio 24: above Wi-Fi driver (prio 23) so LVGL never gets preempted
    // during render. The task yields every 10 ms via vTaskDelay, giving
    // Wi-Fi enough CPU between frames.
    xTaskCreate(ui_task, "ui_task", 6144, nullptr, 24, nullptr);
}
