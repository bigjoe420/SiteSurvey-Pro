#include "ui_hello.h"

#include "lvgl.h"
#include <cstdio>

static lv_obj_t* s_coord_label;

static void on_touch(lv_event_t* e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    char buf[32];
    snprintf(buf, sizeof(buf), "touch: %d, %d", (int)p.x, (int)p.y);
    lv_label_set_text(s_coord_label, buf);
}

void ui_hello_show(void)
{
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "SiteSurvey Pro");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "UI pipeline OK - tap the screen");
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);

    s_coord_label = lv_label_create(scr);
    lv_label_set_text(s_coord_label, "touch: —");
    lv_obj_set_style_text_color(s_coord_label, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(s_coord_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_PRESSING, nullptr);
}
