#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// RSSI severity tier thresholds (dBm)
static constexpr int8_t SSP_RSSI_STRONG_DBM   = -50;
static constexpr int8_t SSP_RSSI_MODERATE_DBM = -70;
static constexpr int8_t SSP_RSSI_WEAK_DBM     = -85;

typedef enum {
    SSP_RSSI_STRONG = 0,
    SSP_RSSI_MODERATE,
    SSP_RSSI_WEAK,
    SSP_RSSI_MARGINAL,
} ssp_rssi_tier_t;

static constexpr char SSP_RSSI_TIER_CHAR[] = {'S', 'M', 'W', 'X'};

#define RSSI_HISTORY_LEN 64  // 64 samples x 5 s scan cadence = ~5.3 min window

// One scan observation. Posted to scan_queue (depth 16) per AP per cycle.
// Band is implicit: channel <= 14 is 2.4 GHz, anything above is 5 GHz.
typedef struct {
    uint8_t ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
    ssp_rssi_tier_t severity;
    bool rogue;  // true if another AP shares this SSID but different BSSID
} ScanResult_t;

typedef struct {
    bool band_2g;          // show 2.4 GHz APs
    bool band_5g;          // show 5 GHz APs
    int8_t min_rssi;       // minimum RSSI to display (-100 .. -30)
    char ssid_pattern[33]; // substring match; empty = match all
} ScanFilter_t;

// Initialize filter subsystem (loads from NVS). Call after nvs_flash_init().
void scan_filter_init(void);

const ScanFilter_t* scan_filter_get(void);

void scan_filter_set_band_2g(bool en);
void scan_filter_set_band_5g(bool en);
void scan_filter_set_min_rssi(int8_t dbm);
void scan_filter_set_ssid_pattern(const char* pattern);

// Same as scan_engine_snapshot but applies ScanFilter_t rules.
int scan_engine_snapshot_filtered(ScanResult_t* out, int max);

// Brings Wi-Fi up in scan-only station mode (never connects) and creates scan_queue.
esp_err_t scan_engine_init(void);

// Consumer handle, valid after scan_engine_init().
QueueHandle_t scan_engine_queue(void);

// Copies the AP pool (strongest signal first) into `out`, returns count.
// Spinlock-guarded against wifi_scan_task upserts; safe from ui_task.
int scan_engine_snapshot(ScanResult_t* out, int max);

// Starts wifi_scan_task (3 KB stack): alternating 2.4/5 GHz active scan loop.
void scan_engine_start_task(void);

// Retrieve RSSI history for an AP by BSSID. Writes up to max_samples into out_rssi.
// Samples are in chronological order (oldest first). Returns sample count (0 if AP not found).
int scan_engine_get_history(const uint8_t bssid[6], int8_t* out_rssi, int max_samples);

const char* scan_engine_auth_str(wifi_auth_mode_t mode);
