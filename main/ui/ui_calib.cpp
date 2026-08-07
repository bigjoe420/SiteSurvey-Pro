#include "ui_calib.h"

#include "touch.h"
#include "esp_log.h"
#include "lvgl.h"
#include <cstdio>

static const char* TAG = "calib";

// Targets sit 24 px in from the bezel; resistive film cannot reach the edge
static constexpr int16_t TARGETS[5][2] = {
    { 24,  24},   // top-left
    {296,  24},   // top-right
    { 24, 216},   // bottom-left
    {296, 216},   // bottom-right
    {160, 120},   // dead center
};
static constexpr const char* NAMES[5] = {"TL", "TR", "BL", "BR", "C"};

static lv_obj_t* s_raw_label;
static bool s_was_pressed;

static void add_target(int16_t cx, int16_t cy, const char* name)
{
    lv_obj_t* scr = lv_screen_active();

    lv_obj_t* dot = lv_obj_create(scr);
    lv_obj_set_size(dot, 18, 18);
    lv_obj_set_pos(dot, cx - 9, cy - 9);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_color(dot, lv_color_white(), 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* tag = lv_label_create(scr);
    lv_label_set_text(tag, name);
    lv_obj_set_style_text_color(tag, lv_color_white(), 0);
    // Right-edge targets get the label on their left so nothing exceeds 320 px
    lv_obj_set_pos(tag, cx > 200 ? cx - 40 : cx + 14, cy - 8);
}

static void poll_cb(lv_timer_t*)
{
    uint16_t rx = 0, ry = 0;
    bool pressed = touch_read_raw(&rx, &ry);
    if (pressed) {
        char buf[40];
        snprintf(buf, sizeof(buf), "RAW  X:%4u  Y:%4u", rx, ry);
        lv_label_set_text(s_raw_label, buf);
        ESP_LOGI(TAG, "raw x=%4u y=%4u%s", rx, ry, s_was_pressed ? "" : "  <- press start");
    }
    s_was_pressed = pressed;
}

void ui_calib_show(void)
{
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    // Root screens scroll by default; a calibration grid must not pan
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 5; i++) {
        add_target(TARGETS[i][0], TARGETS[i][1], NAMES[i]);
    }

    s_raw_label = lv_label_create(scr);
    lv_label_set_text(s_raw_label, "RAW  X:----  Y:----");
    lv_obj_set_style_text_color(s_raw_label, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_text_font(s_raw_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_raw_label, LV_ALIGN_CENTER, 0, 55);

    lv_timer_create(poll_cb, 100, nullptr);
}
