#include "alert_engine.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "alert";
static const char* NVS_NS = "alert_cfg";

static AlertConfig_t s_cfg;
static QueueHandle_t s_queue;

// Circular log buffer for on-screen display
static AlertEntry_t s_log[ALERT_MAX_LOG_ENTRIES];
static int s_log_head = 0;   // next write position
static int s_log_count = 0;  // valid entries (0 … ALERT_MAX_LOG_ENTRIES)
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// Timestamp helper (same logic as session_logger)
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// NVS persistence
// ---------------------------------------------------------------------------
static void load_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.enabled = true;
    s_cfg.rssi_threshold = -80;
    // Demo target: FLEETNAV (visible in your environment)
    strncpy(s_cfg.ssid_targets[0], "FLEETNAV", sizeof(s_cfg.ssid_targets[0]) - 1);
    s_cfg.ssid_count = 1;
}

static esp_err_t nvs_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no NVS config found — using defaults");
        load_defaults();
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    size_t len = sizeof(s_cfg);
    err = nvs_get_blob(h, "cfg", &s_cfg, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof(s_cfg)) {
        ESP_LOGW(TAG, "NVS load failed or size mismatch — using defaults");
        load_defaults();
        return ESP_OK;
    }
    ESP_LOGI(TAG, "config loaded from NVS");
    return ESP_OK;
}

static esp_err_t nvs_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "cfg", &s_cfg, sizeof(s_cfg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t alert_engine_init(void)
{
    s_queue = xQueueCreate(ALERT_QUEUE_DEPTH, sizeof(AlertEntry_t));
    if (!s_queue) return ESP_ERR_NO_MEM;

    esp_err_t err = nvs_load();
    if (err != ESP_OK) load_defaults();

    ESP_LOGI(TAG, "alert engine up: enabled=%d ssid_count=%u rssi_th=%d",
             s_cfg.enabled, s_cfg.ssid_count, s_cfg.rssi_threshold);
    return ESP_OK;
}

QueueHandle_t alert_queue(void)
{
    return s_queue;
}

const AlertConfig_t* alert_config_get(void)
{
    return &s_cfg;
}

esp_err_t alert_config_set(const AlertConfig_t* cfg)
{
    taskENTER_CRITICAL(&s_log_mux);
    s_cfg = *cfg;
    taskEXIT_CRITICAL(&s_log_mux);
    return nvs_save();
}

void alert_log_clear(void)
{
    taskENTER_CRITICAL(&s_log_mux);
    s_log_head = 0;
    s_log_count = 0;
    taskEXIT_CRITICAL(&s_log_mux);
}

int alert_log_snapshot(AlertEntry_t* out, int max)
{
    if (max <= 0) return 0;
    if (max > ALERT_MAX_LOG_ENTRIES) max = ALERT_MAX_LOG_ENTRIES;

    taskENTER_CRITICAL(&s_log_mux);
    int count = (max < s_log_count) ? max : s_log_count;
    // Copy newest-first: head-1 is most recent
    for (int i = 0; i < count; i++) {
        int idx = (s_log_head - 1 - i + ALERT_MAX_LOG_ENTRIES) % ALERT_MAX_LOG_ENTRIES;
        out[i] = s_log[idx];
    }
    taskEXIT_CRITICAL(&s_log_mux);
    return count;
}

// ---------------------------------------------------------------------------
// Matching logic
// ---------------------------------------------------------------------------

static bool match_ssid(const char* ssid)
{
    for (int i = 0; i < s_cfg.ssid_count; i++) {
        if (strcasecmp(ssid, s_cfg.ssid_targets[i]) == 0) return true;
    }
    return false;
}

static bool match_bssid(const uint8_t* bssid)
{
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    for (int i = 0; i < s_cfg.bssid_count; i++) {
        if (strcasecmp(mac, s_cfg.bssid_targets[i]) == 0) return true;
    }
    return false;
}

static void log_alert(const AlertEntry_t* e)
{
    taskENTER_CRITICAL(&s_log_mux);
    s_log[s_log_head] = *e;
    s_log_head = (s_log_head + 1) % ALERT_MAX_LOG_ENTRIES;
    if (s_log_count < ALERT_MAX_LOG_ENTRIES) s_log_count++;
    taskEXIT_CRITICAL(&s_log_mux);
}

void alert_check(const ScanResult_t* ap, const GpsState* gps)
{
    if (!s_cfg.enabled) return;

    AlertEntry_t e = {};
    fmt_ts(e.timestamp, sizeof(e.timestamp), gps);
    e.rssi = ap->rssi;
    e.channel = ap->channel;
    e.lat = (gps && gps->fix_valid) ? (gps->lat_e7 / 1e7f) : 0.0f;
    e.lon = (gps && gps->fix_valid) ? (gps->lon_e7 / 1e7f) : 0.0f;
    e.sats = (gps && gps->fix_valid) ? gps->sats : 0;

    bool matched = false;

    // SSID match
    const char* ssid = (const char*)ap->ssid;
    if (s_cfg.ssid_count > 0 && match_ssid(ssid)) {
        matched = true;
        e.match_type = ALERT_MATCH_SSID;
        snprintf(e.target, sizeof(e.target), "%s", ssid[0] ? ssid : "<hidden>");
    }

    // BSSID match (only if no SSID match — one alert per AP)
    if (!matched && s_cfg.bssid_count > 0 && match_bssid(ap->bssid)) {
        matched = true;
        e.match_type = ALERT_MATCH_BSSID;
        snprintf(e.target, sizeof(e.target), "%02X:%02X:%02X:%02X:%02X:%02X",
                 ap->bssid[0], ap->bssid[1], ap->bssid[2],
                 ap->bssid[3], ap->bssid[4], ap->bssid[5]);
    }

    // RSSI threshold (only if no specific target match)
    if (!matched && ap->rssi >= s_cfg.rssi_threshold) {
        matched = true;
        e.match_type = ALERT_MATCH_RSSI;
        snprintf(e.target, sizeof(e.target), "%s", ssid[0] ? ssid : "<hidden>");
    }

    if (matched) {
        log_alert(&e);
        xQueueSend(s_queue, &e, 0);  // non-blocking; drop if queue full
        ESP_LOGI(TAG, "ALERT: %s | %s | ch%u | %d dBm | type=%u",
                 e.timestamp, e.target, e.channel, e.rssi, (unsigned)e.match_type);
    }
}
