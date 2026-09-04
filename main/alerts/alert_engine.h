#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "scan_engine.h"
#include "gps.h"

#define ALERT_MAX_SSID_TARGETS  4
#define ALERT_MAX_BSSID_TARGETS 4
#define ALERT_QUEUE_DEPTH      16
#define ALERT_MAX_LOG_ENTRIES  32   // circular buffer for on-screen log

typedef enum {
    ALERT_MATCH_SSID = 0,
    ALERT_MATCH_BSSID,
    ALERT_MATCH_RSSI,
} AlertMatchType_t;

typedef struct {
    bool enabled;
    char ssid_targets[ALERT_MAX_SSID_TARGETS][33];
    uint8_t ssid_count;
    char bssid_targets[ALERT_MAX_BSSID_TARGETS][18];
    uint8_t bssid_count;
    int8_t rssi_threshold;   // alert when RSSI >= this (e.g. -80 means "stronger than -80")
} AlertConfig_t;

typedef struct {
    char timestamp[20];      // YYYY-MM-DD HH:MM:SS
    char target[33];         // SSID or BSSID that matched
    int8_t rssi;
    uint8_t channel;
    float lat;
    float lon;
    uint8_t sats;
    AlertMatchType_t match_type;
} AlertEntry_t;

// Load config from NVS (or initialise with demo targets if absent).
// Call once before scan_engine_start_task().
esp_err_t alert_engine_init(void);

// Check a scan result against the current target config.
// If it matches, posts an AlertEntry_t to the alert queue.
// Call from late_init_task context (same thread that owns the GPS state).
void alert_check(const ScanResult_t* ap, const GpsState* gps);

// Consumer handle for the alert UI.
QueueHandle_t alert_queue(void);

// Read-only access to current config (for Settings screen later).
const AlertConfig_t* alert_config_get(void);

// Write config and persist to NVS (for Settings screen later).
esp_err_t alert_config_set(const AlertConfig_t* cfg);

// Circular log buffer: copy up to `max` most-recent entries into `out`.
// Returns actual count copied. Safe to call from ui_task.
int alert_log_snapshot(AlertEntry_t* out, int max);

// Clear the circular log buffer.
void alert_log_clear(void);
