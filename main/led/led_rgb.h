#pragma once

#include <stdint.h>
#include "esp_err.h"

// ---------------------------------------------------------------------------
// WS2812 RGB LED driver — GPIO 27, GRB order, single pixel
// Uses ESP-IDF RMT TX with copy encoder (no external dependencies).
// ---------------------------------------------------------------------------

// Initialise RMT channel on SSP_LED_RGB. Call once before any set/off/alert.
esp_err_t led_rgb_init(void);

// Set solid colour (R, G, B). 0x00 = off, 0xFF = full brightness.
// Safe to call from any task context.
void led_rgb_set(uint8_t r, uint8_t g, uint8_t b);

// Turn LED off (convenience wrapper for led_rgb_set(0,0,0)).
void led_rgb_off(void);

// Brief red flash (non-blocking timer-based).
// Intended to be called from alert_check() on target match.
void led_rgb_alert(void);
