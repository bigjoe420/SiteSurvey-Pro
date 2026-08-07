// =============================================================================
// SiteSurvey Pro — Application Entry Point
// NM-CYD-C5 (ESP32-C5 RISC-V) + ESP-IDF v5.x
// =============================================================================

#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "board_pins.h"
#include "lvgl_port.h"
#include "ui_hello.h"

static const char* TAG = "SiteSurvey";

// -----------------------------------------------------------------------------
// Board Initialization
// -----------------------------------------------------------------------------
static void board_init_gpio(void)
{
    gpio_config_t io_conf = {};

    // Drive all SPI CS lines HIGH before touching the bus
    // Prevents bus contention during init
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

// -----------------------------------------------------------------------------
// Main Application
// -----------------------------------------------------------------------------
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "SiteSurvey Pro booting...");
    ESP_LOGI(TAG, "Target: ESP32-C5 | Flash: 16MB | PSRAM: 8MB");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    board_init_gpio();

    ESP_ERROR_CHECK(lvgl_port_init());
    ui_hello_show();
    lvgl_port_start_ui_task();

    ESP_LOGI(TAG, "UI pipeline up - free heap: %lu bytes", esp_get_free_heap_size());
}
