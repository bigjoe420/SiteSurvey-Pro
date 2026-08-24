#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

// Initializes the shared SPI bus (SPI2), the ST7789 panel at 20 MHz,
// rotation 3 (320x240 landscape), clears GRAM to black, then DISPON.
// Backlight is NOT lit here — lvgl_port lights it after the first full frame.
esp_err_t display_init(esp_lcd_panel_handle_t* out_panel);

// Register a callback that fires when an async DMA color transfer completes.
// This lets lvgl_port signal LVGL that the flush is done without blocking.
esp_err_t display_register_flush_done_cb(esp_lcd_panel_io_color_trans_done_cb_t cb, void* user_ctx);

void display_set_backlight(bool on);
