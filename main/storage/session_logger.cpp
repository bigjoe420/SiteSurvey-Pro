#include "session_logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include "sd_card.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "session";
#define SD_PREFIX  "/sdcard"
#define BUF_ENTRIES 16
#define FLUSH_MS    5000

struct LogEntry {
    char ts[20];          // YYYY-MM-DD HH:MM:SS
    char mac[18];
    char ssid[33];
    char auth[16];
    uint8_t channel;
    int8_t  rssi;
    float   lat;
    float   lon;
    uint8_t sats;
    uint8_t fix_q;
};

static LogEntry s_buf[BUF_ENTRIES];
static int      s_buf_n;
static FILE*    s_f;
static bool     s_active;
static char     s_path[64];
static TickType_t s_last_flush;

// Last-known-good GPS position — survives brief fix dropouts during survey walks.
static GpsState s_last_fix = {};

// Format BSSID as AA:BB:CC:DD:EE:FF

// Format BSSID as AA:BB:CC:DD:EE:FF
static void fmt_mac(char* out, const uint8_t* b)
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             b[0], b[1], b[2], b[3], b[4], b[5]);
}

// Build a YYYY-MM-DD HH:MM:SS timestamp.  If gps is valid, override the
// time portion with GPS HHMMSS so the log stays correct even when the
// system clock has no real-world source (no SNTP yet).
static void fmt_ts(char* out, size_t n, const GpsState* gps)
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

static const char* fmt_auth(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:            return "Open";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    case WIFI_AUTH_OWE:             return "OWE";
    default:                        return "Other";
    }
}

static void write_buffer(void)
{
    if (!s_f || s_buf_n == 0) return;
    for (int i = 0; i < s_buf_n; i++) {
        const LogEntry* e = &s_buf[i];
        fprintf(s_f, "%s,%s,%s,%s,%u,%d,%.7f,%.7f,%u,%u\n",
                e->ts, e->mac, e->ssid, e->auth,
                e->channel, e->rssi, e->lat, e->lon,
                e->sats, e->fix_q);
    }
    fflush(s_f);
    s_buf_n = 0;
    s_last_flush = xTaskGetTickCount();
    ESP_LOGI(TAG, "flushed %d entries to %s", s_buf_n, s_path);
}

esp_err_t session_logger_init(void)
{
    if (!sd_card_present()) {
        ESP_LOGW(TAG, "SD absent — session logging disabled");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

void session_logger_start(void)
{
    if (s_active || !sd_card_present()) return;

    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(s_path, sizeof(s_path),
             SD_PREFIX "/survey_%04d%02d%02d_%02d%02d%02d.csv",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);

    s_f = fopen(s_path, "w");
    if (!s_f) {
        ESP_LOGW(TAG, "fopen failed: %s", s_path);
        return;
    }

    fprintf(s_f, "timestamp,mac,ssid,authmode,channel,rssi,lat,lon,sats,fix_q\n");
    fflush(s_f);
    s_active = true;
    s_buf_n = 0;
    s_last_flush = xTaskGetTickCount();
    ESP_LOGI(TAG, "session started: %s", s_path);
}

void session_logger_stop(void)
{
    if (!s_active) return;
    write_buffer();
    if (s_f) {
        fclose(s_f);
        s_f = nullptr;
    }
    s_active = false;
    ESP_LOGI(TAG, "session stopped: %s", s_path);
}

void session_logger_log_ap(const ScanResult_t* ap, const GpsState* gps)
{
    if (!sd_card_present()) return;
    if (!s_active) session_logger_start();
    if (!s_active) return;  // start() may fail

    // Time-based auto-flush
    if (s_buf_n >= BUF_ENTRIES ||
        (xTaskGetTickCount() - s_last_flush) > pdMS_TO_TICKS(FLUSH_MS)) {
        write_buffer();
    }

    LogEntry* e = &s_buf[s_buf_n++];
    fmt_ts(e->ts, sizeof(e->ts), gps);
    fmt_mac(e->mac, ap->bssid);
    snprintf(e->ssid, sizeof(e->ssid), "%s", ap->ssid[0] ? (const char*)ap->ssid : "<hidden>");
    snprintf(e->auth, sizeof(e->auth), "%s", fmt_auth(ap->authmode));
    e->channel = ap->channel;
    e->rssi = ap->rssi;

    // Use current fix if valid; otherwise fall back to last-known-good position.
    const GpsState* fix = nullptr;
    if (gps && gps->fix_valid) {
        s_last_fix = *gps;           // cache the good fix
        fix = gps;
    } else if (s_last_fix.fix_valid) {
        fix = &s_last_fix;           // brief dropout — use cached position
    }
    if (fix) {
        e->lat  = fix->lat_e7 / 1e7f;
        e->lon  = fix->lon_e7 / 1e7f;
        e->sats = fix->sats;
        e->fix_q = (fix == gps) ? fix->fix_quality : 0; // 0 = cached / no current fix
    } else {
        e->lat = e->lon = 0.0f;
        e->sats = 0;
        e->fix_q = 0;
    }
    fmt_ts(e->ts, sizeof(e->ts), gps);
    fmt_mac(e->mac, ap->bssid);
    snprintf(e->ssid, sizeof(e->ssid), "%s", ap->ssid[0] ? (const char*)ap->ssid : "<hidden>");
    snprintf(e->auth, sizeof(e->auth), "%s", fmt_auth(ap->authmode));
    e->channel = ap->channel;
    e->rssi = ap->rssi;
    e->lat = (gps && gps->fix_valid) ? (gps->lat_e7 / 1e7f) : 0.0f;
    e->lon = (gps && gps->fix_valid) ? (gps->lon_e7 / 1e7f) : 0.0f;
    e->sats = (gps && gps->fix_valid) ? gps->sats : 0;
    e->fix_q = (gps && gps->fix_valid) ? gps->fix_quality : 0;
}

void session_logger_flush(void)
{
    if (!s_active) return;
    write_buffer();
}

bool session_logger_active(void)
{
    return s_active;
}
