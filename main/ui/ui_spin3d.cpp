#include "ui_spin3d.h"

#include <cmath>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"

#define PHI 1.6180339887f

// Unit-shape vertex tables; edges are derived at init by pairwise distance,
// so each shape only needs its corners defined here.
static const float TETRA_V[][3] = {
    {1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1},
};
static const float CUBE_V[][3] = {
    {1, 1, 1}, {1, 1, -1}, {1, -1, 1}, {1, -1, -1},
    {-1, 1, 1}, {-1, 1, -1}, {-1, -1, 1}, {-1, -1, -1},
};
static const float OCTA_V[][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};
static const float ICOSA_V[][3] = {
    {0, 1, PHI}, {0, 1, -PHI}, {0, -1, PHI}, {0, -1, -PHI},
    {1, PHI, 0}, {1, -PHI, 0}, {-1, PHI, 0}, {-1, -PHI, 0},
    {PHI, 0, 1}, {-PHI, 0, 1}, {PHI, 0, -1}, {-PHI, 0, -1},
};

// Squared edge length between neighbouring vertices per shape
typedef struct {
    const float (*verts)[3];
    int nverts;
    float edge_len2;
} ShapeDef;

static const ShapeDef SHAPES[] = {
    { TETRA_V,  4, 8.0f },
    { CUBE_V,   8, 4.0f },
    { OCTA_V,   6, 2.0f },
    { ICOSA_V, 12, 4.0f },
};

#define MAX_EDGES 30
#define MAX_SPINNERS 4

struct Spin3D {
    lv_obj_t* canvas;
    uint8_t* buf;
    int size;
    const ShapeDef* shape;
    uint8_t edges[MAX_EDGES][2];
    int nedges;
    float scale;            // fits the shape's radius into the canvas
    float angle;            // Y-rotation, advanced per frame
    float speed;            // rad/frame, from gauge frac
    lv_color_t color;       // edge color, from gauge band
    bool active;
};

static Spin3D s_pool[MAX_SPINNERS];
static lv_timer_t* s_timer;
static bool s_enabled = true;
static const char* TAG = "spin3d";

static void build_edges(Spin3D* s)
{
    const ShapeDef* sh = s->shape;
    float max_r = 0.0f;
    for (int i = 0; i < sh->nverts; i++) {
        float r = sqrtf(sh->verts[i][0] * sh->verts[i][0] +
                        sh->verts[i][1] * sh->verts[i][1] +
                        sh->verts[i][2] * sh->verts[i][2]);
        if (r > max_r) max_r = r;
    }
    s->scale = (s->size / 2.0f - 4.0f) / max_r;

    int e = 0;
    for (int i = 0; i < sh->nverts && e < MAX_EDGES; i++) {
        for (int j = i + 1; j < sh->nverts && e < MAX_EDGES; j++) {
            float dx = sh->verts[i][0] - sh->verts[j][0];
            float dy = sh->verts[i][1] - sh->verts[j][1];
            float dz = sh->verts[i][2] - sh->verts[j][2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (fabsf(d2 - sh->edge_len2) < 0.01f) {
                s->edges[e][0] = (uint8_t)i;
                s->edges[e][1] = (uint8_t)j;
                e++;
            }
        }
    }
    s->nedges = e;
}

static void render(Spin3D* s)
{
    const ShapeDef* sh = s->shape;
    const float DIST = 3.0f;    // perspective distance in shape radii
    float cx = s->size / 2.0f, cy = s->size / 2.0f;

    // Tilt wobbles slowly so the rotation reads as tumbling, not flat spinning
    float tilt = 0.55f + 0.30f * sinf(s->angle * 0.63f);
    float cy_ = cosf(s->angle), sy_ = sinf(s->angle);
    float ct = cosf(tilt), st = sinf(tilt);

    float px[12], py[12];
    for (int i = 0; i < sh->nverts; i++) {
        float x = sh->verts[i][0], y = sh->verts[i][1], z = sh->verts[i][2];
        float x1 = x * cy_ + z * sy_;          // rotate about Y
        float z1 = -x * sy_ + z * cy_;
        float y2 = y * ct - z1 * st;           // rotate about X (tilt)
        float z2 = y * st + z1 * ct;
        float persp = DIST / (z2 + DIST);
        px[i] = cx + x1 * s->scale * persp;
        py[i] = cy + y2 * s->scale * persp;
    }

    lv_canvas_fill_bg(s->canvas, lv_color_black(), LV_OPA_TRANSP);

    lv_layer_t layer;
    lv_canvas_init_layer(s->canvas, &layer);
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = s->color;
    dsc.width = 2;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1;
    dsc.round_end = 1;
    for (int e = 0; e < s->nedges; e++) {
        dsc.p1.x = px[s->edges[e][0]];
        dsc.p1.y = py[s->edges[e][0]];
        dsc.p2.x = px[s->edges[e][1]];
        dsc.p2.y = py[s->edges[e][1]];
        lv_draw_line(&layer, &dsc);
    }
    lv_canvas_finish_layer(s->canvas, &layer);
}

static void tick(lv_timer_t*)
{
    if (!s_enabled) return;
    for (int i = 0; i < MAX_SPINNERS; i++) {
        Spin3D* s = &s_pool[i];
        if (!s->active) continue;
        s->angle += s->speed;
        render(s);
    }
}

Spin3D* ui_spin3d_create(lv_obj_t* parent, int x, int y, int size, spin3d_shape_t shape)
{
    Spin3D* s = nullptr;
    for (int i = 0; i < MAX_SPINNERS; i++) {
        if (!s_pool[i].active) { s = &s_pool[i]; break; }
    }
    if (!s) return nullptr;

    s->buf = (uint8_t*)heap_caps_malloc((size_t)size * size * 4, MALLOC_CAP_SPIRAM);
    if (!s->buf) {
        ESP_LOGW(TAG, "PSRAM short, spinner skipped");
        return nullptr;
    }

    s->size = size;
    s->shape = &SHAPES[shape];
    s->angle = 0.0f;
    s->speed = 0.02f;
    s->color = lv_color_hex(0x757575);
    build_edges(s);

    s->canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(s->canvas, s->buf, size, size, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(s->canvas, x, y);
    s->active = true;
    render(s);

    if (!s_timer) s_timer = lv_timer_create(tick, 100, nullptr);
    return s;
}

void ui_spin3d_enable(bool on)
{
    s_enabled = on;
    if (s_timer) {
        if (on)
            lv_timer_resume(s_timer);
        else
            lv_timer_pause(s_timer);
    }
}

void ui_spin3d_set(Spin3D* s, float frac, lv_color_t color)
{
    if (!s) return;
    s->speed = 0.02f + frac * 0.10f;    // idle drift -> lively spin
    s->color = color;
}
