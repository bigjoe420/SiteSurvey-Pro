#include "ui_alerts.h"

#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "ui_home.h"
#include "alert_engine.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static bool s_visible;
static lv_obj_t* s_rows[8];
static lv_obj_t* s_empty_label;
static lv_timer_t* s_timer;

static const char* match_char(AlertMatchType_t t)
{
    switch (t) {
        case ALERT_MATCH_SSID: return "S";
        case ALERT_MATCH_BSSID: return "B";
        case ALERT_MATCH_RSSI: return "R";
        default: return "?";
    }
}

static void refresh(lv_timer_t*)
{
    if (!s_visible) return;

    AlertEntry_t entries[8];
    int n = alert_log_snapshot(entries, 8);

    if (n == 0) {
        lv_obj_clear_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 8; i++) {
            lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    lv_obj_add_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 8; i++) {
        if (i < n) {
            lv_obj_clear_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
            // row contains 3 labels: ts, target, rssi+type
            lv_obj_t* row = s_rows[i];
            lv_obj_t* ts_lbl = lv_obj_get_child(row, 0);
            lv_obj_t* tgt_lbl = lv_obj_get_child(row, 1);
            lv_obj_t* sig_lbl = lv_obj_get_child(row, 2);

            // Extract HH:MM:SS from YYYY-MM-DD HH:MM:SS
            const char* ts = entries[i].timestamp;
            const char* time_only = (strlen(ts) >= 19) ? ts + 11 : ts;

            lv_label_set_text(ts_lbl, time_only);
            lv_label_set_text(tgt_lbl, entries[i].target);

            char sig[16];
            snprintf(sig, sizeof(sig), "%d %s", entries[i].rssi, match_char(entries[i].match_type));
            lv_label_set_text(sig_lbl, sig);

            // Color-code by match type
            lv_color_t row_color = lv_color_hex(0xE8E8E8);
            if (entries[i].match_type == ALERT_MATCH_RSSI) row_color = lv_color_hex(0xFFC107);
            lv_obj_set_style_text_color(tgt_lbl, row_color, 0);
        } else {
            lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void back_cb(lv_event_t*)
{
    ui_home_load();
}

static lv_obj_t* make_row(lv_obj_t* parent, int y)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, 304, 24);
    lv_obj_set_pos(row, 8, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 2, 0);
    lv_obj_set_style_pad_all(row, 2, 0);

    lv_obj_t* ts = lv_label_create(row);
    lv_label_set_text(ts, "--:--:--");
    lv_obj_set_style_text_font(ts, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ts, lv_color_hex(0x909090), 0);
    lv_obj_set_pos(ts, 4, 2);

    lv_obj_t* tgt = lv_label_create(row);
    lv_label_set_text(tgt, "---");
    lv_obj_set_style_text_font(tgt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tgt, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_pos(tgt, 64, 2);

    lv_obj_t* sig = lv_label_create(row);
    lv_label_set_text(sig, "---");
    lv_obj_set_style_text_font(sig, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sig, lv_color_hex(0xB0B0B0), 0);
    lv_obj_align(sig, LV_ALIGN_RIGHT_MID, -4, 0);

    return row;
}

lv_obj_t* ui_alerts_create(void)
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Back button
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
    lv_label_set_text(title, "ALERT LOG");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8E8E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // Empty state label
    s_empty_label = lv_label_create(scr);
    lv_label_set_text(s_empty_label, "No alerts yet");
    lv_obj_set_style_text_color(s_empty_label, lv_color_hex(0x757575), 0);
    lv_obj_center(s_empty_label);

    // Alert rows (newest first)
    const int ROW_Y0 = 44;
    const int ROW_H = 26;
    for (int i = 0; i < 8; i++) {
        s_rows[i] = make_row(scr, ROW_Y0 + i * ROW_H);
        lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_timer = lv_timer_create(refresh, 1000, nullptr);
    return scr;
}

void ui_alerts_set_visible(bool visible)
{
    s_visible = visible;
    if (s_timer) {
        if (visible) lv_timer_resume(s_timer);
        else         lv_timer_pause(s_timer);
    }
}
