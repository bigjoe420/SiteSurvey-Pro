#include "display.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "display";

static esp_lcd_panel_io_handle_t s_io;  // kept for callback registration

esp_err_t display_init(esp_lcd_panel_handle_t* out_panel)
{
    // ST7789 needs stable power rails before it accepts SPI commands
    vTaskDelay(pdMS_TO_TICKS(SSP_TFT_RST_WAIT_MS));

    // Shared SPI2 bus: display, touch, and SD card all live here.
    // Touch adds its own device handle at a lower clock (touch.cpp).
    spi_bus_config_t bus = {};
    bus.mosi_io_num = SSP_SPI_MOSI;
    bus.miso_io_num = SSP_SPI_MISO;
    bus.sclk_io_num = SSP_SPI_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = SSP_TFT_WIDTH * 24 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus init failed");

    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num = SSP_TFT_CS;
    io_cfg.dc_gpio_num = SSP_TFT_DC;
    io_cfg.spi_mode = 0;
    io_cfg.pclk_hz = SSP_TFT_SPI_FREQ_HZ;
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                                 &io_cfg, &s_io),
                        TAG, "panel IO init failed");

    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = SSP_TFT_RST;     // chip-level reset, no GPIO
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_cfg.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &panel),
                        TAG, "ST7789 panel init failed");

    // Fast init: same command order as esp_lcd_panel_init() but with a shorter
    // SLPOUT settle (50 ms instead of 100 ms). MADCTL must come before COLMOD
    // and RAMCTRL so the panel latches them in the right orientation.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, LCD_CMD_SLPOUT, NULL, 0),
                        TAG, "SLPOUT failed");
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, LCD_CMD_MADCTL,
                        (uint8_t[]){LCD_CMD_BGR_BIT}, 1), TAG, "MADCTL failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, LCD_CMD_COLMOD,
                        (uint8_t[]){0x55}, 1), TAG, "COLMOD failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, 0xB0,
                        (uint8_t[]){0x00, 0xF0}, 2), TAG, "RAMCTRL failed");

    // This panel's native polarity is correct as-is; no INVON needed
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, false), TAG, "invert failed");
    // Rotation 3: swap axes into landscape, then flip vertically
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel, true), TAG, "swap_xy failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel, false, true), TAG, "mirror failed");

    // ST7789 GRAM powers up with random content. With the display still off,
    // paint the whole panel black so the first visible frame is clean.
    // Batched into 24-row chunks (one SPI transaction each, ~10 ms total).
    size_t chunk_rows = 24;
    size_t chunk_bytes = SSP_TFT_WIDTH * chunk_rows * sizeof(uint16_t);
    uint16_t* black_chunk = (uint16_t*)heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA);
    if (black_chunk) {
        memset(black_chunk, 0, chunk_bytes);
        for (int y = 0; y < SSP_TFT_HEIGHT; y += chunk_rows) {
            int h = (y + chunk_rows > SSP_TFT_HEIGHT) ? (SSP_TFT_HEIGHT - y) : chunk_rows;
            ESP_RETURN_ON_ERROR(
                esp_lcd_panel_draw_bitmap(panel, 0, y, SSP_TFT_WIDTH, y + h, black_chunk),
                TAG, "GRAM clear failed at y=%d", y);
        }
        heap_caps_free(black_chunk);
    } else {
        // Fallback: row-by-row if heap is too tight
        uint16_t black_row[SSP_TFT_WIDTH] = {};
        for (int y = 0; y < SSP_TFT_HEIGHT; y++) {
            ESP_RETURN_ON_ERROR(
                esp_lcd_panel_draw_bitmap(panel, 0, y, SSP_TFT_WIDTH, y + 1, black_row),
                TAG, "GRAM clear failed at row %d", y);
        }
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "DISPON failed");

    // Backlight is deferred to lvgl_port.cpp backlight_on_once() so the
    // panel never lights up on unfinished pixels / GRAM clear artifacts.

    *out_panel = panel;
    ESP_LOGI(TAG, "ST7789 up: %dx%d landscape, SPI2 @ %lu MHz",
             SSP_TFT_WIDTH, SSP_TFT_HEIGHT, SSP_TFT_SPI_FREQ_HZ / 1000000UL);
    return ESP_OK;
}

esp_err_t display_register_flush_done_cb(esp_lcd_panel_io_color_trans_done_cb_t cb, void* user_ctx)
{
    esp_lcd_panel_io_callbacks_t cbs = {};
    cbs.on_color_trans_done = cb;
    return esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, user_ctx);
}

void display_set_backlight(bool on)
{
    gpio_set_level(SSP_TFT_BL, on ? 1 : 0);
}
