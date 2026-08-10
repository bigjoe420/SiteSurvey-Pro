#include "ui_env.h"

#include <cstdio>
#include <cstdlib>

// -----------------------------------------------------------------------------
// Gauge model
// All gauge math runs in centi-units of the displayed quantity (e.g. 0.01 F).
// Bands are cumulative end fractions along the scale, last one always 1.0.
// -----------------------------------------------------------------------------

typedef struct {
    float end;          // cumulative end of this band, 0..1
    lv_color_t color;
} Band;

typedef struct {
    const char* name = nullptr;
    const Band* bands = nullptr;    // nullptr = smooth two-color gradient track, no alarm semantics
    int nbands = 0;
    lv_color_t grad_a = {}, grad_b = {};
    int32_t min_x100 = 0, max_x100 = 0;
    void (*fmt)(char* buf, size_t n, int32_t v_x100) = nullptr;

    lv_obj_t* value_label = nullptr;
    lv_obj_t* marker = nullptr;
    int track_x = 0, track_w = 0;
    int32_t shown_x100 = 0;
    bool has_value = false;
} Gauge;

// Equipment-grade band tables (IT hardware environment, not human comfort)
static const Band TEMP_BANDS[] = {          // 32..122 F
    {0.300f, lv_color_hex(0x2196F3)},       // < 59 F   cool
    {0.556f, lv_color_hex(0x4CAF50)},       // 59..82   nominal
    {0.700f, lv_color_hex(0xFFC107)},       // 82..95   warm
    {1.000f, lv_color_hex(0xF44336)},       // > 95     hot
};
static const Band HUM_BANDS[] = {           // 0..100 %RH
    {0.300f, lv_color_hex(0xFFC107)},       // < 30     static risk
    {0.600f, lv_color_hex(0x4CAF50)},       // 30..60   nominal
    {0.750f, lv_color_hex(0xFFC107)},       // 60..75   damp
    {1.000f, lv_color_hex(0xF44336)},       // > 75     condensation risk
};
static const Band VOC_BANDS[] = {           // 0..150 kOhm, higher = cleaner air
    {0.067f, lv_color_hex(0xF44336)},       // < 10 k   poor
    {0.333f, lv_color_hex(0xFFC107)},       // 10..50 k fair
    {1.000f, lv_color_hex(0x4CAF50)},       // > 50 k   good
};

static void fmt_tenths(char* b, size_t n, int32_t v, const char* unit)
{
    snprintf(b, n, "%ld.%ld %s", (long)(v / 100), (long)(labs(v) / 10 % 10), unit);
}
static void fmt_temp(char* b, size_t n, int32_t v)  { fmt_tenths(b, n, v, "F"); }
static void fmt_hum(char* b, size_t n, int32_t v)   { fmt_tenths(b, n, v, "%"); }
static void fmt_voc(char* b, size_t n, int32_t v)   { fmt_tenths(b, n, v, "k"); }
static void fmt_press(char* b, size_t n, int32_t v) { snprintf(b, n, "%ld hPa", (long)(v / 100)); }

static Gauge s_gauges[] = {
    { .name = "TEMP",     .bands = TEMP_BANDS, .nbands = 4,
      .min_x100 = 3200,  .max_x100 = 12200,  .fmt = fmt_temp  },
    { .name = "HUMIDITY", .bands = HUM_BANDS,  .nbands = 4,
      .min_x100 = 0,     .max_x100 = 10000,  .fmt = fmt_hum   },
    { .name = "PRESSURE", .bands = nullptr,    .nbands = 0,
      .grad_a = lv_color_hex(0x1DE9B6), .grad_b = lv_color_hex(0x2979FF),
      .min_x100 = 95000, .max_x100 = 105000, .fmt = fmt_press },
    { .name = "AIR (VOC)", .bands = VOC_BANDS, .nbands = 3,
      .min_x100 = 0,     .max_x100 = 15000,  .fmt = fmt_voc   },
};
static constexpr int N_GAUGES = sizeof(s_gauges) / sizeof(s_gauges[0]);

// Snapshot handoff from main_task (spinlock, same pattern as the rest of the UI)
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static EnvSnapshot_t s_latest;
static bool s_has_snapshot;
static bool s_bme680_present;

static lv_obj_t* s_status;

void ui_env_post_env(const EnvSnapshot_t* snap, bool bme680_present)
{
    taskENTER_CRITICAL(&s_mux);
    s_latest = *snap;
    s_has_snapshot = true;
    s_bme680_present = bme680_present;
    taskEXIT_CRITICAL(&s_mux);
}

static float gauge_frac(const Gauge* g, int32_t v_x100)
{
    float f = (float)(v_x100 - g->min_x100) / (float)(g->max_x100 - g->min_x100);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return f;
}

static lv_color_t band_color(const Gauge* g, float frac)
{
    if (!g->bands) return g->grad_a;
    for (int i = 0; i < g->nbands; i++) {
        if (frac <= g->bands[i].end) return g->bands[i].color;
    }
    return g->bands[g->nbands - 1].color;
}

// Value tween: re-render the number every animation frame
static void value_anim_cb(void* var, int32_t v)
{
    Gauge* g = (Gauge*)var;
    g->shown_x100 = v;
    char buf[24];
    g->fmt(buf, sizeof(buf), v);
    lv_label_set_text(g->value_label, buf);
}

// Marker travel: ease the position indicator along the scale
static void marker_anim_cb(void* var, int32_t x)
{
    Gauge* g = (Gauge*)var;
    lv_obj_set_x(g->marker, x);
}

static void gauge_set(Gauge* g, int32_t v_x100)
{
    if (g->has_value && g->shown_x100 == v_x100) return;
    float frac = gauge_frac(g, v_x100);
    int target_x = g->track_x + (int)(frac * g->track_w) - 2;
    if (!g->has_value) {
        g->has_value = true;
        g->shown_x100 = v_x100;
        value_anim_cb(g, v_x100);
        marker_anim_cb(g, target_x);
    } else {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, g);
        lv_anim_set_duration(&a, 900);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_values(&a, g->shown_x100, v_x100);
        lv_anim_set_exec_cb(&a, value_anim_cb);
        lv_anim_start(&a);
        lv_anim_set_values(&a, lv_obj_get_x(g->marker), target_x);
        lv_anim_set_exec_cb(&a, marker_anim_cb);
        lv_anim_start(&a);
    }
    lv_obj_set_style_text_color(g->value_label, band_color(g, frac), 0);
}

static void gauge_clear(Gauge* g)
{
    g->has_value = false;
    lv_label_set_text(g->value_label, "--");
    lv_obj_set_style_text_color(g->value_label, lv_color_hex(0x757575), 0);
}

static void env_refresh(lv_timer_t*)
{
    taskENTER_CRITICAL(&s_mux);
    EnvSnapshot_t snap = s_latest;
    bool have = s_has_snapshot;
    bool present = s_bme680_present;
    taskEXIT_CRITICAL(&s_mux);

    if (!present) {
        lv_label_set_text(s_status, "BME680 OFFLINE");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0xF44336), 0);
        for (int i = 0; i < N_GAUGES; i++) gauge_clear(&s_gauges[i]);
        return;
    }
    if (!have || !snap.env_valid) {
        lv_label_set_text(s_status, "WAITING FOR SAMPLE");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0xFFC107), 0);
        for (int i = 0; i < N_GAUGES; i++) gauge_clear(&s_gauges[i]);
        return;
    }

    lv_label_set_text(s_status, "LIVE");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x4CAF50), 0);
    gauge_set(&s_gauges[0], snap.env.temp_c_x100 * 9 / 5 + 3200);
    gauge_set(&s_gauges[1], (int32_t)snap.env.hum_x100);
    gauge_set(&s_gauges[2], (int32_t)snap.env.press_pa);      // Pa == centi-hPa
    gauge_set(&s_gauges[3], (int32_t)(snap.env.gas_ohm / 10)); // -> centi-kOhm
}

// One gauge cell: name, big value, banded track + marker, min/max end labels
static void build_gauge(lv_obj_t* scr, Gauge* g, int x, int y)
{
    const int W = 140, TRACK_H = 12, TRACK_Y = y + 58;

    lv_obj_t* name = lv_label_create(scr);
    lv_label_set_text(name, g->name);
    lv_obj_set_style_text_color(name, lv_color_hex(0x9E9E9E), 0);
    lv_obj_set_pos(name, x, y);

    g->value_label = lv_label_create(scr);
    lv_label_set_text(g->value_label, "--");
    lv_obj_set_style_text_font(g->value_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g->value_label, lv_color_hex(0x757575), 0);
    lv_obj_set_pos(g->value_label, x, y + 16);

    lv_obj_t* track = lv_obj_create(scr);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(track, W, TRACK_H);
    lv_obj_set_pos(track, x, TRACK_Y);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_set_style_radius(track, 0, 0);
    if (g->bands) {
        lv_obj_set_style_bg_color(track, lv_color_hex(0x1E1E1E), 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        float start = 0.0f;
        for (int i = 0; i < g->nbands; i++) {
            lv_obj_t* seg = lv_obj_create(track);
            lv_obj_remove_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
            int sx = (int)(start * W);
            int sw = (int)(g->bands[i].end * W) - sx;
            lv_obj_set_size(seg, sw, TRACK_H);
            lv_obj_set_pos(seg, sx, 0);
            lv_obj_set_style_bg_color(seg, g->bands[i].color, 0);
            lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(seg, 0, 0);
            lv_obj_set_style_radius(seg, 0, 0);
            start = g->bands[i].end;
        }
    } else {
        lv_obj_set_style_bg_color(track, g->grad_a, 0);
        lv_obj_set_style_bg_grad_color(track, g->grad_b, 0);
        lv_obj_set_style_bg_grad_dir(track, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    }

    g->track_x = x;
    g->track_w = W;
    g->marker = lv_obj_create(scr);
    lv_obj_remove_flag(g->marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g->marker, 4, TRACK_H + 8);
    lv_obj_set_style_bg_color(g->marker, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(g->marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g->marker, 0, 0);
    lv_obj_set_style_radius(g->marker, 2, 0);
    lv_obj_set_pos(g->marker, x - 2, TRACK_Y - 4);

    char buf[24];
    lv_obj_t* lo = lv_label_create(scr);
    g->fmt(buf, sizeof(buf), g->min_x100);
    lv_label_set_text(lo, buf);
    lv_obj_set_style_text_color(lo, lv_color_hex(0x616161), 0);
    lv_obj_set_pos(lo, x, TRACK_Y + TRACK_H + 4);

    lv_obj_t* hi = lv_label_create(scr);
    g->fmt(buf, sizeof(buf), g->max_x100);
    lv_label_set_text(hi, buf);
    lv_obj_set_style_text_color(hi, lv_color_hex(0x616161), 0);
    lv_obj_set_pos(hi, x + W - 48, TRACK_Y + TRACK_H + 4);
}

lv_obj_t* ui_env_create(void)
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "ENVIRONMENT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_pos(title, 12, 8);

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "WAITING FOR SAMPLE");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xFFC107), 0);
    lv_obj_set_pos(s_status, 198, 14);

    build_gauge(scr, &s_gauges[0], 12, 44);
    build_gauge(scr, &s_gauges[1], 168, 44);
    build_gauge(scr, &s_gauges[2], 12, 146);
    build_gauge(scr, &s_gauges[3], 168, 146);

    lv_timer_create(env_refresh, 500, nullptr);
    return scr;
}
