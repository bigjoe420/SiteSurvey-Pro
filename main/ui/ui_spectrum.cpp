#include "ui_spectrum.h"

#include <cstdio>
#include <cstring>
#include "scan_engine.h"
#include "ui_home.h"

// -----------------------------------------------------------------------------
// Channel maps
// -----------------------------------------------------------------------------

static const uint8_t CH_2G[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t CH_5G[] = {
    36, 40, 44, 48, 52, 56, 60, 64,
    149, 153, 157, 161, 165
};
static constexpr int N_2G = sizeof(CH_2G) / sizeof(CH_2G[0]);
static constexpr int N_5G = sizeof(CH_5G) / sizeof(CH_5G[0]);

// -----------------------------------------------------------------------------
// Layout constants
// -----------------------------------------------------------------------------

#define SCR_W       320
#define TRACK_H     44
#define TRACK_Y_2G  72
#define TRACK_Y_5G  154

// Bars span the full screen width; each channel gets an equal slot.
#define SLOT_W_2G   (SCR_W / N_2G)   // 29
#define SLOT_W_5G   (SCR_W / N_5G)   // 24
#define BAR_W_2G    20
#define BAR_W_5G    16

// -----------------------------------------------------------------------------
// Tier colours (match ui_wifi.cpp)
// -----------------------------------------------------------------------------

static const lv_color_t TIER_COLORS[] = {
    lv_color_hex(0x4CAF50), // Strong   >= -50
    lv_color_hex(0xFFEB3B), // Moderate -50..-70
    lv_color_hex(0xFF9800), // Weak     -70..-85
    lv_color_hex(0xF44336), // Marginal < -85
};

static lv_color_t tier_color(int8_t rssi)
{
    if (rssi >= SSP_RSSI_STRONG_DBM)   return TIER_COLORS[0];
    if (rssi >= SSP_RSSI_MODERATE_DBM) return TIER_COLORS[1];
    if (rssi >= SSP_RSSI_WEAK_DBM)     return TIER_COLORS[2];
    return TIER_COLORS[3];
}

// -----------------------------------------------------------------------------
// Widget state
// -----------------------------------------------------------------------------

struct ChBar {
    lv_obj_t* bar;
    lv_obj_t* label;
    uint8_t   channel;
    int8_t    shown_rssi;   // -128 = never shown
    bool      was_active;
};

static ChBar s_2g[N_2G];
static ChBar s_5g[N_5G];
static lv_obj_t* s_total_lbl;
static bool      s_visible;
static char      s_footer[48];

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static int ch_index_2g(uint8_t ch)
{
    for (int i = 0; i < N_2G; i++) if (CH_2G[i] == ch) return i;
    return -1;
}

static int ch_index_5g(uint8_t ch)
{
    for (int i = 0; i < N_5G; i++) if (CH_5G[i] == ch) return i;
    return -1;
}

static void build_bar_row(lv_obj_t* scr, ChBar* bars, int n,
                          const uint8_t* channels, int slot_w, int bar_w,
                          int track_y, bool sparse_labels)
{
    // Track background
    lv_obj_t* track = lv_obj_create(scr);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(track, SCR_W, TRACK_H);
    lv_obj_set_pos(track, 0, track_y);
    lv_obj_set_style_bg_color(track, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_set_style_radius(track, 0, 0);

    for (int i = 0; i < n; i++) {
        int x = i * slot_w + (slot_w - bar_w) / 2;

        lv_obj_t* b = lv_bar_create(track);
        lv_bar_set_range(b, -100, -25);
        lv_obj_set_size(b, bar_w, TRACK_H);
        lv_obj_set_pos(b, x, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 1, LV_PART_MAIN);
        // Indicator starts invisible (value at minimum)
        lv_bar_set_value(b, -100, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x333333), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(b, 1, LV_PART_INDICATOR);

        bars[i].bar = b;
        bars[i].channel = channels[i];
        bars[i].shown_rssi = -128;
        bars[i].was_active = false;

        // Channel label — centred under the bar
        if (!sparse_labels || (i % 2 == 0) || i == n - 1) {
            lv_obj_t* lbl = lv_label_create(scr);
            char buf[4];
            snprintf(buf, sizeof(buf), "%u", channels[i]);
            lv_label_set_text(lbl, buf);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x9E9E9E), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            // Approximate centre; LVGL will centre the text on the label's x
            lv_obj_set_pos(lbl, i * slot_w + slot_w / 2 - 8, track_y + TRACK_H + 2);
            bars[i].label = lbl;
        } else {
            bars[i].label = nullptr;
        }
    }
}

// -----------------------------------------------------------------------------
// Refresh
// -----------------------------------------------------------------------------

static void do_refresh(void)
{
    if (!s_visible) return;

    ScanResult_t aps[64];
    int n = scan_engine_snapshot(aps, 64);

    // Aggregate per channel
    int8_t  max_rssi_2g[N_2G];
    int8_t  max_rssi_5g[N_5G];
    int     count_2g[N_2G];
    int     count_5g[N_5G];
    bool    active_2g[N_2G];
    bool    active_5g[N_5G];

    for (int i = 0; i < N_2G; i++) {
        max_rssi_2g[i] = -128;
        count_2g[i] = 0;
        active_2g[i] = false;
    }
    for (int i = 0; i < N_5G; i++) {
        max_rssi_5g[i] = -128;
        count_5g[i] = 0;
        active_5g[i] = false;
    }

    for (int i = 0; i < n; i++) {
        const ScanResult_t* ap = &aps[i];
        if (ap->channel <= 14) {
            int idx = ch_index_2g(ap->channel);
            if (idx >= 0) {
                active_2g[idx] = true;
                count_2g[idx]++;
                if (ap->rssi > max_rssi_2g[idx]) max_rssi_2g[idx] = ap->rssi;
            }
        } else {
            int idx = ch_index_5g(ap->channel);
            if (idx >= 0) {
                active_5g[idx] = true;
                count_5g[idx]++;
                if (ap->rssi > max_rssi_5g[idx]) max_rssi_5g[idx] = ap->rssi;
            }
        }
    }

    // Update 2.4G bars — change-guarded, no animation (Corollary 11)
    for (int i = 0; i < N_2G; i++) {
        ChBar* cb = &s_2g[i];
        if (active_2g[i]) {
            if (!cb->was_active || max_rssi_2g[i] != cb->shown_rssi) {
                cb->was_active = true;
                cb->shown_rssi = max_rssi_2g[i];
                lv_bar_set_value(cb->bar, max_rssi_2g[i], LV_ANIM_OFF);
                lv_obj_set_style_bg_color(cb->bar, tier_color(max_rssi_2g[i]), LV_PART_INDICATOR);
            }
        } else {
            if (cb->was_active) {
                cb->was_active = false;
                cb->shown_rssi = -128;
                lv_bar_set_value(cb->bar, -100, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(cb->bar, lv_color_hex(0x333333), LV_PART_INDICATOR);
            }
        }
    }

    // Update 5G bars — change-guarded, no animation
    for (int i = 0; i < N_5G; i++) {
        ChBar* cb = &s_5g[i];
        if (active_5g[i]) {
            if (!cb->was_active || max_rssi_5g[i] != cb->shown_rssi) {
                cb->was_active = true;
                cb->shown_rssi = max_rssi_5g[i];
                lv_bar_set_value(cb->bar, max_rssi_5g[i], LV_ANIM_OFF);
                lv_obj_set_style_bg_color(cb->bar, tier_color(max_rssi_5g[i]), LV_PART_INDICATOR);
            }
        } else {
            if (cb->was_active) {
                cb->was_active = false;
                cb->shown_rssi = -128;
                lv_bar_set_value(cb->bar, -100, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(cb->bar, lv_color_hex(0x333333), LV_PART_INDICATOR);
            }
        }
    }

    // Footer: total AP count + busiest channel summary
    int total = 0, busiest = -1, busiest_cnt = 0;
    bool is_5g_busiest = false;
    for (int i = 0; i < N_2G; i++) {
        total += count_2g[i];
        if (count_2g[i] > busiest_cnt) {
            busiest_cnt = count_2g[i];
            busiest = CH_2G[i];
            is_5g_busiest = false;
        }
    }
    for (int i = 0; i < N_5G; i++) {
        total += count_5g[i];
        if (count_5g[i] > busiest_cnt) {
            busiest_cnt = count_5g[i];
            busiest = CH_5G[i];
            is_5g_busiest = true;
        }
    }

    // Only rewrite label if text changed
    char buf[48];
    if (busiest >= 0) {
        snprintf(buf, sizeof(buf), "%d APs  |  Busiest: Ch %d (%s) x%d",
                 total, busiest, is_5g_busiest ? "5G" : "2.4G", busiest_cnt);
    } else {
        snprintf(buf, sizeof(buf), "Scanning...");
    }
    if (strcmp(buf, s_footer) != 0) {
        strcpy(s_footer, buf);
        lv_label_set_text(s_total_lbl, buf);
    }
}

static void refresh(lv_timer_t*)
{
    do_refresh();
}

// -----------------------------------------------------------------------------
// Screen construction
// -----------------------------------------------------------------------------

static void do_back(lv_timer_t *t){ lv_timer_del(t); ui_home_load(); } static void back_cb(lv_event_t*)
{
    lv_timer_create(do_back, 1, nullptr);
}

lv_obj_t* ui_spectrum_create(void)
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Back button
    lv_obj_t* back = lv_btn_create(scr);
    lv_obj_set_size(back, 80, 40);
    lv_obj_set_pos(back, 4, 4);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(back, 3, 0);
    lv_obj_add_flag(back, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(back, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_ext_click_area(back, 16);

    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "<");
    lv_obj_center(back_lbl);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "CHANNEL SPECTRUM");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 20, 12);

    // 2.4 GHz label
    lv_obj_t* lbl_2g = lv_label_create(scr);
    lv_label_set_text(lbl_2g, "2.4 GHz");
    lv_obj_set_style_text_color(lbl_2g, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_pos(lbl_2g, 8, 54);

    build_bar_row(scr, s_2g, N_2G, CH_2G, SLOT_W_2G, BAR_W_2G, TRACK_Y_2G, false);

    // 5 GHz label
    lv_obj_t* lbl_5g = lv_label_create(scr);
    lv_label_set_text(lbl_5g, "5 GHz");
    lv_obj_set_style_text_color(lbl_5g, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_pos(lbl_5g, 8, 136);

    build_bar_row(scr, s_5g, N_5G, CH_5G, SLOT_W_5G, BAR_W_5G, TRACK_Y_5G, true);

    // Footer
    s_total_lbl = lv_label_create(scr);
    lv_label_set_text(s_total_lbl, "Scanning...");
    lv_obj_set_style_text_color(s_total_lbl, lv_color_hex(0x9E9E9E), 0);
    lv_obj_set_pos(s_total_lbl, 8, 210);
    s_footer[0] = '\0';

    lv_timer_create(refresh, 5000, nullptr);
    return scr;
}

void ui_spectrum_set_visible(bool visible)
{
    s_visible = visible;
    if (visible) do_refresh();
}
