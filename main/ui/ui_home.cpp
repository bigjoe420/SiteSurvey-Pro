#include "ui_home.h"

#include "ui_wifi.h"
#include "ui_env.h"
#include "ui_spectrum.h"

static lv_obj_t* s_home;
static lv_obj_t* s_wifi_scr;
static lv_obj_t* s_env_scr;
static lv_obj_t* s_spectrum_scr;

static void wifi_btn_cb(lv_event_t*)
{
    if (!s_wifi_scr) s_wifi_scr = ui_wifi_create();
    lv_screen_load(s_wifi_scr);
    ui_wifi_set_visible(true);
    ui_env_set_visible(false);
    ui_spectrum_set_visible(false);
}

static void spectrum_btn_cb(lv_event_t*)
{
    if (!s_spectrum_scr) s_spectrum_scr = ui_spectrum_create();
    lv_screen_load(s_spectrum_scr);
    ui_spectrum_set_visible(true);
    ui_wifi_set_visible(false);
    ui_env_set_visible(false);
}

static void env_btn_cb(lv_event_t*)
{
    if (!s_env_scr) s_env_scr = ui_env_create();
    lv_screen_load(s_env_scr);
    ui_env_set_visible(true);
    ui_wifi_set_visible(false);
    ui_spectrum_set_visible(false);
}

lv_obj_t* ui_home_create(void)
{
    s_home = lv_obj_create(nullptr);
    lv_obj_remove_flag(s_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_home, lv_color_black(), 0);

    lv_obj_t* title = lv_label_create(s_home);
    lv_label_set_text(title, "SiteSurvey Pro");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t* wifi_btn = lv_btn_create(s_home);
    lv_obj_set_size(wifi_btn, 200, 56);
    lv_obj_set_pos(wifi_btn, 60, 52);
    lv_obj_set_style_bg_color(wifi_btn, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_radius(wifi_btn, 4, 0);
    lv_obj_add_event_cb(wifi_btn, wifi_btn_cb, LV_EVENT_PRESSED, nullptr);

    lv_obj_t* wifi_lbl = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_lbl, "Wi-Fi SCAN");
    lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(wifi_lbl);

    lv_obj_t* spectrum_btn = lv_btn_create(s_home);
    lv_obj_set_size(spectrum_btn, 200, 56);
    lv_obj_set_pos(spectrum_btn, 60, 116);
    lv_obj_set_style_bg_color(spectrum_btn, lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_radius(spectrum_btn, 4, 0);
    lv_obj_add_event_cb(spectrum_btn, spectrum_btn_cb, LV_EVENT_PRESSED, nullptr);

    lv_obj_t* spectrum_lbl = lv_label_create(spectrum_btn);
    lv_label_set_text(spectrum_lbl, "SPECTRUM");
    lv_obj_set_style_text_font(spectrum_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(spectrum_lbl);

    lv_obj_t* env_btn = lv_btn_create(s_home);
    lv_obj_set_size(env_btn, 200, 56);
    lv_obj_set_pos(env_btn, 60, 180);
    lv_obj_set_style_bg_color(env_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_radius(env_btn, 4, 0);
    lv_obj_add_event_cb(env_btn, env_btn_cb, LV_EVENT_PRESSED, nullptr);

    lv_obj_t* env_lbl = lv_label_create(env_btn);
    lv_label_set_text(env_lbl, "ENVIRONMENT");
    lv_obj_set_style_text_font(env_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(env_lbl);

    return s_home;
}

void ui_home_load(void)
{
    if (!s_home) s_home = ui_home_create();
    lv_screen_load(s_home);
    ui_wifi_set_visible(false);
    ui_env_set_visible(false);
    ui_spectrum_set_visible(false);
}
