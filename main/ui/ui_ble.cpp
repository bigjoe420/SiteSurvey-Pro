#include "ui_ble.h"

#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "scan_ble.h"
#include "ui_home.h"

#define MAX_ROWS 8

// Build at most this many rows per do_refresh() call to avoid stalling
// lv_timer_handler() with a burst of object creation.
static constexpr int BUILD_BATCH = 2;

typedef struct {
    lv_obj_t* row;
    lv_obj_t* bar;
    lv_obj_t* name;
    lv_obj_t* info;
} Row;

typedef struct {
    bool shown;
    int8_t rssi;
    char name[32];
    char info[48];
} RowState;

static Row       s_rows[MAX_ROWS];
static RowState  s_state[MAX_ROWS];
static lv_obj_t* s_list;
static bool      s_ble_visible;
static bool      s_rows_built;
static lv_timer_t* s_timer;
static lv_obj_t* s_status_lbl;

static const lv_color_t TIER_COLORS[] = {
    lv_color_hex(0x4CAF50), lv_color_hex(0xFFEB3B),
    lv_color_hex(0xFF9800), lv_color_hex(0xF44336),
};

#define ROW_H       30
#define ROW_STRIDE  32
#define LIST_W      316
#define ROW_W       LIST_W

#define COL_BAR_W   40
#define COL_BAR_X   4
#define COL_NAME_X  48
#define COL_NAME_W  130
#define COL_INFO_X  182
#define COL_INFO_W  130

static void build_row(lv_obj_t* parent, Row* r, int idx)
{
    r->row = lv_obj_create(parent);
    lv_obj_remove_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
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

    r->name = lv_label_create(r->row);
    lv_obj_set_size(r->name, COL_NAME_W, LV_SIZE_CONTENT);
    lv_label_set_long_mode(r->name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(r->name, lv_color_white(), 0);
    lv_obj_set_pos(r->name, COL_NAME_X, (ROW_H - lv_font_get_line_height(&lv_font_montserrat_14)) / 2);

    r->info = lv_label_create(r->row);
    lv_obj_set_size(r->info, COL_INFO_W, LV_SIZE_CONTENT);
    lv_label_set_long_mode(r->info, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(r->info, lv_color_hex(0xE8E8E8), 0);
    lv_obj_set_style_text_align(r->info, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(r->info, COL_INFO_X, (ROW_H - lv_font_get_line_height(&lv_font_montserrat_14)) / 2);
}

static void fmt_mac(char* buf, size_t len, const uint8_t* mac)
{
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void fmt_mfg_prefix(char* buf, size_t len, const BleScanResult_t* dev)
{
    if (dev->mfg_len == 0) {
        buf[0] = '\0';
        return;
    }
    int n = snprintf(buf, len, "M:");
    for (int i = 0; i < dev->mfg_len && i < 4 && n < (int)len - 3; i++) {
        n += snprintf(buf + n, len - n, "%02X", dev->mfg_data[i]);
    }
}

static void update_status(int n)
{
    if (!s_status_lbl) return;
    if (n == 0) {
        lv_label_set_text(s_status_lbl, "Scanning...");
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x757575), 0);
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d device%s found", n, n == 1 ? "" : "s");
        lv_label_set_text(s_status_lbl, buf);
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xB0B0B0), 0);
    }
}

static void do_refresh(void)
{
    if (!s_ble_visible) return;

    static BleScanResult_t devs[MAX_ROWS];
    int n = ble_scan_snapshot(devs, MAX_ROWS);

    ESP_LOGI("ui_ble", "do_refresh: snapshot=%d rows_built=%d", n, s_rows_built);
    update_status(n);

    int built_this_call = 0;
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

        // Lazy build: create row on first need, but cap per call to avoid
        // stalling lv_timer_handler() with a burst of object creation.
        if (!r->row) {
            if (built_this_call >= BUILD_BATCH) continue;
            build_row(s_list, r, i);
            built_this_call++;
        }

        const BleScanResult_t* dev = &devs[i];
        char name[32];
        char info[48];

        if (dev->name[0]) {
            snprintf(name, sizeof(name), "%s", dev->name);
        } else {
            fmt_mac(name, sizeof(name), dev->mac);
        }

        char mfg[16];
        fmt_mfg_prefix(mfg, sizeof(mfg), dev);
        if (mfg[0]) {
            snprintf(info, sizeof(info), "%s %4d", mfg, dev->rssi);
        } else {
            snprintf(info, sizeof(info), "%4d dBm", dev->rssi);
        }

        if (!st->shown) {
            st->shown = true;
            st->rssi = 0;
            st->name[0] = 0;
            st->info[0] = 0;
            lv_obj_remove_flag(r->row, LV_OBJ_FLAG_HIDDEN);
        }
        if (dev->rssi != st->rssi) {
            st->rssi = dev->rssi;
            lv_color_t c = TIER_COLORS[dev->severity];
            lv_bar_set_value(r->bar, dev->rssi, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(r->bar, c, LV_PART_INDICATOR);
            lv_obj_set_style_text_color(r->name, c, 0);
        }
        if (strcmp(name, st->name) != 0) {
            strcpy(st->name, name);
            lv_label_set_text(r->name, name);
        }
        if (strcmp(info, st->info) != 0) {
            strcpy(st->info, info);
            lv_label_set_text(r->info, info);
        }
    }

    // Only force layout recalc if we actually built rows this call.
    if (built_this_call > 0) {
        lv_obj_update_layout(s_list);
    }

    // Check whether all needed rows are now built.
    if (!s_rows_built) {
        bool all_built = true;
        for (int i = 0; i < n && i < MAX_ROWS; i++) {
            if (!s_rows[i].row) { all_built = false; break; }
        }
        if (all_built) {
            s_rows_built = true;
            lv_timer_set_period(s_timer, 3000);  // restore normal 3 s refresh
        }
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

lv_obj_t* ui_ble_create(void)
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // List FIRST — back button created after so it sits on top in z-order
    s_list = lv_obj_create(scr);
    lv_obj_set_size(s_list, 320, 176);
    lv_obj_set_pos(s_list, 0, 64);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 2, 0);
    lv_obj_set_style_pad_top(s_list, 2, 0);
    lv_obj_set_style_pad_row(s_list, 0, 0);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(s_list, LV_SCROLL_SNAP_NONE);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "BLE DEVICES");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // Back button LAST — natural top z-order, no move_foreground needed
    lv_obj_t* back = lv_btn_create(scr);
    lv_obj_set_size(back, 80, 32);
    lv_obj_set_pos(back, 4, 4);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(back, 3, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
    // ext_click_area 32 (was 24): retest capture 2026-08-31 showed 70/70
    // in-zone presses clicked (zero firmware failures) and isolated the
    // residual misses to one corner cluster at x 84-88 / y 62-68, 2-8 px
    // below the old zone bottom (y=60).  Zone now reaches y=68 / x=116;
    // the top 4 px of list row 1 (x<=116) becomes button — back is
    // topmost, so it wins the hit test there.
    lv_obj_set_ext_click_area(back, 32);

    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "<");
    lv_obj_center(back_lbl);

    // Status label below title — shows device count or "Scanning..."
    s_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_status_lbl, "Scanning...");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x757575), 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, 36);

    s_timer = lv_timer_create(refresh, 3000, nullptr);
    return scr;
}

void ui_ble_set_visible(bool visible)
{
    s_ble_visible = visible;
    if (s_timer) {
        if (visible) lv_timer_resume(s_timer);
        else         lv_timer_pause(s_timer);
    }
    if (visible) {
        // Always force fast-build on entry — rows may be stale from a
        // previous visit, or the snapshot may have changed.
        s_rows_built = false;
        lv_timer_set_period(s_timer, 50);
        lv_timer_reset(s_timer);
        do_refresh();
    }
}
