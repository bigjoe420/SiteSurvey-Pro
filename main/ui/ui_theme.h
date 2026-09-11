#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

// ---------------------------------------------------------------------------
// UI theme — indoor vs outdoor (high-contrast, sunlight-readable) palettes.
//
// The outdoor palette raises every gray toward white, swaps the cyan accent
// for amber, and brightens the semantic tier colors. The palette is stored as
// raw hex values so both tables are compile-time constants; the THM_* macros
// convert on use (cheap, and screens read them at creation time on the UI task).
//
// The outdoor flag itself is owned by power_mgr (NVS-persisted, loaded at
// boot on the main task before the UI exists); main.cpp calls
// ui_theme_set_outdoor() once before the UI task starts.
// ---------------------------------------------------------------------------

typedef struct {
    // Surfaces
    uint32_t bg;     // screen / modal background
    uint32_t row;    // list row background
    uint32_t track;  // bar / gauge / chart track background
    uint32_t btn;    // neutral button (back, steppers, detail)
    uint32_t grid;   // chart gridline
    uint32_t border; // list row border
    // Text
    uint32_t text;   // primary value text
    uint32_t label;  // row/setting labels
    uint32_t sub;    // section headers, secondary
    uint32_t faint;  // status / placeholder / min-max captions
    // Accent
    uint32_t accent; // title glow, env overlay, highlight
    // Semantic tiers
    uint32_t ok;       // strong / good
    uint32_t ok_mid;   // DGPS / mid-good
    uint32_t ok_hi;    // PPS / best
    uint32_t warn;     // caution / moderate highlight
    uint32_t yellow;   // tier 2 (moderate)
    uint32_t orange;   // tier 3 (weak)
    uint32_t bad;      // bad / rogue / no-fix
    uint32_t bad_soft; // soft red (alerts gradient)
    // Extra hues (env gauge gradients, RTK cyan)
    uint32_t cyan;
    uint32_t blue;
    uint32_t blue2;
    uint32_t teal;
    // Action buttons
    uint32_t btn_ok;    // confirm / add / OTA / edit
    uint32_t btn_del;   // delete / cancel / power off
    uint32_t btn_blue;  // export
} ssp_palette_t;

// Select palette. Call once at boot before any screen is created.
void ui_theme_set_outdoor(bool outdoor);

// Current selection.
bool ui_theme_outdoor(void);

// Active palette (never returns nullptr).
const ssp_palette_t* ui_theme(void);

// Convenience accessors — resolve to lv_color_t at the call site.
#define THM_BG       lv_color_hex(ui_theme()->bg)
#define THM_ROW      lv_color_hex(ui_theme()->row)
#define THM_TRACK    lv_color_hex(ui_theme()->track)
#define THM_BTN      lv_color_hex(ui_theme()->btn)
#define THM_GRID     lv_color_hex(ui_theme()->grid)
#define THM_BORDER   lv_color_hex(ui_theme()->border)
#define THM_TEXT     lv_color_hex(ui_theme()->text)
#define THM_LABEL    lv_color_hex(ui_theme()->label)
#define THM_SUB      lv_color_hex(ui_theme()->sub)
#define THM_FAINT    lv_color_hex(ui_theme()->faint)
#define THM_ACCENT   lv_color_hex(ui_theme()->accent)
#define THM_OK       lv_color_hex(ui_theme()->ok)
#define THM_OK_MID   lv_color_hex(ui_theme()->ok_mid)
#define THM_OK_HI    lv_color_hex(ui_theme()->ok_hi)
#define THM_WARN     lv_color_hex(ui_theme()->warn)
#define THM_YELLOW   lv_color_hex(ui_theme()->yellow)
#define THM_ORANGE   lv_color_hex(ui_theme()->orange)
#define THM_BAD      lv_color_hex(ui_theme()->bad)
#define THM_BAD_SOFT lv_color_hex(ui_theme()->bad_soft)
#define THM_CYAN     lv_color_hex(ui_theme()->cyan)
#define THM_BLUE     lv_color_hex(ui_theme()->blue)
#define THM_BLUE2    lv_color_hex(ui_theme()->blue2)
#define THM_TEAL     lv_color_hex(ui_theme()->teal)
#define THM_BTN_OK   lv_color_hex(ui_theme()->btn_ok)
#define THM_BTN_DEL  lv_color_hex(ui_theme()->btn_del)
#define THM_BTN_BLUE lv_color_hex(ui_theme()->btn_blue)

// RSSI severity tier → theme color (0=strong .. 3=marginal).
static inline lv_color_t ui_theme_tier_color(int tier)
{
    switch (tier) {
    case 0:  return THM_OK;
    case 1:  return THM_YELLOW;
    case 2:  return THM_ORANGE;
    default: return THM_BAD;
    }
}
