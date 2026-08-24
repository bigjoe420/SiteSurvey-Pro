#pragma once

#include "lvgl.h"

// Wireframe 3D polyhedron rendered into a small LVGL canvas: vertices are
// rotated in 3D each frame, perspective-projected, and drawn as 2D lines.
// Spin speed and edge color are driven by the parent gauge's live value.

typedef enum {
    SPIN_TETRA,     //  4 vertices,  6 edges
    SPIN_CUBE,      //  8 vertices, 12 edges
    SPIN_OCTA,      //  6 vertices, 12 edges
    SPIN_ICOSA,     // 12 vertices, 30 edges
} spin3d_shape_t;

typedef struct Spin3D Spin3D;

// Creates a size x size spinner at (x, y). Returns NULL if PSRAM is short;
// every other call tolerates NULL so a missing spinner is never fatal.
Spin3D* ui_spin3d_create(lv_obj_t* parent, int x, int y, int size, spin3d_shape_t shape);

// frac 0..1 maps to spin speed; color sets the wireframe edge color.
void ui_spin3d_set(Spin3D* s, float frac, lv_color_t color);

// Master gate: when off, the frame timer renders nothing (hidden tab costs 0 CPU).
void ui_spin3d_enable(bool on);
