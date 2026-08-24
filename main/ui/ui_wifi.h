#pragma once

#include "lvgl.h"

// Live Wi-Fi scan list screen: strongest-first AP rows with animated,
// severity-colored RSSI bars. Created on demand when the user taps the
// Wi-Fi button on the home menu.
lv_obj_t* ui_wifi_create(void);

// Visibility gate for the refresh timer. Called when the screen is loaded
// or unloaded so hidden screens cost zero render churn.
void ui_wifi_set_visible(bool visible);
