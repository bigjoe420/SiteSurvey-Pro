#pragma once

// =============================================================================
// SiteSurvey Pro — Shared formatting helpers
// Single definition for helpers that used to be copy-pasted across modules.
// =============================================================================

#include <cstdio>
#include <ctime>
#include <cstddef>
#include "gps.h"

// Build a "YYYY-MM-DD HH:MM:SS" timestamp. If gps is valid, the time portion
// is overridden with GPS HHMMSS so logs stay correct even when the system
// clock has no real-world source (no SNTP).
static inline void fmt_ts(char* out, size_t n, const GpsState* gps)
{
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);

    if (gps && gps->fix_valid && gps->utc_hhmmss > 0) {
        uint32_t t = gps->utc_hhmmss;
        int hh = t / 10000;
        int mm = (t / 100) % 100;
        int ss = t % 100;
        snprintf(out, n, "%04d-%02d-%02d %02d:%02d:%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 hh, mm, ss);
    } else {
        strftime(out, n, "%Y-%m-%d %H:%M:%S", &tm);
    }
}
