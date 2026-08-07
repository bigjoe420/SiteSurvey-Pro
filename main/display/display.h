#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

// Initializes the shared SPI bus (SPI2), the ST7789 panel at 20 MHz,
// rotation 3 (320x240 landscape), and switches the backlight on last.
esp_err_t display_init(esp_lcd_panel_handle_t* out_panel);

void display_set_backlight(bool on);
