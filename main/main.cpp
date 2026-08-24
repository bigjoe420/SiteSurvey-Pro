// =============================================================================
// SiteSurvey Pro — Application Entry Point
// NM-CYD-C5 (ESP32-C5 RISC-V) + ESP-IDF v5.x
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "board_pins.h"
#include "lvgl_port.h"
#include "ui_home.h"
#include "ui_env.h"
#include "ui_splash.h"
#include "scan_engine.h"
#include "sensors.h"
#include "sd_card.h"

static const char* TAG = "SiteSurvey";

// ---------------------------------------------------------------------------
// Board Initialization
// ---------------------------------------------------------------------------
static void board_init_gpio(void)
{
    gpio_config_t io_conf = {};

    // Drive all SPI CS lines HIGH before touching the bus
    uint64_t cs_mask = (1ULL << SSP_TFT_CS)
                     | (1ULL << SSP_TOUCH_CS)
                     | (1ULL << SSP_SDCARD_CS);

    io_conf.pin_bit_mask = cs_mask;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level(SSP_TFT_CS, 1);
    gpio_set_level(SSP_TOUCH_CS, 1);
    gpio_set_level(SSP_SDCARD_CS, 1);

    // Backlight: output, start OFF until display is ready
    gpio_set_direction(SSP_TFT_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(SSP_TFT_BL, 0);

    ESP_LOGI(TAG, "GPIO init complete. CS lines HIGH, BL LOW.");
}

// ---------------------------------------------------------------------------
// Late-init task: everything that can block or stall the system
// Runs at prio 1 so ui_task (prio 24) always preempts it.
// ---------------------------------------------------------------------------
static void late_init_task(void*)
{
    // Give the splash one full frame (10 ms) before init hogs flash/SPI bus
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_ERROR_CHECK(scan_engine_init());
    ESP_ERROR_CHECK(sensors_init());

    // Build queue set and add BOTH queues BEFORE any task can post.
    // xQueueAddToSet() fails (pdFAIL) if the queue already holds data.
    QueueHandle_t scan_queue = scan_engine_queue();
    QueueHandle_t env_queue  = sensors_queue();
    QueueSetHandle_t qs = xQueueCreateSet(16 + 8);
    ESP_ERROR_CHECK(qs ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xQueueAddToSet(scan_queue, qs) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xQueueAddToSet(env_queue,  qs) == pdPASS ? ESP_OK : ESP_FAIL);

    // NOW start producer tasks — safe to post because queues are already
    // members of the queue set.
    scan_engine_start_task();
    sensors_start_task();

    // Non-fatal: an absent card only means no session logging this boot
    sd_card_init();

    // Event loop: drains scan_queue + env_queue via the queue set
    bool scan_ready = false;
    bool env_ready  = false;
    while (true) {
        QueueSetMemberHandle_t member = xQueueSelectFromSet(qs, portMAX_DELAY);
        if (member == scan_queue) {
            ScanResult_t ap;
            xQueueReceive(member, &ap, 0);
            ESP_LOGI(TAG, "%-32s %02X:%02X:%02X:%02X:%02X:%02X %s ch%-3u %4d dBm %c %s",
                     (const char*)ap.ssid,
                     ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
                     ap.channel <= 14 ? "2.4G" : "5G ",
                     ap.channel, ap.rssi,
                     SSP_RSSI_TIER_CHAR[ap.severity],
                     scan_engine_auth_str(ap.authmode));
            if (!scan_ready) {
                scan_ready = true;
                ui_splash_notify_scan_ready();
            }
        } else if (member == env_queue) {
            EnvSnapshot_t snap;
            xQueueReceive(member, &snap, 0);
            ui_env_post_env(&snap, sensors_bme680_present());
            if (snap.env_valid) {
                int32_t t = snap.env.temp_c_x100;
                ESP_LOGI(TAG, "env: %ld.%02ld C  %lu.%02lu %%RH  %lu Pa  gas %lu ohm",
                         (long)(t / 100), (long)(labs(t) % 100),
                         (unsigned long)(snap.env.hum_x100 / 100),
                         (unsigned long)(snap.env.hum_x100 % 100),
                         (unsigned long)snap.env.press_pa, (unsigned long)snap.env.gas_ohm);
            }
            if (snap.gps.fix_valid) {
                ESP_LOGI(TAG, "gps: fix q=%u sats=%u lat=%ld.%07ld lon=%ld.%07ld utc=%06lu",
                         snap.gps.fix_quality, snap.gps.sats,
                         (long)(snap.gps.lat_e7 / 10000000), (long)(labs(snap.gps.lat_e7) % 10000000),
                         (long)(snap.gps.lon_e7 / 10000000), (long)(labs(snap.gps.lon_e7) % 10000000),
                         (unsigned long)snap.gps.utc_hhmmss);
            } else {
                ESP_LOGI(TAG, "gps: no fix (sats=%u utc=%06lu) nmea ok=%lu bad=%lu",
                         snap.gps.sats, (unsigned long)snap.gps.utc_hhmmss,
                         (unsigned long)snap.gps.sentences_ok, (unsigned long)snap.gps.sentences_bad);
            }
            if (!env_ready && (snap.env_valid || !sensors_bme680_present())) {
                env_ready = true;
                ui_splash_notify_env_ready();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Main Application
// ---------------------------------------------------------------------------
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "SiteSurvey Pro booting...");
    ESP_LOGI(TAG, "Target: ESP32-C5 | Flash: 16MB | PSRAM: 8MB");

    // GPIO first: clamps backlight LOW and parks all SPI CS lines HIGH
    board_init_gpio();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(lvgl_port_init());

    // Splash owns the display first. Home screen is deferred via callback
    // until splash gates clear — preventing ~100+ widget objects + 25KB PSRAM
    // from pressuring memory while the waterfall animation runs.
    ui_splash_show([]() -> lv_obj_t* { return ui_home_create(); });
    lvgl_port_start_ui_task();

    ESP_LOGI(TAG, "UI pipeline up - free heap: %lu bytes", esp_get_free_heap_size());

    // Spin heavy init into a background task at prio 1.  ui_task (prio 24)
    // will always preempt it, so the splash animation never stalls.
    xTaskCreate(late_init_task, "late_init", 6144, nullptr, 1, nullptr);
}
