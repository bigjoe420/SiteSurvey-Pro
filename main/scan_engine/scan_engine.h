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

// One scan observation. Posted to scan_queue (depth 16) per AP per cycle.
// Band is implicit: channel <= 14 is 2.4 GHz, anything above is 5 GHz.
typedef struct {
    uint8_t ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
    ssp_rssi_tier_t severity;
} ScanResult_t;

// Brings Wi-Fi up in scan-only station mode (never connects) and creates scan_queue.
esp_err_t scan_engine_init(void);

// Consumer handle, valid after scan_engine_init().
QueueHandle_t scan_engine_queue(void);

// Copies the AP pool (strongest signal first) into `out`, returns count.
// Spinlock-guarded against wifi_scan_task upserts; safe from ui_task.
int scan_engine_snapshot(ScanResult_t* out, int max);

// Starts wifi_scan_task (3 KB stack): alternating 2.4/5 GHz active scan loop.
void scan_engine_start_task(void);

const char* scan_engine_auth_str(wifi_auth_mode_t mode);
