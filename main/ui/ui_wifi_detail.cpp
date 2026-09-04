#include "ui_wifi_detail.h"

#include <cstdio>
#include <cstring>
#include "scan_engine.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static lv_obj_t* s_scr = nullptr;
static lv_obj_t* s_chart = nullptr;
static lv_chart_series_t* s_series = nullptr;
static lv_obj_t* s_title = nullptr;
static lv_obj_t* s_info = nullptr;
static lv_timer_t* s_timer = nullptr;
static bool s_visible = false;
static uint8_t s_bssid[6];

// ROADMAP §5 tiers: Strong green, Moderate yellow, Weak orange, Marginal red
static const lv_color_t TIER_COLORS[] = {
    lv_color_hex(0x4CAF50), lv_color_hex(0xFFEB3B),
    lv_color_hex(0xFF9800), lv_color_hex(0xF44336),
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void populate_chart(void)
{
    if (!s_chart || !s_series) return;

    int8_t samples[RSSI_HISTORY_LEN];
    int n = scan_engine_get_history(s_bssid, samples, RSSI_HISTORY_LEN);

    if (n > 0) {
        lv_chart_set_point_count(s_chart, (uint32_t)n);
        for (int i = 0; i < n; i++) {
            lv_chart_set_series_value_by_id(s_chart, s_series, (uint32_t)i, (int32_t)samples[i]);
        }
    } else {
        // No history yet — show a flat line at floor
        lv_chart_set_point_count(s_chart, 1);
        lv_chart_set_series_value_by_id(s_chart, s_series, 0, (int32_t)(-100));
    }
    lv_chart_refresh(s_chart);
}

static void detail_refresh(lv_timer_t*)
{
    if (!s_visible) return;
    populate_chart();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t* ui_wifi_detail_create(const WifiApInfo_t* info, lv_event_cb_t back_cb)
{
    memcpy(s_bssid, info->bssid, 6);

    s_scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);

    // Back button — same spec as all other screens
    lv_obj_t* back = lv_btn_create(s_scr);
    lv_obj_set_size(back, 80, 32);
    lv_obj_set_pos(back, 4, 4);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(back, 3, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_ext_click_area(back, 32);

    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "<");
    lv_obj_center(back_lbl);

    // Title — AP SSID
    s_title = lv_label_create(s_scr);
    lv_label_set_text(s_title, info->ssid);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 12);

    // RSSI history chart
    s_chart = lv_chart_create(s_scr);
    lv_obj_set_size(s_chart, 280, 130);
    lv_obj_set_pos(s_chart, 20, 44);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, -100, -25);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);

    // Series — colour reflects current severity tier
    lv_color_t c = TIER_COLORS[info->severity];
    s_series = lv_chart_add_series(s_chart, c, LV_CHART_AXIS_PRIMARY_Y);

    populate_chart();

    // Info label — channel, current RSSI, auth mode
    s_info = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_info, lv_color_hex(0xB0B0B0), 0);
    lv_obj_align(s_info, LV_ALIGN_BOTTOM_MID, 0, -8);

    static char info_buf[64];
    snprintf(info_buf, sizeof(info_buf), "Ch %u  |  %d dBm  |  %s",
             info->channel, info->rssi, scan_engine_auth_str(info->authmode));
    lv_label_set_text(s_info, info_buf);

    // Refresh timer — every 2 s to pull new history samples
    s_timer = lv_timer_create(detail_refresh, 2000, nullptr);

    return s_scr;
}

void ui_wifi_detail_update(const WifiApInfo_t* info)
{
    if (!s_scr) return;

    memcpy(s_bssid, info->bssid, 6);

    lv_label_set_text(s_title, info->ssid);

    // Change series colour to match new AP's severity
    if (s_series) {
        lv_color_t c = TIER_COLORS[info->severity];
        lv_chart_set_series_color(s_chart, s_series, c);
    }

    populate_chart();

    static char info_buf[64];
    snprintf(info_buf, sizeof(info_buf), "Ch %u  |  %d dBm  |  %s",
             info->channel, info->rssi, scan_engine_auth_str(info->authmode));
    lv_label_set_text(s_info, info_buf);
}

void ui_wifi_detail_set_visible(bool visible)
{
    s_visible = visible;
    if (s_timer) {
        if (visible) lv_timer_resume(s_timer);
        else         lv_timer_pause(s_timer);
    }
}
