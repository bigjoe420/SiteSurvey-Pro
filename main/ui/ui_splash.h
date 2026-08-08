#pragma once

#include "lvgl.h"

// Builds the boot splash (waterfall bars + title + pulsing status) on its own
// screen and loads it. `next` is the screen the splash fades to once both
// readiness gates are satisfied. Call right after lvgl_port_init().
void ui_splash_show(lv_obj_t* next);

// Gate setters — main_task context, flag stores only (no LVGL calls).
// The splash dismisses when BOTH are set; GPS lock is deliberately not a gate.
void ui_splash_notify_scan_ready(void);
void ui_splash_notify_env_ready(void);
