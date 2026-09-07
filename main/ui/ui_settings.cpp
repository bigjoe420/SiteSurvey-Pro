#include "ui_settings.h"

#include <cstdio>
#include <cstring>
#include "alert_engine.h"
#include "power_mgr.h"
#include <cstdlib>
#include "esp_log.h"
#include "ui_home.h"
#include "scan_engine.h"

static bool s_visible;
static lv_obj_t* s_scr;

// Working copy of config (edited on this screen)
static AlertConfig_t s_cfg;

// Scrollable content panel
static lv_obj_t* s_panel;

// Widget references for dynamic updates
static lv_obj_t* s_switch_enabled;
static lv_obj_t* s_lbl_rssi;
static lv_obj_t* s_ssid_rows[ALERT_MAX_SSID_TARGETS];
static lv_obj_t* s_bssid_rows[ALERT_MAX_BSSID_TARGETS];
static lv_obj_t* s_ssid_add_btn;
static lv_obj_t* s_bssid_add_btn;
static lv_obj_t* s_lbl_ssid_header;
static lv_obj_t* s_lbl_bssid_header;

// Power settings widgets
static lv_obj_t* s_lbl_timeout;
static lv_obj_t* s_lbl_dim;
static lv_obj_t* s_switch_sleep;

// Keyboard modal
static lv_obj_t* s_modal;
static lv_obj_t* s_modal_title;
static lv_obj_t* s_ta;
static lv_obj_t* s_kb;
static int s_edit_type;   // 0=SSID, 1=BSSID, 2=RSSI
static int s_edit_index;  // -1=new, 0..3=existing

// Scanned-AP picker modal
static lv_obj_t* s_pick_modal;
static lv_obj_t* s_pick_list;
static lv_obj_t* s_pick_empty;
static lv_obj_t* s_pick_title;
static char s_pick_ssids[16][33];
static int s_pick_count;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void back_cb(lv_event_t*)
{
    ui_home_load();
}

static void refresh_rssi_label(void)
{
    if (!s_lbl_rssi) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d dBm", s_cfg.rssi_threshold);
    lv_label_set_text(s_lbl_rssi, buf);
}

static void refresh_ssid_list(void)
{
    if (s_lbl_ssid_header) {
        char buf[32];
        snprintf(buf, sizeof(buf), "SSID targets (%d/%d)", s_cfg.ssid_count, ALERT_MAX_SSID_TARGETS);
        lv_label_set_text(s_lbl_ssid_header, buf);
    }

    for (int i = 0; i < ALERT_MAX_SSID_TARGETS; i++) {
        if (!s_ssid_rows[i]) continue;
        if (i < s_cfg.ssid_count) {
            lv_obj_remove_flag(s_ssid_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_t* lbl = lv_obj_get_child(s_ssid_rows[i], 0);
            if (lbl) lv_label_set_text(lbl, s_cfg.ssid_targets[i]);
        } else {
            lv_obj_add_flag(s_ssid_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_ssid_add_btn) {
        if (s_cfg.ssid_count >= ALERT_MAX_SSID_TARGETS) {
            lv_obj_add_flag(s_ssid_add_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_ssid_add_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void refresh_bssid_list(void)
{
    if (s_lbl_bssid_header) {
        char buf[32];
        snprintf(buf, sizeof(buf), "BSSID targets (%d/%d)", s_cfg.bssid_count, ALERT_MAX_BSSID_TARGETS);
        lv_label_set_text(s_lbl_bssid_header, buf);
    }

    for (int i = 0; i < ALERT_MAX_BSSID_TARGETS; i++) {
        if (!s_bssid_rows[i]) continue;
        if (i < s_cfg.bssid_count) {
            lv_obj_remove_flag(s_bssid_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_t* lbl = lv_obj_get_child(s_bssid_rows[i], 0);
            if (lbl) lv_label_set_text(lbl, s_cfg.bssid_targets[i]);
        } else {
            lv_obj_add_flag(s_bssid_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_bssid_add_btn) {
        if (s_cfg.bssid_count >= ALERT_MAX_BSSID_TARGETS) {
            lv_obj_add_flag(s_bssid_add_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_bssid_add_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// --- Power helpers ---
static const uint16_t TIMEOUT_PRESETS[] = {0, 10, 30, 60, 120, 300};
static const int TIMEOUT_COUNT = 6;

static void refresh_power_labels(void)
{
    if (s_lbl_timeout) {
        uint16_t t = power_mgr_get_timeout_s();
        char buf[32];
        if (t == 0) snprintf(buf, sizeof(buf), "Never");
        else if (t < 60) snprintf(buf, sizeof(buf), "%us", t);
        else snprintf(buf, sizeof(buf), "%um", t / 60);
        lv_label_set_text(s_lbl_timeout, buf);
    }
    if (s_lbl_dim) {
        uint8_t d = power_mgr_get_dim_pct();
        char buf[16];
        snprintf(buf, sizeof(buf), "%u%%", d);
        lv_label_set_text(s_lbl_dim, buf);
    }
    if (s_switch_sleep) {
        if (power_mgr_get_sleep_en()) {
            lv_obj_add_state(s_switch_sleep, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_switch_sleep, LV_STATE_CHECKED);
        }
    }
}

static void timeout_minus_cb(lv_event_t*)
{
    uint16_t t = power_mgr_get_timeout_s();
    int idx = 0;
    for (int i = TIMEOUT_COUNT - 1; i >= 0; i--) {
        if (TIMEOUT_PRESETS[i] < t) { idx = i; break; }
    }
    power_mgr_set_timeout_s(TIMEOUT_PRESETS[idx]);
    refresh_power_labels();
}

static void timeout_plus_cb(lv_event_t*)
{
    uint16_t t = power_mgr_get_timeout_s();
    int idx = TIMEOUT_COUNT - 1;
    for (int i = 0; i < TIMEOUT_COUNT; i++) {
        if (TIMEOUT_PRESETS[i] > t) { idx = i; break; }
    }
    power_mgr_set_timeout_s(TIMEOUT_PRESETS[idx]);
    refresh_power_labels();
}

static void dim_minus_cb(lv_event_t*)
{
    uint8_t d = power_mgr_get_dim_pct();
    if (d > 10) d -= 5;
    else d = 10;
    power_mgr_set_dim_pct(d);
    refresh_power_labels();
}

static void dim_plus_cb(lv_event_t*)
{
    uint8_t d = power_mgr_get_dim_pct();
    if (d < 50) d += 5;
    else d = 50;
    power_mgr_set_dim_pct(d);
    refresh_power_labels();
}

static void sleep_toggle_cb(lv_event_t* e)
{
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    power_mgr_set_sleep_en(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void save_config(void)
{
    esp_err_t err = alert_config_set(&s_cfg);
    if (err != ESP_OK) {
        ESP_LOGW("settings", "config save failed: %s", esp_err_to_name(err));
    }
}

// ---------------------------------------------------------------------------
// Keyboard modal
// ---------------------------------------------------------------------------

static void close_modal(void)
{
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(s_ta, "");
    if (s_panel) lv_obj_add_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
}

static void modal_ok_cb(lv_event_t*)
{
    const char* text = lv_textarea_get_text(s_ta);
    if (!text || !text[0]) {
        close_modal();
        return;
    }

    if (s_edit_type == 0) {  // SSID
        int idx = (s_edit_index >= 0) ? s_edit_index : s_cfg.ssid_count;
        if (idx < ALERT_MAX_SSID_TARGETS) {
            strncpy(s_cfg.ssid_targets[idx], text, sizeof(s_cfg.ssid_targets[0]) - 1);
            s_cfg.ssid_targets[idx][sizeof(s_cfg.ssid_targets[0]) - 1] = '\0';
            if (s_edit_index < 0) s_cfg.ssid_count++;
        }
        refresh_ssid_list();
    } else if (s_edit_type == 1) {  // BSSID
        int idx = (s_edit_index >= 0) ? s_edit_index : s_cfg.bssid_count;
        if (idx < ALERT_MAX_BSSID_TARGETS) {
            strncpy(s_cfg.bssid_targets[idx], text, sizeof(s_cfg.bssid_targets[0]) - 1);
            s_cfg.bssid_targets[idx][sizeof(s_cfg.bssid_targets[0]) - 1] = '\0';
            if (s_edit_index < 0) s_cfg.bssid_count++;
        }
        refresh_bssid_list();
    } else if (s_edit_type == 2) {  // RSSI threshold
        int val = atoi(text);
        if (val < -100) val = -100;
        if (val > -30) val = -30;
        s_cfg.rssi_threshold = (int8_t)val;
        refresh_rssi_label();
    }

    save_config();
    close_modal();
}

static void modal_cancel_cb(lv_event_t*)
{
    close_modal();
}

static void open_modal(int type, int index)
{
    s_edit_type = type;
    s_edit_index = index;

    if (type == 0 && index >= 0) {
        lv_textarea_set_text(s_ta, s_cfg.ssid_targets[index]);
        if (s_modal_title) lv_label_set_text(s_modal_title, "Enter SSID");
    } else if (type == 1 && index >= 0) {
        lv_textarea_set_text(s_ta, s_cfg.bssid_targets[index]);
        if (s_modal_title) lv_label_set_text(s_modal_title, "Enter BSSID");
    } else if (type == 2) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", s_cfg.rssi_threshold);
        lv_textarea_set_text(s_ta, buf);
        if (s_modal_title) lv_label_set_text(s_modal_title, "Enter RSSI threshold");
    } else {
        lv_textarea_set_text(s_ta, "");
        if (s_modal_title) lv_label_set_text(s_modal_title, "Enter target");
    }

    if (s_pick_modal) lv_obj_add_flag(s_pick_modal, LV_OBJ_FLAG_HIDDEN);
    if (s_panel) lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// Scanned-AP picker modal
// ---------------------------------------------------------------------------

static void close_pick_modal(void)
{
    lv_obj_add_flag(s_pick_modal, LV_OBJ_FLAG_HIDDEN);
    if (s_panel) lv_obj_add_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
}

static void pick_ap_cb(lv_event_t* e)
{
    const char* ssid = (const char*)lv_event_get_user_data(e);
    if (!ssid || !ssid[0]) {
        close_pick_modal();
        return;
    }
    if (s_cfg.ssid_count < ALERT_MAX_SSID_TARGETS) {
        int idx = s_cfg.ssid_count;
        strncpy(s_cfg.ssid_targets[idx], ssid, sizeof(s_cfg.ssid_targets[0]) - 1);
        s_cfg.ssid_targets[idx][sizeof(s_cfg.ssid_targets[0]) - 1] = '\0';
        s_cfg.ssid_count++;
        refresh_ssid_list();
        save_config();
    }
    close_pick_modal();
}

static void pick_manual_cb(lv_event_t*)
{
    close_pick_modal();
    open_modal(0, -1);
}

static void pick_cancel_cb(lv_event_t*)
{
    close_pick_modal();
}

static void open_pick_modal(void)
{
    if (!s_pick_modal || !s_pick_list) return;

    lv_obj_clean(s_pick_list);
    lv_obj_scroll_to_y(s_pick_list, 0, LV_ANIM_OFF);
    s_pick_count = 0;

    ScanResult_t aps[32];
    int n = scan_engine_snapshot(aps, 32);

    // Sort by RSSI descending (strongest first)
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (aps[j].rssi < aps[j + 1].rssi) {
                ScanResult_t tmp = aps[j];
                aps[j] = aps[j + 1];
                aps[j + 1] = tmp;
            }
        }
    }

    // Build rows - limit to 5 to avoid scroll-vs-click ambiguity on small screen
    for (int i = 0; i < n && s_pick_count < 5; i++) {
        // Skip already-targeted SSIDs
        bool already = false;
        for (int j = 0; j < s_cfg.ssid_count; j++) {
            if (strcmp((const char*)aps[i].ssid, s_cfg.ssid_targets[j]) == 0) {
                already = true; break;
            }
        }
        if (already) continue;

        lv_obj_t* row = lv_obj_create(s_pick_list);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(row, 304, 28);
        lv_obj_set_pos(row, 0, s_pick_count * 28);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 2, 0);

        lv_obj_t* lbl = lv_label_create(row);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s  %s  %d dBm",
                 aps[i].ssid[0] ? (const char*)aps[i].ssid : "<hidden>",
                 aps[i].channel <= 14 ? "2.4G" : "5G",
                 aps[i].rssi);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8E8E8), 0);
        lv_obj_set_pos(lbl, 4, 4);

        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_PRESS_LOCK);
        strncpy(s_pick_ssids[s_pick_count], (const char*)aps[i].ssid, 32);
        s_pick_ssids[s_pick_count][32] = '\0';
        lv_obj_add_event_cb(row, pick_ap_cb, LV_EVENT_CLICKED, s_pick_ssids[s_pick_count]);
        s_pick_count++;
    }

    // Size list to fit exactly - never scroll
    if (s_pick_list) {
        int list_h = s_pick_count * 28;
        if (list_h < 28) list_h = 28;  // minimum height when empty
        lv_obj_set_height(s_pick_list, list_h);
        lv_obj_remove_flag(s_pick_list, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Update title if some APs were hidden
    if (s_pick_title) {
        if (n > 5) {
            lv_label_set_text(s_pick_title, "Pick SSID (top 5 by signal)");
        } else {
            lv_label_set_text(s_pick_title, "Pick SSID from scan");
        }
    }

    if (s_pick_empty) {
        if (s_pick_count == 0) {
            lv_obj_clear_flag(s_pick_empty, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_pick_empty, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_modal) lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
    if (s_panel) lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_pick_modal, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

static void toggle_cb(lv_event_t* e)
{
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    s_cfg.enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_config();
}

static void rssi_minus_cb(lv_event_t*)
{
    if (s_cfg.rssi_threshold > -100) s_cfg.rssi_threshold -= 5;
    refresh_rssi_label();
    save_config();
}

static void rssi_plus_cb(lv_event_t*)
{
    if (s_cfg.rssi_threshold < -30) s_cfg.rssi_threshold += 5;
    refresh_rssi_label();
    save_config();
}

static void rssi_label_cb(lv_event_t*)
{
    open_modal(2, -1);
}

static void ssid_delete_cb(lv_event_t* e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < s_cfg.ssid_count) {
        for (int i = idx; i < s_cfg.ssid_count - 1; i++) {
            strcpy(s_cfg.ssid_targets[i], s_cfg.ssid_targets[i + 1]);
        }
        s_cfg.ssid_targets[s_cfg.ssid_count - 1][0] = '\0';
        s_cfg.ssid_count--;
        refresh_ssid_list();
        save_config();
    }
}

static void bssid_delete_cb(lv_event_t* e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < s_cfg.bssid_count) {
        for (int i = idx; i < s_cfg.bssid_count - 1; i++) {
            strcpy(s_cfg.bssid_targets[i], s_cfg.bssid_targets[i + 1]);
        }
        s_cfg.bssid_targets[s_cfg.bssid_count - 1][0] = '\0';
        s_cfg.bssid_count--;
        refresh_bssid_list();
        save_config();
    }
}

static void ssid_add_cb(lv_event_t*)
{
    open_pick_modal();
}

static void bssid_add_cb(lv_event_t*)
{
    open_modal(1, -1);
}

static void ssid_edit_cb(lv_event_t* e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    open_modal(0, idx);
}

static void bssid_edit_cb(lv_event_t* e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    open_modal(1, idx);
}

static void clear_log_cb(lv_event_t*)
{
    alert_log_clear();
}

// ---------------------------------------------------------------------------
// Row builders
// ---------------------------------------------------------------------------

static lv_obj_t* make_target_row(lv_obj_t* parent, int y, lv_event_cb_t edit_cb,
                                  lv_event_cb_t del_cb, void* user_data)
{
    lv_obj_t* row = lv_obj_create(parent);
    if (!row) {
        ESP_LOGE("settings", "make_target_row: row alloc failed");
        return nullptr;
    }
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, 304, 24);
    lv_obj_set_pos(row, 4, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 2, 0);
    lv_obj_set_style_pad_all(row, 2, 0);

    // Label (tappable for edit)
    lv_obj_t* lbl = lv_label_create(row);
    if (lbl) {
        lv_label_set_text(lbl, "---");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8E8E8), 0);
        lv_obj_set_pos(lbl, 4, 2);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(lbl, edit_cb, LV_EVENT_CLICKED, user_data);
    }

    // Delete button
    lv_obj_t* del = lv_btn_create(row);
    if (del) {
        lv_obj_set_size(del, 40, 20);
        lv_obj_set_pos(del, 260, 2);
        lv_obj_set_style_bg_color(del, lv_color_hex(0xB71C1C), 0);
        lv_obj_set_style_radius(del, 2, 0);
        lv_obj_add_event_cb(del, del_cb, LV_EVENT_CLICKED, user_data);

        lv_obj_t* del_lbl = lv_label_create(del);
        if (del_lbl) {
            lv_label_set_text(del_lbl, "X");
            lv_obj_set_style_text_font(del_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(del_lbl, lv_color_white(), 0);
            lv_obj_center(del_lbl);
        }
    }

    return row;
}

// ---------------------------------------------------------------------------
// Screen creation
// ---------------------------------------------------------------------------

lv_obj_t* ui_settings_create(void)
{
    s_scr = lv_obj_create(nullptr);
    if (!s_scr) {
        ESP_LOGE("settings", "ui_settings_create: screen alloc failed");
        return nullptr;
    }
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);

    // -----------------------------------------------------------------------
    // Scrollable content panel (create FIRST so back button sits on top)
    // -----------------------------------------------------------------------
    s_panel = lv_obj_create(s_scr);
    if (s_panel) {
        lv_obj_set_size(s_panel, 320, 196);
        lv_obj_set_pos(s_panel, 0, 44);
        lv_obj_set_style_bg_color(s_panel, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_panel, 0, 0);
        lv_obj_set_style_pad_all(s_panel, 4, 0);
        lv_obj_set_scroll_dir(s_panel, LV_DIR_VER);
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_SCROLL_ELASTIC);
    }

    int y = 0;
    const int ROW_H = 30;
    const int GAP = 4;
    const int SECTION_GAP = 8;

    // --- Alerts Enabled ---
    if (s_panel) {
        lv_obj_t* lbl_enabled = lv_label_create(s_panel);
        if (lbl_enabled) {
            lv_label_set_text(lbl_enabled, "Alerts");
            lv_obj_set_style_text_font(lbl_enabled, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl_enabled, lv_color_hex(0xB0B0B0), 0);
            lv_obj_set_pos(lbl_enabled, 8, y + 6);
        }

        s_switch_enabled = lv_switch_create(s_panel);
        if (s_switch_enabled) {
            lv_obj_set_size(s_switch_enabled, 44, 22);
            lv_obj_set_pos(s_switch_enabled, 260, y + 4);
            lv_obj_add_event_cb(s_switch_enabled, toggle_cb, LV_EVENT_VALUE_CHANGED, nullptr);
        }
        y += ROW_H + GAP;
    }

    // --- RSSI Threshold ---
    if (s_panel) {
        lv_obj_t* lbl_rssi_title = lv_label_create(s_panel);
        if (lbl_rssi_title) {
            lv_label_set_text(lbl_rssi_title, "RSSI threshold");
            lv_obj_set_style_text_font(lbl_rssi_title, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl_rssi_title, lv_color_hex(0xB0B0B0), 0);
            lv_obj_set_pos(lbl_rssi_title, 8, y + 6);
        }

        s_lbl_rssi = lv_label_create(s_panel);
        if (s_lbl_rssi) {
            lv_label_set_text(s_lbl_rssi, "-80 dBm");
            lv_obj_set_style_text_font(s_lbl_rssi, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_lbl_rssi, lv_color_hex(0xE8E8E8), 0);
            lv_obj_set_pos(s_lbl_rssi, 140, y + 6);
            lv_obj_add_flag(s_lbl_rssi, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(s_lbl_rssi, rssi_label_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_ext_click_area(s_lbl_rssi, 10);
        }

        lv_obj_t* btn_minus = lv_btn_create(s_panel);
        if (btn_minus) {
            lv_obj_set_size(btn_minus, 28, 24);
            lv_obj_set_pos(btn_minus, 230, y + 3);
            lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0x333333), 0);
            lv_obj_set_style_radius(btn_minus, 2, 0);
            lv_obj_add_event_cb(btn_minus, rssi_minus_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_ext_click_area(btn_minus, 12);
            lv_obj_t* lbl_minus = lv_label_create(btn_minus);
            if (lbl_minus) {
                lv_label_set_text(lbl_minus, "-");
                lv_obj_center(lbl_minus);
            }
        }

        lv_obj_t* btn_plus = lv_btn_create(s_panel);
        if (btn_plus) {
            lv_obj_set_size(btn_plus, 28, 24);
            lv_obj_set_pos(btn_plus, 264, y + 3);
            lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x333333), 0);
            lv_obj_set_style_radius(btn_plus, 2, 0);
            lv_obj_add_event_cb(btn_plus, rssi_plus_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_ext_click_area(btn_plus, 12);
            lv_obj_t* lbl_plus = lv_label_create(btn_plus);
            if (lbl_plus) {
                lv_label_set_text(lbl_plus, "+");
                lv_obj_center(lbl_plus);
            }
        }
        y += ROW_H + SECTION_GAP;
    }

    // --- SSID targets ---
    if (s_panel) {
        s_lbl_ssid_header = lv_label_create(s_panel);
        if (s_lbl_ssid_header) {
            lv_label_set_text(s_lbl_ssid_header, "SSID targets (0/4)");
            lv_obj_set_style_text_font(s_lbl_ssid_header, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_lbl_ssid_header, lv_color_hex(0x909090), 0);
            lv_obj_set_pos(s_lbl_ssid_header, 8, y);
        }
        y += 18 + GAP;

        for (int i = 0; i < ALERT_MAX_SSID_TARGETS; i++) {
            s_ssid_rows[i] = make_target_row(s_panel, y, ssid_edit_cb, ssid_delete_cb, (void*)(intptr_t)i);
            if (s_ssid_rows[i]) {
                lv_obj_add_flag(s_ssid_rows[i], LV_OBJ_FLAG_HIDDEN);
            }
            y += 24 + GAP;
        }

        s_ssid_add_btn = lv_btn_create(s_panel);
        if (s_ssid_add_btn) {
            lv_obj_set_size(s_ssid_add_btn, 120, 24);
            lv_obj_set_pos(s_ssid_add_btn, 8, y);
            lv_obj_set_style_bg_color(s_ssid_add_btn, lv_color_hex(0x2E7D32), 0);
            lv_obj_set_style_radius(s_ssid_add_btn, 2, 0);
            lv_obj_add_event_cb(s_ssid_add_btn, ssid_add_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t* lbl_ssid_add = lv_label_create(s_ssid_add_btn);
            if (lbl_ssid_add) {
                lv_label_set_text(lbl_ssid_add, "+ Add SSID");
                lv_obj_set_style_text_font(lbl_ssid_add, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(lbl_ssid_add, lv_color_white(), 0);
                lv_obj_center(lbl_ssid_add);
            }
        }
        y += 24 + SECTION_GAP;
    }

    // --- BSSID targets ---
    if (s_panel) {
        s_lbl_bssid_header = lv_label_create(s_panel);
        if (s_lbl_bssid_header) {
            lv_label_set_text(s_lbl_bssid_header, "BSSID targets (0/4)");
            lv_obj_set_style_text_font(s_lbl_bssid_header, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_lbl_bssid_header, lv_color_hex(0x909090), 0);
            lv_obj_set_pos(s_lbl_bssid_header, 8, y);
        }
        y += 18 + GAP;

        for (int i = 0; i < ALERT_MAX_BSSID_TARGETS; i++) {
            s_bssid_rows[i] = make_target_row(s_panel, y, bssid_edit_cb, bssid_delete_cb, (void*)(intptr_t)i);
            if (s_bssid_rows[i]) {
                lv_obj_add_flag(s_bssid_rows[i], LV_OBJ_FLAG_HIDDEN);
            }
            y += 24 + GAP;
        }

        s_bssid_add_btn = lv_btn_create(s_panel);
        if (s_bssid_add_btn) {
            lv_obj_set_size(s_bssid_add_btn, 120, 24);
            lv_obj_set_pos(s_bssid_add_btn, 8, y);
            lv_obj_set_style_bg_color(s_bssid_add_btn, lv_color_hex(0x2E7D32), 0);
            lv_obj_set_style_radius(s_bssid_add_btn, 2, 0);
            lv_obj_add_event_cb(s_bssid_add_btn, bssid_add_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t* lbl_bssid_add = lv_label_create(s_bssid_add_btn);
            if (lbl_bssid_add) {
                lv_label_set_text(lbl_bssid_add, "+ Add BSSID");
                lv_obj_set_style_text_font(lbl_bssid_add, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(lbl_bssid_add, lv_color_white(), 0);
                lv_obj_center(lbl_bssid_add);
            }
        }
        y += 24 + SECTION_GAP;
    }

    // --- Power ---
    if (s_panel) {
        lv_obj_t* lbl_power = lv_label_create(s_panel);
        if (lbl_power) {
            lv_label_set_text(lbl_power, "Power");
            lv_obj_set_style_text_font(lbl_power, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl_power, lv_color_hex(0xB0B0B0), 0);
            lv_obj_set_pos(lbl_power, 8, y + 6);
        }
        y += ROW_H + GAP;

        // Timeout
        lv_obj_t* lbl_to_title = lv_label_create(s_panel);
        if (lbl_to_title) {
            lv_label_set_text(lbl_to_title, "Timeout");
            lv_obj_set_style_text_font(lbl_to_title, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl_to_title, lv_color_hex(0xB0B0B0), 0);
            lv_obj_set_pos(lbl_to_title, 8, y + 6);
        }

        s_lbl_timeout = lv_label_create(s_panel);
        if (s_lbl_timeout) {
            lv_obj_set_style_text_font(s_lbl_timeout, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_lbl_timeout, lv_color_hex(0xE8E8E8), 0);
            lv_obj_set_pos(s_lbl_timeout, 140, y + 6);
        }

        lv_obj_t* btn_to_minus = lv_btn_create(s_panel);
        if (btn_to_minus) {
            lv_obj_set_size(btn_to_minus, 28, 24);
            lv_obj_set_pos(btn_to_minus, 230, y + 3);
            lv_obj_set_style_bg_color(btn_to_minus, lv_color_hex(0x333333), 0);
            lv_obj_set_style_radius(btn_to_minus, 2, 0);
            lv_obj_add_event_cb(btn_to_minus, timeout_minus_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_ext_click_area(btn_to_minus, 12);
            lv_obj_t* lbl_to_minus = lv_label_create(btn_to_minus);
            if (lbl_to_minus) {
                lv_label_set_text(lbl_to_minus, "-");
                lv_obj_center(lbl_to_minus);
            }
        }

        lv_obj_t* btn_to_plus = lv_btn_create(s_panel);
        if (btn_to_plus) {
            lv_obj_set_size(btn_to_plus, 28, 24);
            lv_obj_set_pos(btn_to_plus, 264, y + 3);
            lv_obj_set_style_bg_color(btn_to_plus, lv_color_hex(0x333333), 0);
            lv_obj_set_style_radius(btn_to_plus, 2, 0);
            lv_obj_add_event_cb(btn_to_plus, timeout_plus_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_ext_click_area(btn_to_plus, 12);
            lv_obj_t* lbl_to_plus = lv_label_create(btn_to_plus);
            if (lbl_to_plus) {
                lv_label_set_text(lbl_to_plus, "+");
                lv_obj_center(lbl_to_plus);
            }
        }
        y += ROW_H + GAP;

        // Dim level
        lv_obj_t* lbl_dim_title = lv_label_create(s_panel);
        if (lbl_dim_title) {
            lv_label_set_text(lbl_dim_title, "Dim level");
            lv_obj_set_style_text_font(lbl_dim_title, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl_dim_title, lv_color_hex(0xB0B0B0), 0);
            lv_obj_set_pos(lbl_dim_title, 8, y + 6);
        }

        s_lbl_dim = lv_label_create(s_panel);
        if (s_lbl_dim) {
            lv_obj_set_style_text_font(s_lbl_dim, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_lbl_dim, lv_color_hex(0xE8E8E8), 0);
            lv_obj_set_pos(s_lbl_dim, 140, y + 6);
        }

        lv_obj_t* btn_dim_minus = lv_btn_create(s_panel);
        if (btn_dim_minus) {
            lv_obj_set_size(btn_dim_minus, 28, 24);
            lv_obj_set_pos(btn_dim_minus, 230, y + 3);
            lv_obj_set_style_bg_color(btn_dim_minus, lv_color_hex(0x333333), 0);
            lv_obj_set_style_radius(btn_dim_minus, 2, 0);
            lv_obj_add_event_cb(btn_dim_minus, dim_minus_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_ext_click_area(btn_dim_minus, 12);
            lv_obj_t* lbl_dim_minus = lv_label_create(btn_dim_minus);
            if (lbl_dim_minus) {
                lv_label_set_text(lbl_dim_minus, "-");
                lv_obj_center(lbl_dim_minus);
            }
        }

        lv_obj_t* btn_dim_plus = lv_btn_create(s_panel);
        if (btn_dim_plus) {
            lv_obj_set_size(btn_dim_plus, 28, 24);
            lv_obj_set_pos(btn_dim_plus, 264, y + 3);
            lv_obj_set_style_bg_color(btn_dim_plus, lv_color_hex(0x333333), 0);
            lv_obj_set_style_radius(btn_dim_plus, 2, 0);
            lv_obj_add_event_cb(btn_dim_plus, dim_plus_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_ext_click_area(btn_dim_plus, 12);
            lv_obj_t* lbl_dim_plus = lv_label_create(btn_dim_plus);
            if (lbl_dim_plus) {
                lv_label_set_text(lbl_dim_plus, "+");
                lv_obj_center(lbl_dim_plus);
            }
        }
        y += ROW_H + GAP;

        // Light sleep
        lv_obj_t* lbl_sleep = lv_label_create(s_panel);
        if (lbl_sleep) {
            lv_label_set_text(lbl_sleep, "Light sleep");
            lv_obj_set_style_text_font(lbl_sleep, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl_sleep, lv_color_hex(0xB0B0B0), 0);
            lv_obj_set_pos(lbl_sleep, 8, y + 6);
        }

        s_switch_sleep = lv_switch_create(s_panel);
        if (s_switch_sleep) {
            lv_obj_set_size(s_switch_sleep, 44, 22);
            lv_obj_set_pos(s_switch_sleep, 260, y + 4);
            lv_obj_add_event_cb(s_switch_sleep, sleep_toggle_cb, LV_EVENT_VALUE_CHANGED, nullptr);
        }
        y += ROW_H + SECTION_GAP;
    }

    // --- Clear Alert Log ---
    if (s_panel) {
        lv_obj_t* btn_clear = lv_btn_create(s_panel);
        if (btn_clear) {
            lv_obj_set_size(btn_clear, 160, 30);
            lv_obj_set_pos(btn_clear, 80, y);
            lv_obj_set_style_bg_color(btn_clear, lv_color_hex(0xB71C1C), 0);
            lv_obj_set_style_radius(btn_clear, 3, 0);
            lv_obj_add_event_cb(btn_clear, clear_log_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t* lbl_clear = lv_label_create(btn_clear);
            if (lbl_clear) {
                lv_label_set_text(lbl_clear, "Clear Alert Log");
                lv_obj_set_style_text_font(lbl_clear, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(lbl_clear, lv_color_white(), 0);
                lv_obj_center(lbl_clear);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Back button (created AFTER panel so it sits on top in z-order)
    // -----------------------------------------------------------------------
    lv_obj_t* back = lv_btn_create(s_scr);
    if (back) {
        lv_obj_set_size(back, 80, 32);
        lv_obj_set_pos(back, 4, 4);
        lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(back, 3, 0);
        lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_set_ext_click_area(back, 32);

        lv_obj_t* back_lbl = lv_label_create(back);
        if (back_lbl) {
            lv_label_set_text(back_lbl, "<");
            lv_obj_center(back_lbl);
        }
    }

    // Title
    lv_obj_t* title = lv_label_create(s_scr);
    if (title) {
        lv_label_set_text(title, "SETTINGS");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xE8E8E8), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    }

    // -----------------------------------------------------------------------
    // Keyboard modal overlay (created LAST so it blocks everything when visible)
    // -----------------------------------------------------------------------
    s_modal = lv_obj_create(s_scr);
    if (s_modal) {
        lv_obj_set_size(s_modal, 320, 240);
        lv_obj_set_pos(s_modal, 0, 0);
        lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_modal, 0, 0);
        lv_obj_set_style_pad_all(s_modal, 0, 0);
        lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);  // block pass-through
        lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);

        // Title for modal
        s_modal_title = lv_label_create(s_modal);
        if (s_modal_title) {
            lv_label_set_text(s_modal_title, "Enter target");
            lv_obj_set_style_text_font(s_modal_title, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_modal_title, lv_color_hex(0xE8E8E8), 0);
            lv_obj_align(s_modal_title, LV_ALIGN_TOP_MID, 0, 6);
        }

        // Text area - compact input field above keyboard
        s_ta = lv_textarea_create(s_modal);
        if (s_ta) {
            lv_obj_set_size(s_ta, 280, 18);
            lv_obj_set_pos(s_ta, 20, 20);

            lv_textarea_set_one_line(s_ta, true);
            lv_textarea_set_max_length(s_ta, 32);
            lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_ta, lv_color_white(), 0);
            lv_obj_set_style_bg_color(s_ta, lv_color_hex(0x333333), 0);
            lv_obj_set_style_bg_opa(s_ta, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(s_ta, 0, 0);
            lv_obj_set_style_pad_all(s_ta, 4, 0);

            // OK button
            lv_obj_t* btn_ok = lv_btn_create(s_modal);
            if (btn_ok) {
                lv_obj_set_size(btn_ok, 80, 18);
                lv_obj_set_pos(btn_ok, 60, 42);
                lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x2E7D32), 0);
                lv_obj_set_style_radius(btn_ok, 3, 0);
                lv_obj_add_event_cb(btn_ok, modal_ok_cb, LV_EVENT_CLICKED, nullptr);
                lv_obj_t* lbl_ok = lv_label_create(btn_ok);
                if (lbl_ok) {
                    lv_label_set_text(lbl_ok, "OK");
                    lv_obj_center(lbl_ok);
                }
            }

            // Cancel button
            lv_obj_t* btn_cancel = lv_btn_create(s_modal);
            if (btn_cancel) {
                lv_obj_set_size(btn_cancel, 80, 18);
                lv_obj_set_pos(btn_cancel, 180, 42);
                lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0xB71C1C), 0);
                lv_obj_set_style_radius(btn_cancel, 3, 0);
                lv_obj_add_event_cb(btn_cancel, modal_cancel_cb, LV_EVENT_CLICKED, nullptr);
                lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
                if (lbl_cancel) {
                    lv_label_set_text(lbl_cancel, "Cancel");
                    lv_obj_center(lbl_cancel);
                }
            }

            // Keyboard
            s_kb = lv_keyboard_create(s_modal);
            if (s_kb) {
                lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(s_kb, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_flag(s_kb, LV_OBJ_FLAG_PRESS_LOCK);
                lv_keyboard_set_textarea(s_kb, s_ta);
                lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
                lv_keyboard_set_popovers(s_kb, true);
                // Strip internal padding so keys fill the widget
                lv_obj_set_style_pad_all(s_kb, 0, 0);
                lv_obj_set_style_pad_row(s_kb, 1, 0);
                lv_obj_set_style_pad_column(s_kb, 1, 0);
                lv_obj_set_style_border_width(s_kb, 0, 0);
                // Shrink keys: smaller font and zero inner padding on buttons

                lv_obj_set_style_pad_all(s_kb, 0, LV_PART_ITEMS);
                // Set size/pos AFTER textarea to override any auto-layout
                lv_obj_set_size(s_kb, 320, 178);
                lv_obj_set_pos(s_kb, 0, 60);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Scanned-AP picker modal
    // -----------------------------------------------------------------------
    s_pick_modal = lv_obj_create(s_scr);
    if (s_pick_modal) {
        lv_obj_set_size(s_pick_modal, 320, 240);
        lv_obj_set_pos(s_pick_modal, 0, 0);
        lv_obj_set_style_bg_color(s_pick_modal, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_pick_modal, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_pick_modal, 0, 0);
        lv_obj_set_style_pad_all(s_pick_modal, 0, 0);
        lv_obj_add_flag(s_pick_modal, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_pick_modal, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_pick_modal, LV_OBJ_FLAG_HIDDEN);

        s_pick_title = lv_label_create(s_pick_modal);
        if (s_pick_title) {
            lv_label_set_text(s_pick_title, "Pick SSID from scan");
            lv_obj_set_style_text_font(s_pick_title, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_pick_title, lv_color_hex(0xE8E8E8), 0);
            lv_obj_align(s_pick_title, LV_ALIGN_TOP_MID, 0, 8);
        }

        s_pick_list = lv_obj_create(s_pick_modal);
        if (s_pick_list) {
            lv_obj_set_size(s_pick_list, 320, 160);
            lv_obj_set_pos(s_pick_list, 0, 30);
            lv_obj_set_style_bg_opa(s_pick_list, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(s_pick_list, 0, 0);
            lv_obj_set_scroll_dir(s_pick_list, LV_DIR_VER);
            lv_obj_add_flag(s_pick_list, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(s_pick_list, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_scroll_snap_y(s_pick_list, LV_SCROLL_SNAP_START);


        }

        s_pick_empty = lv_label_create(s_pick_modal);
        if (s_pick_empty) {
            lv_label_set_text(s_pick_empty, "No APs found\nWait for scan...");
            lv_obj_set_style_text_font(s_pick_empty, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_pick_empty, lv_color_hex(0x757575), 0);
            lv_obj_set_style_text_align(s_pick_empty, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(s_pick_empty, LV_ALIGN_CENTER, 0, -10);
            lv_obj_add_flag(s_pick_empty, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_t* btn_manual = lv_btn_create(s_pick_modal);
        if (btn_manual) {
            lv_obj_set_size(btn_manual, 120, 28);
            lv_obj_set_pos(btn_manual, 20, 200);
            lv_obj_set_style_bg_color(btn_manual, lv_color_hex(0x2E7D32), 0);
            lv_obj_set_style_radius(btn_manual, 2, 0);
            lv_obj_add_event_cb(btn_manual, pick_manual_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t* lbl_manual = lv_label_create(btn_manual);
            if (lbl_manual) {
                lv_label_set_text(lbl_manual, "Type manually");
                lv_obj_set_style_text_font(lbl_manual, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(lbl_manual, lv_color_white(), 0);
                lv_obj_center(lbl_manual);
            }
        }

        lv_obj_t* btn_cancel2 = lv_btn_create(s_pick_modal);
        if (btn_cancel2) {
            lv_obj_set_size(btn_cancel2, 120, 28);
            lv_obj_set_pos(btn_cancel2, 180, 200);
            lv_obj_set_style_bg_color(btn_cancel2, lv_color_hex(0xB71C1C), 0);
            lv_obj_set_style_radius(btn_cancel2, 2, 0);
            lv_obj_add_event_cb(btn_cancel2, pick_cancel_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t* lbl_cancel2 = lv_label_create(btn_cancel2);
            if (lbl_cancel2) {
                lv_label_set_text(lbl_cancel2, "Cancel");
                lv_obj_set_style_text_font(lbl_cancel2, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(lbl_cancel2, lv_color_white(), 0);
                lv_obj_center(lbl_cancel2);
            }
        }
    }

    // Load current config
    const AlertConfig_t* cfg = alert_config_get();
    s_cfg = *cfg;

    // Apply to UI
    if (s_switch_enabled) {
        if (s_cfg.enabled) {
            lv_obj_add_state(s_switch_enabled, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_switch_enabled, LV_STATE_CHECKED);
        }
    }
    refresh_rssi_label();
    refresh_ssid_list();
    refresh_bssid_list();
    refresh_power_labels();

    return s_scr;
}

void ui_settings_set_visible(bool visible)
{
    s_visible = visible;
    if (visible) {
        // Reload config in case it changed externally
        const AlertConfig_t* cfg = alert_config_get();
        s_cfg = *cfg;
        if (s_switch_enabled) {
            if (s_cfg.enabled) {
                lv_obj_add_state(s_switch_enabled, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(s_switch_enabled, LV_STATE_CHECKED);
            }
        }
        refresh_rssi_label();
        refresh_ssid_list();
        refresh_bssid_list();
        refresh_power_labels();
    } else {
        // Ensure modal is hidden when leaving Settings so keyboard state
        // doesn't leak across screen transitions.
        if (s_modal) {
            lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_ta) {
            lv_textarea_set_text(s_ta, "");
        }
        if (s_pick_modal) {
            lv_obj_add_flag(s_pick_modal, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_panel) {
            lv_obj_add_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
}
