#include "ui_theme.h"

// Single dark high-contrast palette — the original SiteSurvey Pro look
// (dark, cyan accent). Owner decision 2026-09-12: the outdoor light theme
// was removed (visibility regressions, no owner value); the palette stays
// struct theme-driven so every screen reads colors from one place.
static const ssp_palette_t s_palette = {
    .bg     = 0x000000,
    .row    = 0x1A1A1A,
    .track  = 0x1E1E1E,
    .btn    = 0x333333,
    .grid   = 0x3A3A3A,
    .border = 0x222222,
    .text   = 0xE8E8E8,
    .label  = 0xB0B0B0,
    .sub    = 0x909090,
    .faint  = 0x757575,
    .accent = 0x00E5FF,
    .ok       = 0x4CAF50,
    .ok_mid   = 0x8BC34A,
    .ok_hi    = 0x00E676,
    .warn     = 0xFFC107,
    .yellow   = 0xFFEB3B,
    .orange   = 0xFF9800,
    .bad      = 0xF44336,
    .bad_soft = 0xEF5350,
    .cyan  = 0x00BCD4,
    .blue  = 0x2196F3,
    .blue2 = 0x2979FF,
    .teal  = 0x1DE9B6,
    .btn_ok   = 0x2E7D32,
    .btn_del  = 0xB71C1C,
    .btn_blue = 0x1F4E79,
};

const ssp_palette_t* ui_theme(void)
{
    return &s_palette;
}
