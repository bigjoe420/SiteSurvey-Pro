#pragma once

#include "lvgl.h"
#include "sensors.h"

// Builds the home screen on its own screen object (NOT loaded — the boot
// splash owns the display until its gates clear). All objects are
// created once at init; the touch readout and env readout update at runtime.
lv_obj_t* ui_hello_create(void);

// Hands the latest env snapshot to the home screen. Safe to call from
// main_task (the snapshot crosses via spinlock; LVGL rendering happens in
// ui_task). `bme680_present` selects offline/live display.
void ui_hello_post_env(const EnvSnapshot_t* snap, bool bme680_present);
