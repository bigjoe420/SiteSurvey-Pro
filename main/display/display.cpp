#include "display.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "display";

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

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num = SSP_TFT_CS;
    io_cfg.dc_gpio_num = SSP_TFT_DC;
    io_cfg.spi_mode = 0;
    io_cfg.pclk_hz = SSP_TFT_SPI_FREQ_HZ;
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                                 &io_cfg, &io),
                        TAG, "panel IO init failed");

    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = SSP_TFT_RST;     // chip-level reset, no GPIO
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_cfg.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel),
                        TAG, "ST7789 panel init failed");

    // Driver init emits SLPOUT with its own 120 ms settle delay
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), TAG, "invert failed");
    // Rotation 3: swap axes into landscape, then flip vertically
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel, true), TAG, "swap_xy failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel, false, true), TAG, "mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "DISPON failed");

    // Backlight comes up only after DISPON to avoid white-flash on boot
    display_set_backlight(true);

    *out_panel = panel;
    ESP_LOGI(TAG, "ST7789 up: %dx%d landscape, SPI2 @ %lu MHz",
             SSP_TFT_WIDTH, SSP_TFT_HEIGHT, SSP_TFT_SPI_FREQ_HZ / 1000000UL);
    return ESP_OK;
}

void display_set_backlight(bool on)
{
    gpio_set_level(SSP_TFT_BL, on ? 1 : 0);
}
