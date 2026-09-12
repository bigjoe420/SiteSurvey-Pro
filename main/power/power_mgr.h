#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

// ---------------------------------------------------------------------------
// Power Manager — Backlight PWM dimming, idle timeout, light-sleep gating
// ---------------------------------------------------------------------------

// NVS namespace for persisted power settings
#define SSP_PM_NVS_NAMESPACE   "power_cfg"
#define SSP_PM_NVS_KEY_TIMEOUT "bl_timeout"
#define SSP_PM_NVS_KEY_DIM     "bl_dim"
#define SSP_PM_NVS_KEY_SLEEP   "ls_enable"
#define SSP_PM_NVS_KEY_AUTOOFF "auto_off"

typedef enum {
    SSP_PM_BL_OFF = 0,
    SSP_PM_BL_DIM,
    SSP_PM_BL_FULL,
} ssp_pm_bl_state_t;

// Initialize LEDC on SSP_TFT_BL, restore settings from NVS, start idle timer.
// Call after display_init() but before lvgl_port_start_ui_task().
esp_err_t power_mgr_init(void);

// Set backlight 0–100 %. Persists until next state change or activity reset.
void power_mgr_set_brightness(uint8_t pct);

// Call on every user interaction (touch press, button click) to reset idle.
void power_mgr_activity(void);

// Current backlight state (for UI indicator).
ssp_pm_bl_state_t power_mgr_bl_state(void);

// ---------------------------------------------------------------------------
// Settings accessors (read/write NVS)
// ---------------------------------------------------------------------------

// Backlight timeout in seconds. 0 = never auto-dim.
uint16_t power_mgr_get_timeout_s(void);
void     power_mgr_set_timeout_s(uint16_t seconds);

// Dim level as percentage (10–50). What the backlight drops to after timeout.
uint8_t power_mgr_get_dim_pct(void);
void    power_mgr_set_dim_pct(uint8_t pct);

// Light-sleep enable: CPU sleeps between scans when idle.
bool power_mgr_get_sleep_en(void);
void power_mgr_set_sleep_en(bool en);

// Auto power-off: minutes of touch-idle before deep sleep. 0 = never.
// Deep sleep is the same state as Power Off (wake with BOOT). Off by
// default — a unit left scanning a site must not stop logging on its own.
uint16_t power_mgr_get_auto_off_min(void);
void     power_mgr_set_auto_off_min(uint16_t minutes);

// Full power off: backlight off, then deep sleep. Wake by pressing the BOOT
// button (GPIO0, held low). Consumes only RTC-domain power while off.
void power_mgr_power_off(void);
