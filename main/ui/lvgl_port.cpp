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

// 1/10th of the screen per buffer, double buffered, in PSRAM
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

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    // draw_bitmap queues the DMA transfer and returns (non-blocking for the
    // queue; brief block only if the driver's internal queue is full).  With
    // double-buffered partial mode, LVGL renders into the other buffer while
    // DMA reads from this one, so we can signal flush-ready immediately.
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);

    if (lv_display_flush_is_last(disp)) {
        s_first_frame_done = true;
    }
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    int16_t x, y;
    bool pressed = touch_read_latest(&x, &y);
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
        lv_timer_handler();

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
    ESP_RETURN_ON_ERROR(touch_start_sampler(), TAG, "touch sampler start failed");

    void* buf1 = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM);
    void* buf2 = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "PSRAM buffer alloc failed");

    lv_init();

    /* Add a PSRAM-backed secondary pool to LVGL. The 64 KB internal pool can
     * be exhausted by heavy screens (Settings keyboard), causing lv_realloc to
     * return NULL and LV_ASSERT_MALLOC to hang in while(1). The PSRAM pool
     * absorbs overflow so the UI never OOMs. */
    constexpr size_t LV_PSRAM_POOL_SIZE = 256 * 1024;
    void * lv_psram_pool = heap_caps_malloc(LV_PSRAM_POOL_SIZE, MALLOC_CAP_SPIRAM);
    if (lv_psram_pool) {
        lv_mem_pool_t pool = lv_mem_add_pool(lv_psram_pool, LV_PSRAM_POOL_SIZE);
        if (pool) {
            ESP_LOGI(TAG, "LVGL PSRAM pool added: %u KB", (unsigned)(LV_PSRAM_POOL_SIZE / 1024));
        } else {
            ESP_LOGW(TAG, "lv_mem_add_pool failed, freeing PSRAM block");
            heap_caps_free(lv_psram_pool);
        }
    } else {
        ESP_LOGW(TAG, "PSRAM pool alloc failed, LVGL limited to internal pool");
    }

    s_disp = lv_display_create(SSP_TFT_WIDTH, SSP_TFT_HEIGHT);
    // ST7789 takes RGB565 MSB-first over SPI; render pre-swapped so flush
    // can hand the buffer to esp_lcd untouched
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(s_disp, buf1, buf2, BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    // Touch polling at 100 Hz; the sampler runs at 50 Hz, so every other
    // read gets a fresh sample and the in-betweens repeat the latest coord.
    lv_timer_set_period(lv_indev_get_read_timer(indev), 10);

    esp_timer_create_args_t tick_args = {};
    tick_args.callback = tick_cb;
    tick_args.name = "lv_tick";
    esp_timer_handle_t tick_timer;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), TAG, "tick timer failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, 1000), TAG, "tick start failed");

    ESP_LOGI(TAG, "LVGL up: %dx%d, 2x%u KB draw buffers in PSRAM",
             SSP_TFT_WIDTH, SSP_TFT_HEIGHT, (unsigned)(BUF_SIZE / 1024));
    return ESP_OK;
}

void lvgl_port_start_ui_task(void)
{
    // Prio 24: above Wi-Fi driver (prio 23) so LVGL never gets preempted
    // during render. The task yields every 10 ms via vTaskDelay, giving
    // Wi-Fi enough CPU between frames.
    xTaskCreate(ui_task, "ui_task", 12288, nullptr, 24, nullptr);
}
