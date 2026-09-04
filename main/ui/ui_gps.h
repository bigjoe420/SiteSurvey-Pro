#pragma once

#include "lvgl.h"
#include "gps.h"

// Post latest GPS state to the screen (thread-safe, called from late_init_task).
void ui_gps_post_gps(const GpsState* state);

// Lazy-create the GPS status screen.
lv_obj_t* ui_gps_create(void);

// Pause/resume the refresh timer when the screen is hidden/shown.
void ui_gps_set_visible(bool visible);
