#pragma once

#include <stdint.h>
#include "esp_err.h"

// Adds the XPT2046 as a second device on the shared SPI2 bus (2.5 MHz, CS=GPIO1).
// The bus must already be initialized by display_init().
esp_err_t touch_init(void);

// Start a background 50 Hz sampler that polls the XPT2046 independently of
// LVGL.  Stores the latest coordinates so touch_read_latest() is lock-free.
esp_err_t touch_start_sampler(void);

// Read the most recent sample from the background sampler (no SPI).
// Returns true if the panel is currently pressed, with x/y in screen coords.
bool touch_read_latest(int16_t* x, int16_t* y);

// Calibration aid: raw 12-bit ADC values straight off the SPI bus.
// No calibration constants, no rotation math.
bool touch_read_raw(uint16_t* raw_x, uint16_t* raw_y);

// Polls the XPT2046. Returns true when the panel is pressed, with x/y mapped
// into 320x240 landscape screen coordinates using the factory calibration.
// No IRQ line is wired on this board, so callers poll on a timer.
bool touch_read(int16_t* x, int16_t* y);
