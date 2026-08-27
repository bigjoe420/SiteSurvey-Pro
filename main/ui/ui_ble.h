#pragma once

#include "lvgl.h"

// BLE device list screen: strongest-first rows with RSSI bars, name/MAC,
// and manufacturer-data hint. Created on demand from the home menu.
lv_obj_t* ui_ble_create(void);

// Visibility gate for the refresh timer.
void ui_ble_set_visible(bool visible);
