#pragma once

#include "lvgl.h"
#include "scan_engine.h"

typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    wifi_auth_mode_t authmode;
    ssp_rssi_tier_t severity;
} WifiApInfo_t;

// Create the detail screen. back_cb is called when the back button is tapped.
// The screen persists and is updated via ui_wifi_detail_update().
lv_obj_t* ui_wifi_detail_create(const WifiApInfo_t* info, lv_event_cb_t back_cb);

// Update the detail screen with a new AP's data (SSID, BSSID, chart series colour).
void ui_wifi_detail_update(const WifiApInfo_t* info);

// Visibility gate for the refresh timer.
void ui_wifi_detail_set_visible(bool visible);
