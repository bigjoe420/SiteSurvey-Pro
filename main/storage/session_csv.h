#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

// Shared helpers for reading SiteSurvey Pro session CSV files
// (/sdcard/survey_*.csv), used by the KML and report exporters.
//
// CSV columns: timestamp,mac,ssid,authmode,channel,rssi,lat,lon,sats,fix_q

typedef struct {
    char   ts[24];
    char   mac[20];
    char   ssid[44];
    char   auth[20];
    int    channel;
    int    rssi;
    double lat;
    double lon;
    int    sats;
} ApRec;

// Parse one CSV line, right-anchored so commas inside the SSID are safe.
// Truncated / malformed lines fail the field-count check and return false.
// line is modified in place. Returns false on malformed / partial rows.
bool csv_parse_row(char* line, ApRec* out);

// Find the newest non-empty survey_*.csv on the SD card (name embeds a
// sortable timestamp; zero-byte files — e.g. from a session interrupted
// before its first flush — are skipped). Writes the FULL path.
// Returns false if none found.
bool csv_find_latest(char* out_path, size_t n);

// Like csv_find_latest, but prefers the session the logger currently has
// open (path stored in /sdcard/current_session at session start). Falls
// back to the mtime scan when no pointer exists (e.g. export right after
// a reboot with no new session yet) — needed because no-RTC boards date
// every file 1970 and FAT cannot represent pre-1980 dates, so neither
// name nor timestamp order is reliable across boots.
bool csv_find_active(char* out_path, size_t n);

// One non-empty survey_*.csv found on the SD card.
typedef struct {
    char     name[40];    // file name only, e.g. "survey_19700101_000132.csv"
    uint32_t size;        // bytes
    time_t   mtime;       // for csv_list_sessions sorting only
    bool     is_active;   // matches /sdcard/current_session
} CsvSession;

// List non-empty survey_*.csv files on the SD card, newest first
// (mtime desc, file-name desc as tiebreak — same rule as csv_find_latest,
// with the same no-RTC caveat: across boots, "newest" is a heuristic).
// The currently-active session is flagged. out must hold max entries.
// Returns the number of sessions written (0 = no SD / none / bad args).
int csv_list_sessions(CsvSession* out, int max);

// Growable table of unique APs keyed by MAC, allocated from PSRAM.
typedef struct {
    ApRec* recs;
    int    n;
    int    cap;
} ApTable;

void   aptable_reset(ApTable* t);                        // keeps allocation
ApRec* aptable_add(ApTable* t, const ApRec* r);          // dedup by MAC; nullptr on OOM
void   aptable_free(ApTable* t);

// Format degrees with 7 decimal places WITHOUT newlib's float printf.
// %f pulls in the mprec formatter, whose stack appetite overflows small
// tasks (bench-verified: stack protection fault in a 4 KB task).
static inline void fmt_deg7_e7(char* buf, size_t n, long long e7)
{
    long long a = e7 >= 0 ? e7 : -e7;
    snprintf(buf, n, "%s%lld.%07llu", e7 < 0 ? "-" : "",
             a / 10000000LL, (unsigned long long)(a % 10000000LL));
}

static inline void fmt_deg7(char* buf, size_t n, double v)
{
    long long e7 = (long long)(v >= 0 ? v * 1e7 + 0.5 : v * 1e7 - 0.5);
    fmt_deg7_e7(buf, n, e7);
}
