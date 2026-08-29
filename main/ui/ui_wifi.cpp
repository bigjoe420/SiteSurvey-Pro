#include "ui_wifi.h"

#include <cstdio>
#include <cstring>
#include "scan_engine.h"
#include "ui_home.h"

#define MAX_ROWS 16

typedef struct {
    lv_obj_t* row;
    lv_obj_t* bar;
    lv_obj_t* ssid;
    lv_obj_t* info;
} Row;

typedef struct {
    bool shown;
    int8_t rssi;
    char ssid[33];
    char info[48];
} RowState;

static Row        s_rows[MAX_ROWS];
static RowState   s_state[MAX_ROWS];
static lv_obj_t*  s_list;
static bool       s_wifi_visible;
static bool       s_rows_built;
static lv_timer_t* s_timer;

// Environmental overlay
static lv_obj_t*  s_env_overlay;

// Overlay change guards — only touch widgets when state/text changes
static char       s_overlay_text[48];
static int        s_overlay_state; // 0=unset, 1=offline, 2=waiting, 3=live

// Spinlock-protected env snapshot (mirrors ui_env.cpp pattern)
static portMUX_TYPE s_env_mux = portMUX_INITIALIZER_UNLOCKED;
static EnvSnapshot_t s_env_snap;
static bool s_env_has_snap;
static bool s_env_bme_present;

// ROADMAP §5 tiers: Strong green, Moderate yellow, Weak orange, Marginal red
static const lv_color_t TIER_COLORS[] = {
    lv_color_hex(0x4CAF50), lv_color_hex(0xFFEB3B),
    lv_color_hex(0xFF9800), lv_color_hex(0xF44336),
};

#define ROW_H       30
#define ROW_STRIDE  32
#define LIST_W      316
#define ROW_W       LIST_W

#define COL_BAR_W   48
#define COL_BAR_X   4
#define COL_SSID_X  56
#define COL_SSID_W  120
#define COL_INFO_X  180
#define COL_INFO_W  130

static void build_row(lv_obj_t* parent, Row* r, int idx)
{
    r->row = lv_obj_create(parent);
    lv_obj_remove_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
    // Allow drag gestures on the row to chain up to the scrollable list.
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_pos(r->row, 0, idx * ROW_STRIDE);
    lv_obj_set_size(r->row, ROW_W, ROW_H);
    lv_obj_set_style_bg_color(r->row, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(r->row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r->row, 1, 0);
    lv_obj_set_style_border_color(r->row, lv_color_hex(0x222222), 0);
    lv_obj_set_style_pad_all(r->row, 2, 0);

    r->bar = lv_bar_create(r->row);
    lv_bar_set_range(r->bar, -100, -25);
    lv_obj_set_size(r->bar, COL_BAR_W, 10);
    lv_obj_set_pos(r->bar, COL_BAR_X, (ROW_H - 10) / 2);
    lv_obj_set_style_bg_color(r->bar, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r->bar, LV_OPA_COVER, LV_PART_MAIN);

    r->ssid = lv_label_create(r->row);
    lv_obj_set_size(r->ssid, COL_SSID_W, LV_SIZE_CONTENT);
    lv_label_set_long_mode(r->ssid, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(r->ssid, lv_color_white(), 0);
    lv_obj_set_pos(r->ssid, COL_SSID_X, (ROW_H - lv_font_get_line_height(&lv_font_montserrat_14)) / 2);

    r->info = lv_label_create(r->row);
    lv_obj_set_size(r->info, COL_INFO_W, LV_SIZE_CONTENT);
    lv_label_set_long_mode(r->info, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(r->info, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_style_text_align(r->info, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(r->info, COL_INFO_X, (ROW_H - lv_font_get_line_height(&lv_font_montserrat_14)) / 2);
}

static void update_env_overlay(void)
{
    taskENTER_CRITICAL(&s_env_mux);
    EnvSnapshot_t snap = s_env_snap;
    bool have = s_env_has_snap;
    bool present = s_env_bme_present;
    taskEXIT_CRITICAL(&s_env_mux);

    int state = 0;
    const char* text = nullptr;
    lv_color_t color = lv_color_hex(0x4CAF50);

    if (!present) {
        state = 1;
        text = "BME680 OFFLINE";
        color = lv_color_hex(0xF44336);
    } else if (!have || !snap.env_valid) {
        state = 2;
        text = "ENV WAITING...";
        color = lv_color_hex(0xFFC107);
    } else {
        int32_t temp_f_x100 = snap.env.temp_c_x100 * 9 / 5 + 3200;
        int temp_whole = (int)(temp_f_x100 / 100);
        int temp_frac  = (int)(temp_f_x100 / 10 % 10);
        int hum_whole  = (int)(snap.env.hum_x100 / 100);
        int press_hpa  = (int)(snap.env.press_pa / 100);

        static char buf[48];
        snprintf(buf, sizeof(buf), "%d.%d°F  %d%%  %dhPa", temp_whole, temp_frac, hum_whole, press_hpa);
        text = buf;
        state = 3;
        color = lv_color_hex(0x4CAF50);
    }

    // Only touch widgets when state or text actually changes
    if (state != s_overlay_state) {
        s_overlay_state = state;
        lv_obj_set_style_text_color(s_env_overlay, color, 0);
    }
    if (strcmp(text, s_overlay_text) != 0) {
        strcpy(s_overlay_text, text);
        lv_label_set_text(s_env_overlay, text);
    }
}

static void do_refresh(void)
{
    if (!s_wifi_visible) return;

    static ScanResult_t aps[MAX_ROWS];
    int n = scan_engine_snapshot(aps, MAX_ROWS);

    for (int i = 0; i < MAX_ROWS; i++) {
        Row* r = &s_rows[i];
        RowState* st = &s_state[i];

        if (i >= n) {
            if (st->shown) {
                st->shown = false;
                lv_obj_add_flag(r->row, LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }

        // Lazy build: create row on first need so ui_wifi_create() stays fast.
        if (!r->row) {
            build_row(s_list, r, i);
        }

        const ScanResult_t* ap = &aps[i];
        char ssid[33];
        char info[48];
        snprintf(ssid, sizeof(ssid), "%s", ap->ssid[0] ? (const char*)ap->ssid : "<hidden>");
        snprintf(info, sizeof(info), "%s %2u %3d %s",
                 ap->channel <= 14 ? "2.4" : "5G ",
                 ap->channel, ap->rssi, scan_engine_auth_str(ap->authmode));

        if (!st->shown) {
            st->shown = true;
            st->rssi = 0;
            st->ssid[0] = 0;
            st->info[0] = 0;
            lv_obj_remove_flag(r->row, LV_OBJ_FLAG_HIDDEN);
        }
        if (ap->rssi != st->rssi) {
            st->rssi = ap->rssi;
            lv_color_t c = TIER_COLORS[ap->severity];
            lv_bar_set_value(r->bar, ap->rssi, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(r->bar, c, LV_PART_INDICATOR);
            lv_obj_set_style_text_color(r->ssid, c, 0);
        }
        if (strcmp(ssid, st->ssid) != 0) {
            strcpy(st->ssid, ssid);
            lv_label_set_text(r->ssid, ssid);
        }
        if (strcmp(info, st->info) != 0) {
            strcpy(st->info, info);
            lv_label_set_text(r->info, info);
        }
    }
    s_rows_built = true;

    // Force LVGL to recalculate the list's content area now that rows have
    // been created/shown.  With absolute positioning + lazy creation, the
    // scrollable height may not be updated automatically.
    lv_obj_update_layout(s_list);

    // Update env overlay alongside Wi-Fi list
    if (s_env_overlay) {
        update_env_overlay();
    }
}

static void refresh(lv_timer_t*)
{
    do_refresh();
}

static void back_cb(lv_event_t*)
{
    ui_home_load();
}

lv_obj_t* ui_wifi_create(void)
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);


    // Environmental overlay — top-right, compact readout
    s_env_overlay = lv_label_create(scr);
    lv_label_set_text(s_env_overlay, "ENV WAITING...");
    lv_obj_set_style_text_color(s_env_overlay, lv_color_hex(0xFFC107), 0);
    lv_obj_set_style_text_font(s_env_overlay, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_env_overlay, 90, 16);

    s_list = lv_obj_create(scr);
    lv_obj_set_size(s_list, 320, 192);
    lv_obj_set_pos(s_list, 0, 48);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 2, 0);
    lv_obj_set_style_pad_top(s_list, 2, 0);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    // Explicit scroll configuration: vertical only, no snap, momentum enabled.
    // The default flags on a plain lv_obj should include SCROLLABLE, but we
    // reinforce them here because lazy row creation + absolute positioning
    // can leave the content area uncalculated until layout is forced.
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(s_list, LV_SCROLL_SNAP_NONE);

    // Rows are NOT built here — lazy creation in do_refresh() keeps this fast.
    lv_obj_t* back = lv_btn_create(scr);
    lv_obj_set_size(back, 80, 40);
    lv_obj_set_pos(back, 4, 4);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(back, 3, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "<");
    lv_obj_center(back_lbl);

    s_timer = lv_timer_create(refresh, 5000, nullptr);
    return scr;
}

void ui_wifi_set_visible(bool visible)
{
    s_wifi_visible = visible;
    if (s_timer) {
        if (visible) lv_timer_resume(s_timer);
        else         lv_timer_pause(s_timer);
    }
    if (visible && !s_rows_built) {
        // Immediate population if scan results already exist; prevents
        // the black-list stare while waiting for the 5 s timer.
        do_refresh();
    }
}

void ui_wifi_post_env(const EnvSnapshot_t* snap, bool bme680_present)
{
    taskENTER_CRITICAL(&s_env_mux);
    s_env_snap = *snap;
    s_env_has_snap = true;
    s_env_bme_present = bme680_present;
    taskEXIT_CRITICAL(&s_env_mux);
}
