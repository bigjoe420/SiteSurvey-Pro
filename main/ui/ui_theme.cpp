#include "ui_theme.h"

// Indoor palette — the original SiteSurvey Pro look (dark, cyan accent).
static const ssp_palette_t s_pal_indoor = {
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

// Outdoor palette — daylight light theme. Direct sunlight kills emissive
// dark UIs; black-on-white with darkened accents/tiers is the readable
// configuration. Every screen is palette-driven, so these values re-theme
// the whole app at boot.
static const ssp_palette_t s_pal_outdoor = {
    .bg     = 0xFFFFFF,
    .row    = 0xE4E4E4,
    .track  = 0x303030,
    .btn    = 0xD2D2D2,
    .grid   = 0xB0B0B0,
    .border = 0x8A8A8A,
    .text   = 0x000000,
    .label  = 0x1A1A1A,
    .sub    = 0x3A3A3A,
    .faint  = 0x5E5E5E,
    .accent = 0xFF8F00,
    .ok       = 0x1B8A3C,
    .ok_mid   = 0x558B2F,
    .ok_hi    = 0x00A152,
    .warn     = 0xF9A825,
    .yellow   = 0xF9A825,
    .orange   = 0xEF6C00,
    .bad      = 0xC62828,
    .bad_soft = 0xD32F2F,
    .cyan  = 0x00838F,
    .blue  = 0x1565C0,
    .blue2 = 0x1565C0,
    .teal  = 0x00695C,
    .btn_ok   = 0x2E7D32,
    .btn_del  = 0xB71C1C,
    .btn_blue = 0x1F4E79,
};

static bool s_outdoor;

void ui_theme_set_outdoor(bool outdoor)
{
    s_outdoor = outdoor;
}

bool ui_theme_outdoor(void)
{
    return s_outdoor;
}

const ssp_palette_t* ui_theme(void)
{
    return s_outdoor ? &s_pal_outdoor : &s_pal_indoor;
}
