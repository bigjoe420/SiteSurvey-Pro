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

// Outdoor palette — high contrast for direct sunlight: grays pushed toward
// white, amber accent (reads better than cyan in daylight), saturated tiers.
static const ssp_palette_t s_pal_outdoor = {
    .bg     = 0x000000,
    .row    = 0x2E2E2E,
    .track  = 0x2E2E2E,
    .btn    = 0x505050,
    .grid   = 0x5C5C5C,
    .border = 0x4A4A4A,
    .text   = 0xFFFFFF,
    .label  = 0xE8E8E8,
    .sub    = 0xCFCFCF,
    .faint  = 0xA8A8A8,
    .accent = 0xFFD600,
    .ok       = 0x00E676,
    .ok_mid   = 0x76FF03,
    .ok_hi    = 0x00E676,
    .warn     = 0xFFD600,
    .yellow   = 0xFFEA00,
    .orange   = 0xFF9100,
    .bad      = 0xFF1744,
    .bad_soft = 0xFF5252,
    .cyan  = 0x00E5FF,
    .blue  = 0x448AFF,
    .blue2 = 0x448AFF,
    .teal  = 0x64FFDA,
    .btn_ok   = 0x00A152,
    .btn_del  = 0xD50000,
    .btn_blue = 0x1565C0,
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
