#include "ui_gps.h"

#include <cstdio>
#include <cstdlib>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "ui_home.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static GpsState s_latest;
static bool s_has_gps;
static bool s_gps_visible;

static lv_obj_t* s_fix_indicator;
static lv_obj_t* s_fix_label;
static lv_obj_t* s_lat_label;
static lv_obj_t* s_lon_label;
static lv_obj_t* s_sats_label;
static lv_obj_t* s_utc_label;
static lv_obj_t* s_nmea_label;
static lv_timer_t* s_timer;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void fmt_coord(char* buf, size_t n, int32_t e7, bool is_lat)
{
    char dir = is_lat ? (e7 >= 0 ? 'N' : 'S') : (e7 >= 0 ? 'E' : 'W');
    int32_t abs_e7 = e7 >= 0 ? e7 : -e7;
    int32_t deg = abs_e7 / 10000000;
    int32_t min_int = (abs_e7 % 10000000) * 60 / 10000000;
    int32_t min_frac = ((abs_e7 % 10000000) * 60 % 10000000) * 100 / 10000000;
    snprintf(buf, n, "%ld° %02ld.%02ld' %c", (long)deg, (long)min_int, (long)min_frac, dir);
}

static void fmt_utc(char* buf, size_t n, uint32_t hhmmss)
{
    uint32_t h = hhmmss / 10000;
    uint32_t m = (hhmmss / 100) % 100;
    uint32_t s = hhmmss % 100;
    snprintf(buf, n, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
}

static const char* fix_str(uint8_t q)
{
    switch (q) {
        case 0:  return "NO FIX";
        case 1:  return "GPS FIX";
        case 2:  return "DGPS FIX";
        case 3:  return "PPS FIX";
        case 4:  return "RTK FIX";
        case 5:  return "RTK FLOAT";
        case 6:  return "ESTIMATED";
        case 7:  return "MANUAL";
        case 8:  return "SIMULATION";
        default: return "UNKNOWN";
    }
}

static lv_color_t fix_color(uint8_t q)
{
    switch (q) {
        case 0:  return lv_color_hex(0xF44336); // red — no fix
        case 1:  return lv_color_hex(0x4CAF50); // green — GPS
        case 2:  return lv_color_hex(0x8BC34A); // light green — DGPS
        case 3:  return lv_color_hex(0x00E676); // bright green — PPS
        case 4:
        case 5:  return lv_color_hex(0x00BCD4); // cyan — RTK
        default: return lv_color_hex(0xFFC107); // yellow — other
    }
}

// ---------------------------------------------------------------------------
// Refresh timer
// ---------------------------------------------------------------------------

static void gps_refresh(lv_timer_t*)
{
    if (!s_gps_visible) return;

    taskENTER_CRITICAL(&s_mux);
    GpsState state = s_latest;
    bool have = s_has_gps;
    taskEXIT_CRITICAL(&s_mux);

    if (!have) {
        lv_label_set_text(s_fix_label, "WAITING");
        lv_obj_set_style_text_color(s_fix_label, lv_color_hex(0xFFC107), 0);
        lv_obj_set_style_bg_color(s_fix_indicator, lv_color_hex(0xFFC107), 0);
        lv_label_set_text(s_lat_label, "Lat: --");
        lv_label_set_text(s_lon_label, "Lon: --");
        lv_label_set_text(s_sats_label, "Sats: --");
        lv_label_set_text(s_utc_label,  "UTC: --");
        lv_label_set_text(s_nmea_label, "NMEA: --");
        return;
    }

    lv_label_set_text(s_fix_label, fix_str(state.fix_quality));
    lv_obj_set_style_text_color(s_fix_label, fix_color(state.fix_quality), 0);
    lv_obj_set_style_bg_color(s_fix_indicator, fix_color(state.fix_quality), 0);

    if (state.fix_valid) {
        char buf[32];
        fmt_coord(buf, sizeof(buf), state.lat_e7, true);
        lv_label_set_text_fmt(s_lat_label, "Lat: %s", buf);
        fmt_coord(buf, sizeof(buf), state.lon_e7, false);
        lv_label_set_text_fmt(s_lon_label, "Lon: %s", buf);
    } else {
        lv_label_set_text(s_lat_label, "Lat: --");
        lv_label_set_text(s_lon_label, "Lon: --");
    }

    lv_label_set_text_fmt(s_sats_label, "Sats: %u", (unsigned)state.sats);

    char utc_buf[16];
    fmt_utc(utc_buf, sizeof(utc_buf), state.utc_hhmmss);
    lv_label_set_text_fmt(s_utc_label, "UTC: %s", utc_buf);

    lv_label_set_text_fmt(s_nmea_label, "NMEA ok=%lu bad=%lu rx=%lu",
                          (unsigned long)state.sentences_ok,
                          (unsigned long)state.sentences_bad,
                          (unsigned long)state.rx_bytes);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

static void back_cb(lv_event_t*)
{
    ui_home_load();
}

// ---------------------------------------------------------------------------
// Screen creation
// ---------------------------------------------------------------------------

lv_obj_t* ui_gps_create(void)
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Back button — same spec as all other screens (2026-08-31 geometry fix)
    lv_obj_t* back = lv_btn_create(scr);
    lv_obj_set_size(back, 80, 32);
    lv_obj_set_pos(back, 4, 4);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(back, 3, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_ext_click_area(back, 32);

    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "<");
    lv_obj_center(back_lbl);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "GPS STATUS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8E8E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // Fix quality indicator (colored circle)
    s_fix_indicator = lv_obj_create(scr);
    lv_obj_remove_flag(s_fix_indicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_fix_indicator, 16, 16);
    lv_obj_set_pos(s_fix_indicator, 24, 56);
    lv_obj_set_style_bg_color(s_fix_indicator, lv_color_hex(0x757575), 0);
    lv_obj_set_style_bg_opa(s_fix_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_fix_indicator, 0, 0);
    lv_obj_set_style_radius(s_fix_indicator, LV_RADIUS_CIRCLE, 0);

    s_fix_label = lv_label_create(scr);
    lv_label_set_text(s_fix_label, "WAITING");
    lv_obj_set_style_text_font(s_fix_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_fix_label, lv_color_hex(0xFFC107), 0);
    lv_obj_set_pos(s_fix_label, 48, 52);

    // Lat / Lon
    s_lat_label = lv_label_create(scr);
    lv_label_set_text(s_lat_label, "Lat: --");
    lv_obj_set_style_text_font(s_lat_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lat_label, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_pos(s_lat_label, 24, 88);

    s_lon_label = lv_label_create(scr);
    lv_label_set_text(s_lon_label, "Lon: --");
    lv_obj_set_style_text_font(s_lon_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lon_label, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_pos(s_lon_label, 24, 112);

    // Sats + UTC
    s_sats_label = lv_label_create(scr);
    lv_label_set_text(s_sats_label, "Sats: --");
    lv_obj_set_style_text_font(s_sats_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_sats_label, lv_color_hex(0xB0B0B0), 0);
    lv_obj_set_pos(s_sats_label, 24, 144);

    s_utc_label = lv_label_create(scr);
    lv_label_set_text(s_utc_label, "UTC: --");
    lv_obj_set_style_text_font(s_utc_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_utc_label, lv_color_hex(0xB0B0B0), 0);
    lv_obj_set_pos(s_utc_label, 24, 168);

    // NMEA diagnostics (small, bottom)
    s_nmea_label = lv_label_create(scr);
    lv_label_set_text(s_nmea_label, "NMEA: --");
    lv_obj_set_style_text_font(s_nmea_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_nmea_label, lv_color_hex(0x757575), 0);
    lv_obj_set_pos(s_nmea_label, 24, 200);

    s_timer = lv_timer_create(gps_refresh, 1000, nullptr);
    return scr;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ui_gps_post_gps(const GpsState* state)
{
    taskENTER_CRITICAL(&s_mux);
    s_latest = *state;
    s_has_gps = true;
    taskEXIT_CRITICAL(&s_mux);
}

void ui_gps_set_visible(bool visible)
{
    s_gps_visible = visible;
    if (s_timer) {
        if (visible) lv_timer_resume(s_timer);
        else         lv_timer_pause(s_timer);
    }
}
