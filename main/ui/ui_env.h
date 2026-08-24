#pragma once

#include "lvgl.h"
#include "sensors.h"

// Environmental dashboard screen: four gradient-scale gauges (temp / humidity /
// pressure / VOC) with animated markers and value tweens. Created on demand
// when the user taps the ENVIRONMENT button on the home menu.
lv_obj_t* ui_env_create(void);

// Latest env snapshot handoff. Safe from main_task context (spinlock copy);
// all LVGL work happens in ui_task via the refresh timer.
void ui_env_post_env(const EnvSnapshot_t* snap, bool bme680_present);

// Visibility gate for the refresh timer and spinner render.
void ui_env_set_visible(bool visible);
