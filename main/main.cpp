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
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "board_pins.h"
#include "lvgl_port.h"
#include "ui_home.h"
#include "ui_env.h"
#include "ui_wifi.h"
#include "ui_ble.h"
#include "ui_gps.h"
#include "ui_splash.h"
#include "scan_engine.h"
#include "scan_ble.h"
#include "sensors.h"
#include "sd_card.h"
#include "session_logger.h"
#include "alert_engine.h"
#include "led_rgb.h"
#include "power_mgr.h"
#include "flash_broker.h"
#include "ui_theme.h"

static const char* TAG = "SiteSurvey";

// ---------------------------------------------------------------------------
// DMA heap diagnostics — pinpoints who eats DMA-capable internal RAM
// ---------------------------------------------------------------------------
static void log_dma_heap(const char* where)
{
    size_t dma_free     = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t dma_largest  = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    size_t total_free   = esp_get_free_heap_size();
    ESP_LOGI(TAG, "[DMA] %-22s free=%6u  largest=%6u  total=%6u",
             where,
             (unsigned)dma_free,
             (unsigned)dma_largest,
             (unsigned)total_free);
}

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

    log_dma_heap("before sd_card_init");

    // SD init FIRST — before WiFi/BLE consume DMA-capable internal RAM.
    // With psram_dma_direct=1 the display DMAs from PSRAM, but SDSPI still
    // needs small internal DMA buffers for CMD/response transactions. Once
    // WiFi (prio 23) and NimBLE (prio 21) allocate their descriptors there
    // is often not enough contiguous DMA RAM left for sdmmc_send_cmd.
    esp_err_t sd_err = sd_card_init();
    if (sd_err == ESP_OK) {
        session_logger_init();
    }
    log_dma_heap("after sd_card_init");

    ESP_ERROR_CHECK(scan_engine_init());
    log_dma_heap("after scan_engine_init");

    ESP_ERROR_CHECK(sensors_init());
    log_dma_heap("after sensors_init");

    ESP_ERROR_CHECK(ble_scan_init());
    log_dma_heap("after ble_scan_init");

    ESP_ERROR_CHECK(alert_engine_init());
    log_dma_heap("after alert_engine_init");

    ESP_ERROR_CHECK(led_rgb_init());
    log_dma_heap("after led_rgb_init");

    // Build queue set and add ALL queues BEFORE any task can post.
    // xQueueAddToSet() fails (pdFAIL) if the queue already holds data.
    QueueHandle_t scan_queue = scan_engine_queue();
    QueueHandle_t env_queue  = sensors_queue();
    QueueHandle_t ble_queue  = ble_scan_queue();
    QueueSetHandle_t qs = xQueueCreateSet(16 + 8 + 16);
    ESP_ERROR_CHECK(qs ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xQueueAddToSet(scan_queue, qs) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xQueueAddToSet(env_queue,  qs) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xQueueAddToSet(ble_queue,  qs) == pdPASS ? ESP_OK : ESP_FAIL);

    // NOW start producer tasks — safe to post because queues are already
    // members of the queue set.
    scan_engine_start_task();
    sensors_start_task();
    ble_scan_start_task();
    log_dma_heap("after all tasks started");

    // Latest GPS state cached for session logging (updated every 5 s).
    GpsState latest_gps = {};

    // Event loop: drains scan_queue + env_queue + ble_queue via the queue set
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
            session_logger_log_ap(&ap, &latest_gps);
            alert_check(&ap, &latest_gps);
            if (!scan_ready) {
                scan_ready = true;
                ui_splash_notify_scan_ready();
            }
        } else if (member == env_queue) {
            EnvSnapshot_t snap;
            xQueueReceive(member, &snap, 0);
            ui_env_post_env(&snap, sensors_bme680_present());
            ui_wifi_post_env(&snap, sensors_bme680_present());
            ui_gps_post_gps(&snap.gps);

            // Cache GPS state for the next scan log entry.
            latest_gps = snap.gps;

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
        } else if (member == ble_queue) {
            BleScanResult_t dev;
            xQueueReceive(member, &dev, 0);
            // DEBUG, not INFO: one line per advertisement (~8-9/s in busy
            // environments). At INFO with no serial monitor draining the
            // console, the UART TX ring buffer fills in seconds and this
            // superloop task blocks inside esp_log — starving every queue
            // consumer behind it (UI posts, alerts, session data). Same
            // blocking-console shape as the 2026-08-31 freeze incident.
            ESP_LOGD(TAG, "ble: %-20s %02X:%02X:%02X:%02X:%02X:%02X %4d dBm %c",
                     dev.name[0] ? dev.name : "<unknown>",
                     dev.mac[0], dev.mac[1], dev.mac[2], dev.mac[3], dev.mac[4], dev.mac[5],
                     dev.rssi,
                     SSP_RSSI_TIER_CHAR[dev.severity]);
        }
    }
}

// ---------------------------------------------------------------------------
// Main Application
// ---------------------------------------------------------------------------
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "SiteSurvey Pro booting...");
    // Permanent: knowing why the previous boot ended (panic/WDT/brownout/SW)
    // is the first thing needed when diagnosing any field reset report.
    ESP_LOGI(TAG, "reset reason: %d (%s)", (int)esp_reset_reason(),
             "0=POWERON 1=HARD 3=SW 4=PANIC 5=INT_WDT 6=TASK_WDT 7=BROWNOUT 8=WDT 9=DEEP_SLEEP");
    ESP_LOGI(TAG, "Target: ESP32-C5 | Flash: 16MB | PSRAM: 8MB");
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
        ESP_LOGI(TAG, "woke from deep sleep (BOOT button)");
    }

    log_dma_heap("boot start");

    // GPIO first: clamps backlight LOW and parks all SPI CS lines HIGH
    board_init_gpio();
    log_dma_heap("after gpio");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    log_dma_heap("after nvs");

    ESP_ERROR_CHECK(lvgl_port_init());
    log_dma_heap("after lvgl_port_init");

    ESP_ERROR_CHECK(power_mgr_init());
    log_dma_heap("after power_mgr_init");

    // UI is single-theme (dark high-contrast); screens read the palette at
    // creation time on the UI task.
    ESP_LOGI(TAG, "UI theme: dark (single palette)");

    scan_filter_init();
    log_dma_heap("after scan_filter_init");

    // Flash-write broker: NVS commits requested by PSRAM-stack tasks (UI)
    // run here on an internal-RAM stack. Must exist before the UI task starts.
    // (Was disabled during a splash bisect — without it every settings save
    // runs NVS inline on the PSRAM-stack UI task and the CPU locks up.)
    flash_broker_init();

    // Splash owns the display first. Home screen is deferred via callback
    // until splash gates clear — preventing ~100+ widget objects + 25KB PSRAM
    // from pressuring memory while the waterfall animation runs.
    ui_splash_show([]() -> lv_obj_t* { return ui_home_create(); });
    lvgl_port_start_ui_task();

    log_dma_heap("after ui_task start");
    ESP_LOGI(TAG, "UI pipeline up - free heap: %lu bytes", esp_get_free_heap_size());

    // Spin heavy init into a background task at prio 1.  ui_task (prio 24)
    // will always preempt it, so the splash animation never stalls.
    // INTERNAL stack: WiFi/BLE init performs NVS flash writes (default config
    // on first boot, IRK persist); a PSRAM stack is unreachable while the
    // cache is down during those writes and locks the CPU.
    xTaskCreate(late_init_task, "late_init", 6144, nullptr, 1, nullptr);

}
