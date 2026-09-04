#include "ui_home.h"

#include "ui_wifi.h"
#include "ui_ble.h"
#include "ui_env.h"
#include "ui_spectrum.h"
#include "ui_gps.h"
#include "ui_alerts.h"
#include "ui_settings.h"

static lv_obj_t* s_home;
static lv_obj_t* s_wifi_scr;
static lv_obj_t* s_env_scr;
static lv_obj_t* s_spectrum_scr;
static lv_obj_t* s_ble_scr;
static lv_obj_t* s_gps_scr;
static lv_obj_t* s_alerts_scr;
static lv_obj_t* s_settings_scr;

static TickType_t s_last_nav;
static constexpr uint32_t NAV_DEBOUNCE_MS = 150;

static bool nav_guard(void)
{
    TickType_t now = xTaskGetTickCount();
    if ((now - s_last_nav) < pdMS_TO_TICKS(NAV_DEBOUNCE_MS)) return false;
    s_last_nav = now;
    return true;
}

static void wifi_btn_cb(lv_event_t*)
{
    if (!nav_guard()) return;
    if (!s_wifi_scr) s_wifi_scr = ui_wifi_create();
    lv_screen_load(s_wifi_scr);
    ui_wifi_set_visible(true);
    ui_ble_set_visible(false);
    ui_env_set_visible(false);
    ui_spectrum_set_visible(false);
    ui_gps_set_visible(false);
    ui_alerts_set_visible(false);
    ui_settings_set_visible(false);
}

static void ble_btn_cb(lv_event_t*)
{
    if (!nav_guard()) return;
    if (!s_ble_scr) s_ble_scr = ui_ble_create();
    lv_screen_load(s_ble_scr);
    ui_ble_set_visible(true);
    ui_wifi_set_visible(false);
    ui_env_set_visible(false);
    ui_spectrum_set_visible(false);
    ui_gps_set_visible(false);
    ui_alerts_set_visible(false);
    ui_settings_set_visible(false);
}

static void spectrum_btn_cb(lv_event_t*)
{
    if (!nav_guard()) return;
    if (!s_spectrum_scr) s_spectrum_scr = ui_spectrum_create();
    lv_screen_load(s_spectrum_scr);
    ui_spectrum_set_visible(true);
    ui_wifi_set_visible(false);
    ui_ble_set_visible(false);
    ui_env_set_visible(false);
    ui_gps_set_visible(false);
    ui_alerts_set_visible(false);
    ui_settings_set_visible(false);
}

static void env_btn_cb(lv_event_t*)
{
    if (!nav_guard()) return;
    if (!s_env_scr) s_env_scr = ui_env_create();
    lv_screen_load(s_env_scr);
    ui_env_set_visible(true);
    ui_wifi_set_visible(false);
    ui_ble_set_visible(false);
    ui_spectrum_set_visible(false);
    ui_gps_set_visible(false);
    ui_alerts_set_visible(false);
    ui_settings_set_visible(false);
}

static void gps_btn_cb(lv_event_t*)
{
    if (!nav_guard()) return;
    if (!s_gps_scr) s_gps_scr = ui_gps_create();
    lv_screen_load(s_gps_scr);
    ui_gps_set_visible(true);
    ui_wifi_set_visible(false);
    ui_ble_set_visible(false);
    ui_env_set_visible(false);
    ui_spectrum_set_visible(false);
    ui_alerts_set_visible(false);
    ui_settings_set_visible(false);
}

static void alerts_btn_cb(lv_event_t*)
{
    if (!nav_guard()) return;
    if (!s_alerts_scr) s_alerts_scr = ui_alerts_create();
    lv_screen_load(s_alerts_scr);
    ui_alerts_set_visible(true);
    ui_wifi_set_visible(false);
    ui_ble_set_visible(false);
    ui_env_set_visible(false);
    ui_spectrum_set_visible(false);
    ui_gps_set_visible(false);
    ui_settings_set_visible(false);
}

static void settings_btn_cb(lv_event_t*)
{
    if (!nav_guard()) return;
    if (!s_settings_scr) s_settings_scr = ui_settings_create();
    if (!s_settings_scr) return;
    lv_screen_load(s_settings_scr);
    ui_settings_set_visible(true);
    ui_wifi_set_visible(false);
    ui_ble_set_visible(false);
    ui_env_set_visible(false);
    ui_spectrum_set_visible(false);
    ui_gps_set_visible(false);
    ui_alerts_set_visible(false);
}

// ---------------------------------------------------------------------------
// Block helper
// ---------------------------------------------------------------------------

static lv_obj_t* make_block(lv_obj_t* parent, const char* label_text,
                            lv_color_t bg, lv_event_cb_t cb, int x, int y)
{
    const int W = 144;
    const int H = 44;

    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, W, H);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    return btn;
}

lv_obj_t* ui_home_create(void)
{
    s_home = lv_obj_create(nullptr);
    lv_obj_remove_flag(s_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_home, lv_color_black(), 0);

    // Small title at top
    lv_obj_t* title = lv_label_create(s_home);
    lv_label_set_text(title, "SiteSurvey Pro");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xB0B0B0), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // 2×4 block grid — 7 blocks + 1 empty.
    // Block size 144×44, gap 16 horizontal / 8 vertical, left margin 12.
    const int X0 = 12;
    const int X1 = 172;   // 12 + 144 + 16
    const int Y0 = 24;
    const int Y1 = 76;    // 24 + 44 + 8
    const int Y2 = 128;   // 76 + 44 + 8
    const int Y3 = 180;   // 128 + 44 + 8

    make_block(s_home, "Wi-Fi",        lv_color_hex(0x4CAF50), wifi_btn_cb,     X0, Y0);
    make_block(s_home, "BLE",          lv_color_hex(0x00BCD4), ble_btn_cb,      X1, Y0);
    make_block(s_home, "SPECTRUM",     lv_color_hex(0xFF9800), spectrum_btn_cb, X0, Y1);
    make_block(s_home, "ENVIRONMENT",  lv_color_hex(0x2196F3), env_btn_cb,      X1, Y1);
    make_block(s_home, "GPS",          lv_color_hex(0x9C27B0), gps_btn_cb,      X0, Y2);
    make_block(s_home, "ALERTS",       lv_color_hex(0xF44336), alerts_btn_cb,   X1, Y2);
    make_block(s_home, "SETTINGS",     lv_color_hex(0x607D8B), settings_btn_cb, X0, Y3);

    return s_home;
}

void ui_home_load(void)
{
    if (!nav_guard()) return;
    if (!s_home) s_home = ui_home_create();
    lv_screen_load(s_home);
    ui_wifi_set_visible(false);
    ui_ble_set_visible(false);
    ui_env_set_visible(false);
    ui_spectrum_set_visible(false);
    ui_gps_set_visible(false);
    ui_alerts_set_visible(false);
    ui_settings_set_visible(false);
}
