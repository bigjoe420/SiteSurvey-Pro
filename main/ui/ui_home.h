#pragma once

#include "lvgl.h"

// Minimal home menu: two buttons that create Wi-Fi / ENV screens on demand.
// Created by the splash callback after gates clear; never destroyed.
lv_obj_t* ui_home_create(void);

// Load the home screen (creates it if this is the first call).
void ui_home_load(void);
