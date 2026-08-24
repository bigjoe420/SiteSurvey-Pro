#include "ui_splash.h"

#include "esp_log.h"

#include "board_pins.h"

static const char* TAG = "ui_splash";

static constexpr int BAR_COUNT = 8;
static constexpr int BAR_W = 28;
static constexpr int BAR_GAP = 10;
static constexpr int BAR_TOP = 16;
static constexpr int BAR_MAX_H = 140;

static splash_done_cb_t s_done_cb;
static volatile bool s_scan_ready;
static volatile bool s_env_ready;
static bool s_dismissed;
static lv_obj_t* s_splash_scr;

static void bar_anim_exec(void* obj, int32_t v)
{
    lv_obj_set_height((lv_obj_t*)obj, v);
}

static void opa_anim_exec(void* obj, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}

static void gate_cb(lv_timer_t* timer)
{
    if (s_dismissed || !s_scan_ready || !s_env_ready) return;
    s_dismissed = true;
    ESP_LOGI(TAG, "gates satisfied (first AP + first BME680 read) - loading home");

    lv_obj_t* home = s_done_cb ? s_done_cb() : nullptr;
    if (home) {
        lv_screen_load(home);
        if (s_splash_scr) {
            lv_obj_delete(s_splash_scr);
            s_splash_scr = nullptr;
        }
    } else {
        ESP_LOGW(TAG, "home screen callback returned null");
    }
    lv_timer_delete(timer);
}

// Impatient finger: any tap on the splash counts both gates as satisfied
static void skip_cb(lv_event_t*)
{
    s_scan_ready = true;
    s_env_ready = true;
}

void ui_splash_show(splash_done_cb_t done_cb)
{
    s_done_cb = done_cb;

    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Waterfall: staggered hue-rotated bars dropping from the top edge
    const int row_w = BAR_COUNT * BAR_W + (BAR_COUNT - 1) * BAR_GAP;
    const int x0 = (SSP_TFT_WIDTH - row_w) / 2;
    for (int i = 0; i < BAR_COUNT; i++) {
        lv_obj_t* bar = lv_obj_create(scr);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(bar, BAR_W, 0);
        lv_obj_set_pos(bar, x0 + i * (BAR_W + BAR_GAP), BAR_TOP);
        lv_obj_set_style_bg_color(bar, lv_color_hsv_to_rgb((i * 360) / BAR_COUNT, 90, 90), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 3, 0);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, bar);
        lv_anim_set_exec_cb(&a, bar_anim_exec);
        lv_anim_set_values(&a, 0, BAR_MAX_H);
        lv_anim_set_duration(&a, 900);
        lv_anim_set_delay(&a, i * 110);
        lv_anim_set_repeat_delay(&a, 500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_start(&a);
    }

    // Title fades in over the cascade
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "SiteSurvey Pro");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_opa(title, LV_OPA_TRANSP, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -12);

    lv_anim_t ta;
    lv_anim_init(&ta);
    lv_anim_set_var(&ta, title);
    lv_anim_set_exec_cb(&ta, opa_anim_exec);
    lv_anim_set_values(&ta, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&ta, 900);
    lv_anim_set_delay(&ta, 1400);
    lv_anim_start(&ta);

    // Pulsing status line
    lv_obj_t* status = lv_label_create(scr);
    lv_label_set_text(status, "Initializing Sensors & Radio...  (tap to skip)");
    lv_obj_set_style_text_color(status, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_anim_t pa;
    lv_anim_init(&pa);
    lv_anim_set_var(&pa, status);
    lv_anim_set_exec_cb(&pa, opa_anim_exec);
    lv_anim_set_values(&pa, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_duration(&pa, 750);
    lv_anim_set_playback_duration(&pa, 750);
    lv_anim_set_repeat_count(&pa, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&pa);

    lv_obj_add_event_cb(scr, skip_cb, LV_EVENT_CLICKED, nullptr);
    s_splash_scr = scr;
    lv_screen_load(scr);
    lv_timer_create(gate_cb, 100, nullptr);
}

void ui_splash_notify_scan_ready(void)
{
    s_scan_ready = true;
}

void ui_splash_notify_env_ready(void)
{
    s_env_ready = true;
}
