#include "ui_hello.h"

#include <cstdio>
#include <cstdlib>

// Latest env snapshot, posted from main_task. LVGL objects are
// touched only in ui_task context, so the snapshot crosses tasks through this
// spinlock-guarded copy and is rendered by an lv_timer (which runs in ui_task).
static portMUX_TYPE s_env_mux = portMUX_INITIALIZER_UNLOCKED;
static EnvSnapshot_t s_latest;
static bool s_has_snapshot;
static bool s_bme680_present;

static lv_obj_t* s_coord_label;
static lv_obj_t* s_env_status;
static lv_obj_t* s_env_data;

void ui_hello_post_env(const EnvSnapshot_t* snap, bool bme680_present)
{
    taskENTER_CRITICAL(&s_env_mux);
    s_latest = *snap;
    s_has_snapshot = true;
    s_bme680_present = bme680_present;
    taskEXIT_CRITICAL(&s_env_mux);
}

static void env_refresh(lv_timer_t*)
{
    taskENTER_CRITICAL(&s_env_mux);
    EnvSnapshot_t snap = s_latest;
    bool have = s_has_snapshot;
    bool present = s_bme680_present;
    taskEXIT_CRITICAL(&s_env_mux);

    if (!present) {
        lv_label_set_text(s_env_status, "BME680: offline (CN1)");
        lv_obj_set_style_text_color(s_env_status, lv_palette_main(LV_PALETTE_RED), 0);
        lv_label_set_text(s_env_data, "--.-- F    --.- %RH\n------- Pa    ------ ohm");
        return;
    }
    if (!have || !snap.env_valid) {
        lv_label_set_text(s_env_status, "BME680: waiting for first sample...");
        lv_obj_set_style_text_color(s_env_status, lv_palette_main(LV_PALETTE_AMBER), 0);
        lv_label_set_text(s_env_data, "--.-- F    --.- %RH\n------- Pa    ------ ohm");
        return;
    }

    int32_t f = snap.env.temp_c_x100 * 9 / 5 + 3200;  // centi-Fahrenheit
    char buf[96];
    snprintf(buf, sizeof(buf), "%ld.%02ld F    %lu.%lu %%RH\n%lu Pa    %lu ohm",
             (long)(f / 100), (long)(labs(f) % 100),
             (unsigned long)(snap.env.hum_x100 / 100),
             (unsigned long)((snap.env.hum_x100 / 10) % 10),
             (unsigned long)snap.env.press_pa, (unsigned long)snap.env.gas_ohm);
    lv_label_set_text(s_env_status, "BME680: live");
    lv_obj_set_style_text_color(s_env_status, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_label_set_text(s_env_data, buf);
}

static void on_touch(lv_event_t* e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    char buf[32];
    snprintf(buf, sizeof(buf), "touch: %d, %d", (int)p.x, (int)p.y);
    lv_label_set_text(s_coord_label, buf);
}

lv_obj_t* ui_hello_create(void)
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "SiteSurvey Pro");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Env readout: refreshed by env_refresh from the
    // spinlock-guarded snapshot; ASCII only (default fonts lack degree/ohm)
    s_env_status = lv_label_create(scr);
    lv_label_set_text(s_env_status, "BME680: probing...");
    lv_obj_set_style_text_color(s_env_status, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_align(s_env_status, LV_ALIGN_TOP_MID, 0, 44);

    s_env_data = lv_label_create(scr);
    lv_label_set_text(s_env_data, "--.-- F    --.- %RH\n------- Pa    ------ ohm");
    lv_obj_set_style_text_color(s_env_data, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_env_data, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_env_data, LV_ALIGN_TOP_MID, 0, 66);

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "UI pipeline OK - tap the screen");
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -44);

    s_coord_label = lv_label_create(scr);
    lv_label_set_text(s_coord_label, "touch: —");
    lv_obj_set_style_text_color(s_coord_label, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(s_coord_label, LV_ALIGN_BOTTOM_MID, 0, -18);

    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_PRESSING, nullptr);

    // 500 ms refresh is plenty for a 5 s sensor cadence and catches the
    // present->first-sample transition promptly after the splash fades
    lv_timer_create(env_refresh, 500, nullptr);
    return scr;
}
