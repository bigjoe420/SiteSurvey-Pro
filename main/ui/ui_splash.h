#pragma once

#include "lvgl.h"

// Builds the boot splash (waterfall bars + title + pulsing status) on its own
// screen and loads it. When both readiness gates are satisfied, `done_cb` is
// called to create the home screen; the splash fades to it with auto-delete.
// Call right after lvgl_port_init() and lvgl_port_start_ui_task().
typedef lv_obj_t* (*splash_done_cb_t)(void);
void ui_splash_show(splash_done_cb_t done_cb);

// Gate setters — main_task context, flag stores only (no LVGL calls).
// The splash dismisses when BOTH are set; GPS lock is deliberately not a gate.
void ui_splash_notify_scan_ready(void);
void ui_splash_notify_env_ready(void);
