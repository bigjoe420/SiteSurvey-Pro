#pragma once

#include <stdint.h>
#include "esp_err.h"

// Adds the XPT2046 as a second device on the shared SPI2 bus (2.5 MHz, CS=GPIO1).
// The bus must already be initialized by display_init().
esp_err_t touch_init(void);

// Calibration aid: raw 12-bit ADC values straight off the SPI bus.
// No calibration constants, no rotation math.
bool touch_read_raw(uint16_t* raw_x, uint16_t* raw_y);

// Polls the XPT2046. Returns true when the panel is pressed, with x/y mapped
// into 320x240 landscape screen coordinates using the factory calibration.
// No IRQ line is wired on this board, so callers poll on a timer.
bool touch_read(int16_t* x, int16_t* y);
