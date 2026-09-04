#pragma once

#include "lvgl.h"

// Lazy-create the Alert Log screen.
lv_obj_t* ui_alerts_create(void);

// Pause/resume refresh timer.
void ui_alerts_set_visible(bool visible);
