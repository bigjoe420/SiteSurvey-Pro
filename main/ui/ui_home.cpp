#include "ui_home.h"

#include "ui_wifi.h"
#include "ui_ble.h"
#include "ui_env.h"
#include "ui_spectrum.h"
#include "ui_gps.h"
#include "ui_alerts.h"
#include "ui_settings.h"
#include "ui_theme.h"

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

// One row per destination screen; each button's user_data points at its row.
struct NavEntry {
    lv_obj_t** scr;               // lazy screen slot
    lv_obj_t*  (*create)(void);   // screen factory
    void       (*set_visible)(bool);
};

static NavEntry s_nav[] = {
    { &s_wifi_scr,     ui_wifi_create,     ui_wifi_set_visible     },
    { &s_ble_scr,      ui_ble_create,      ui_ble_set_visible      },
    { &s_spectrum_scr, ui_spectrum_create, ui_spectrum_set_visible },
    { &s_env_scr,      ui_env_create,      ui_env_set_visible      },
    { &s_gps_scr,      ui_gps_create,      ui_gps_set_visible      },
    { &s_alerts_scr,   ui_alerts_create,   ui_alerts_set_visible   },
    { &s_settings_scr, ui_settings_create, ui_settings_set_visible },
};
static constexpr int NAV_COUNT = sizeof(s_nav) / sizeof(s_nav[0]);

// Show exactly one screen: lazy-create if needed, load it, hide the rest.
static void nav_go(NavEntry* nav)
{
    if (!nav_guard()) return;
    if (!*nav->scr) *nav->scr = nav->create();
    if (!*nav->scr) return;
    lv_screen_load(*nav->scr);
    for (int i = 0; i < NAV_COUNT; i++) {
        s_nav[i].set_visible(&s_nav[i] == nav);
    }
}

static void nav_btn_cb(lv_event_t* e)
{
    nav_go((NavEntry*)lv_event_get_user_data(e));
}

// ---------------------------------------------------------------------------
// Block helper — premium styling
// ---------------------------------------------------------------------------

static lv_obj_t* make_block(lv_obj_t* parent, const char* label_text,
                            lv_color_t bg_top, lv_color_t bg_bot,
                            lv_color_t press_top, lv_color_t press_bot,
                            NavEntry* nav, int x, int y, int w)
{
    const int H = 44;

    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, H);
    lv_obj_set_pos(btn, x, y);

    // Normal state — vertical gradient
    lv_obj_set_style_bg_color(btn, bg_top, 0);
    lv_obj_set_style_bg_grad_color(btn, bg_bot, 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(btn, 0, 0);
    lv_obj_set_style_bg_grad_stop(btn, 255, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    // Pressed state — darker flat gradient
    lv_obj_set_style_bg_color(btn, press_top, LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(btn, press_bot, LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_STATE_PRESSED);

    // Drop shadow for depth
    lv_obj_set_style_shadow_color(btn, lv_color_black(), 0);
    lv_obj_set_style_shadow_width(btn, 6, 0);
    lv_obj_set_style_shadow_offset_x(btn, 0, 0);
    lv_obj_set_style_shadow_offset_y(btn, 3, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_spread(btn, 1, 0);

    lv_obj_add_event_cb(btn, nav_btn_cb, LV_EVENT_CLICKED, nav);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ui_theme()->text), 0);
    lv_obj_center(lbl);

    return btn;
}

lv_obj_t* ui_home_create(void)
{
    s_home = lv_obj_create(nullptr);
    lv_obj_remove_flag(s_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_home, lv_color_black(), 0);

    // -----------------------------------------------------------------------
    // Title — big, with cyan outline glow for that "3D pop"
    // -----------------------------------------------------------------------
    lv_obj_t* title = lv_label_create(s_home);
    lv_label_set_text(title, "SiteSurvey Pro");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(ui_theme()->text), 0);
    lv_obj_set_style_text_outline_stroke_color(title, THM_ACCENT, 0);
    lv_obj_set_style_text_outline_stroke_width(title, 2, 0);
    lv_obj_set_style_text_outline_stroke_opa(title, LV_OPA_60, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    // 2×4 block grid — 7 blocks + 1 empty.
    // Block size 144×44, gap 16 horizontal / 8 vertical, left margin 12.
    const int X0 = 12;
    const int X1 = 172;   // 12 + 144 + 16
    const int Y0 = 36;
    const int Y1 = 88;    // 36 + 44 + 8
    const int Y2 = 140;   // 88 + 44 + 8
    const int Y3 = 192;   // 140 + 44 + 8

    // Wi-Fi — fresh green gradient
    make_block(s_home, "Wi-Fi",
               lv_color_hex(0x66BB6A), lv_color_hex(0x2E7D32),
               lv_color_hex(0x388E3C), lv_color_hex(0x1B5E20),
               &s_nav[0], X0, Y0, 144);

    // BLUETOOTH — electric cyan gradient
    make_block(s_home, "BLUETOOTH",
               lv_color_hex(0x26C6DA), lv_color_hex(0x00838F),
               lv_color_hex(0x0097A7), lv_color_hex(0x006064),
               &s_nav[1], X1, Y0, 144);

    // SPECTRUM — warm orange gradient
    make_block(s_home, "SPECTRUM",
               lv_color_hex(0xFFA726), lv_color_hex(0xEF6C00),
               lv_color_hex(0xF57C00), lv_color_hex(0xE65100),
               &s_nav[2], X0, Y1, 144);

    // ENVIRONMENT — sky blue gradient
    make_block(s_home, "ENVIRONMENT",
               lv_color_hex(0x42A5F5), lv_color_hex(0x1565C0),
               lv_color_hex(0x1976D2), lv_color_hex(0x0D47A1),
               &s_nav[3], X1, Y1, 144);

    // GPS — royal purple gradient
    make_block(s_home, "GPS",
               lv_color_hex(0xAB47BC), lv_color_hex(0x6A1B9A),
               lv_color_hex(0x8E24AA), lv_color_hex(0x4A148C),
               &s_nav[4], X0, Y2, 144);

    // ALERTS — alert red gradient
    make_block(s_home, "ALERTS",
               lv_color_hex(0xEF5350), lv_color_hex(0xC62828),
               lv_color_hex(0xD32F2F), lv_color_hex(0xB71C1C),
               &s_nav[5], X1, Y2, 144);

    // SETTINGS — gunmetal steel gradient, full width
    make_block(s_home, "SETTINGS",
               lv_color_hex(0x90A4AE), lv_color_hex(0x455A64),
               lv_color_hex(0x607D8B), lv_color_hex(0x263238),
               &s_nav[6], X0, Y3, 304);

    return s_home;
}

void ui_home_load(void)
{
    if (!nav_guard()) return;
    if (!s_home) s_home = ui_home_create();
    lv_screen_load(s_home);
    for (int i = 0; i < NAV_COUNT; i++) s_nav[i].set_visible(false);
}
