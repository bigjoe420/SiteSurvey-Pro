#pragma once

#include "lvgl.h"

// Channel spectrum / occupancy screen: per-channel RSSI bars for 2.4 GHz
// and 5 GHz, colored by signal tier. Created on demand from the home menu.
lv_obj_t* ui_spectrum_create(void);

// Visibility gate for the refresh timer.
void ui_spectrum_set_visible(bool visible);
