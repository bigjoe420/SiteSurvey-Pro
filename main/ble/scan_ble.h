#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// One BLE advertisement observation. Posted to ble_scan_queue per device per cycle.
typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    char name[32];
    uint8_t mfg_data[32];
    uint8_t mfg_len;
    uint8_t severity;  // ssp_rssi_tier_t
} BleScanResult_t;

// Brings BLE controller up in observer mode and creates ble_scan_queue.
esp_err_t ble_scan_init(void);

// Consumer handle, valid after ble_scan_init().
QueueHandle_t ble_scan_queue(void);

// Copies the device pool (strongest signal first) into `out`, returns count.
// Spinlock-guarded against ble_scan_task upserts; safe from ui_task.
int ble_scan_snapshot(BleScanResult_t* out, int max);

// Starts ble_scan_task (3 KB stack): passive scan loop.
void ble_scan_start_task(void);
