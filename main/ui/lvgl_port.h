#pragma once

#include "esp_err.h"
#include "lvgl.h"

// Brings up display + touch + LVGL core. Draw buffers are allocated in PSRAM.
esp_err_t lvgl_port_init(void);

// Starts ui_task (4 KB stack): owns every LVGL call from this point on.
// Call after all screens are created.
void lvgl_port_start_ui_task(void);
