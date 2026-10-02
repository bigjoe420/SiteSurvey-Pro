#include "scan_engine.h"
#include "flash_broker.h"

#include <cstring>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/task.h"

static const char* TAG = "scan_engine";

#define SSP_WIFI_COUNTRY "US"

static constexpr size_t POOL_SIZE = 64;
static constexpr uint16_t SCAN_BUF_MAX = 64;
static constexpr uint32_t SCAN_INTERVAL_MS = 5000;
static constexpr UBaseType_t SCAN_TASK_STACK = 3072;
static constexpr UBaseType_t SCAN_TASK_PRIO = 4;

// US 5 GHz channels (bit0 = 0 for bitmap mode, bits 1-25 = channels 36-165)
static constexpr uint32_t US_5G_CHANNEL_MASK =
    WIFI_CHANNEL_36 | WIFI_CHANNEL_40 | WIFI_CHANNEL_44 | WIFI_CHANNEL_48 |
    WIFI_CHANNEL_52 | WIFI_CHANNEL_56 | WIFI_CHANNEL_60 | WIFI_CHANNEL_64 |
    WIFI_CHANNEL_100 | WIFI_CHANNEL_104 | WIFI_CHANNEL_108 | WIFI_CHANNEL_112 |
    WIFI_CHANNEL_116 | WIFI_CHANNEL_120 | WIFI_CHANNEL_124 | WIFI_CHANNEL_128 |
    WIFI_CHANNEL_132 | WIFI_CHANNEL_136 | WIFI_CHANNEL_140 | WIFI_CHANNEL_144 |
    WIFI_CHANNEL_149 | WIFI_CHANNEL_153 | WIFI_CHANNEL_157 | WIFI_CHANNEL_161 |
    WIFI_CHANNEL_165;

// 2.4 GHz channels 1-14 (bit0 = 0 for bitmap mode)
static constexpr uint16_t ALL_2G_CHANNEL_MASK = 0x7FFE;

typedef struct {
    int8_t samples[RSSI_HISTORY_LEN];
    uint8_t head;
    uint8_t count;
} RssiHistory_t;

typedef struct {
    ScanResult_t ap;
    TickType_t last_seen;
    bool used;
    RssiHistory_t history;
} PoolEntry;

static PoolEntry s_pool[POOL_SIZE];
static portMUX_TYPE s_pool_mux = portMUX_INITIALIZER_UNLOCKED;
static wifi_ap_record_t s_scan_buf[SCAN_BUF_MAX];
static QueueHandle_t s_queue;
static uint32_t s_evictions;
static uint32_t s_drops;

static ssp_rssi_tier_t classify(int8_t rssi)
{
    if (rssi >= SSP_RSSI_STRONG_DBM)   return SSP_RSSI_STRONG;
    if (rssi >= SSP_RSSI_MODERATE_DBM) return SSP_RSSI_MODERATE;
    if (rssi >= SSP_RSSI_WEAK_DBM)     return SSP_RSSI_WEAK;
    return SSP_RSSI_MARGINAL;
}

static void history_append(RssiHistory_t* h, int8_t rssi)
{
    h->samples[h->head] = rssi;
    h->head = (h->head + 1) % RSSI_HISTORY_LEN;
    if (h->count < RSSI_HISTORY_LEN) h->count++;
}

const char* scan_engine_auth_str(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:           return "Open";
    case WIFI_AUTH_WEP:            return "WEP";
    case WIFI_AUTH_WPA_PSK:        return "WPA";
    case WIFI_AUTH_WPA2_PSK:       return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:   return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
    case WIFI_AUTH_WPA3_PSK:       return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:  return "WPA2/WPA3";
    case WIFI_AUTH_OWE:            return "OWE";
    default:                       return "Other";
    }
}

// Insert or refresh by BSSID; evicts the least recently seen entry when full.
// Returns the pooled record for queue posting.
static const ScanResult_t* pool_upsert(const wifi_ap_record_t* rec, TickType_t now)
{
    taskENTER_CRITICAL(&s_pool_mux);
    PoolEntry* free_slot = nullptr;
    PoolEntry* lru = nullptr;
    for (PoolEntry& e : s_pool) {
        if (!e.used) {
            if (!free_slot) free_slot = &e;
            continue;
        }
        if (memcmp(e.ap.bssid, rec->bssid, sizeof(rec->bssid)) == 0) {
            memcpy(e.ap.ssid, rec->ssid, 32);
            e.ap.ssid[32] = 0;
            e.ap.rssi = rec->rssi;
            e.ap.channel = rec->primary;
            e.ap.authmode = rec->authmode;
            e.ap.severity = classify(rec->rssi);
            e.last_seen = now;
            history_append(&e.history, rec->rssi);
            taskEXIT_CRITICAL(&s_pool_mux);
            return &e.ap;
        }
        if (!lru || e.last_seen < lru->last_seen) lru = &e;
    }

    PoolEntry* slot = free_slot;
    if (!slot) {
        slot = lru;
        s_evictions++;
    }
    memcpy(slot->ap.ssid, rec->ssid, 32);
    slot->ap.ssid[32] = 0;
    memcpy(slot->ap.bssid, rec->bssid, sizeof(rec->bssid));
    slot->ap.rssi = rec->rssi;
    slot->ap.channel = rec->primary;
    slot->ap.authmode = rec->authmode;
    slot->ap.severity = classify(rec->rssi);
    slot->last_seen = now;
    slot->used = true;
    history_append(&slot->history, rec->rssi);
    taskEXIT_CRITICAL(&s_pool_mux);
    return &slot->ap;
}

// Single-shot dual-band scan using channel bitmap. Scans all 2.4 GHz and
// US 5 GHz channels concurrently in one blocking call.
static uint16_t scan_all(TickType_t now)
{
    wifi_scan_config_t cfg = {};
    cfg.channel = 0;                          // bitmap mode
    cfg.show_hidden = true;
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    cfg.scan_time.active.min = 100;           // ms per channel
    cfg.scan_time.active.max = 300;
    cfg.scan_time.passive = 300;
    cfg.channel_bitmap.ghz_2_channels = ALL_2G_CHANNEL_MASK;
    cfg.channel_bitmap.ghz_5_channels = US_5G_CHANNEL_MASK;

    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "dual-band scan failed: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t num = SCAN_BUF_MAX;
    esp_wifi_scan_get_ap_records(&num, s_scan_buf);
    for (uint16_t i = 0; i < num; i++) {
        const ScanResult_t* ap = pool_upsert(&s_scan_buf[i], now);
        if (xQueueSend(s_queue, ap, 0) != pdTRUE) s_drops++;
    }
    return num;
}

static void wifi_scan_task(void*)
{
    while (true) {
        TickType_t now = xTaskGetTickCount();
        uint16_t n = scan_all(now);

        // Count 2.4G vs 5G results for the log
        uint16_t n2 = 0, n5 = 0;
        for (uint16_t i = 0; i < n && i < SCAN_BUF_MAX; i++) {
            if (s_scan_buf[i].primary <= 14) n2++;
            else n5++;
        }

        size_t used = 0;
        for (const PoolEntry& e : s_pool) if (e.used) used++;

        ESP_LOGI(TAG, "cycle: total=%u (2.4G=%u 5G=%u) pool=%u/%u evict=%lu drops=%lu hwm=%u heap=%lu",
                 n, n2, n5, (unsigned)used, (unsigned)POOL_SIZE,
                 (unsigned long)s_evictions, (unsigned long)s_drops,
                 (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                 (unsigned long)esp_get_free_heap_size());

        vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL_MS));
    }
}

#include "nvs_flash.h"
#include "nvs.h"

static const char* SF_TAG = "scan_filter";

#define SF_NVS_NS     "scan_filter"
#define SF_KEY_BAND2  "band2"
#define SF_KEY_BAND5  "band5"
#define SF_KEY_RSSI   "minrssi"
#define SF_KEY_PAT    "pattern"

static ScanFilter_t s_filter = { true, true, -100, "" };
static nvs_handle_t s_sf_nvs = 0;

static void sf_nvs_load(void)
{
    if (s_sf_nvs == 0) return;
    uint8_t v;
    int8_t r;
    size_t len = sizeof(s_filter.ssid_pattern);
    if (nvs_get_u8(s_sf_nvs, SF_KEY_BAND2, &v) == ESP_OK) s_filter.band_2g = (v != 0);
    if (nvs_get_u8(s_sf_nvs, SF_KEY_BAND5, &v) == ESP_OK) s_filter.band_5g = (v != 0);
    if (nvs_get_i8(s_sf_nvs, SF_KEY_RSSI, &r) == ESP_OK) s_filter.min_rssi = r;
    nvs_get_str(s_sf_nvs, SF_KEY_PAT, s_filter.ssid_pattern, &len);
}

static void sf_nvs_save_now(void)
{
    if (s_sf_nvs == 0) return;
    nvs_set_u8(s_sf_nvs, SF_KEY_BAND2, s_filter.band_2g ? 1 : 0);
    nvs_set_u8(s_sf_nvs, SF_KEY_BAND5, s_filter.band_5g ? 1 : 0);
    nvs_set_i8(s_sf_nvs, SF_KEY_RSSI, s_filter.min_rssi);
    nvs_set_str(s_sf_nvs, SF_KEY_PAT, s_filter.ssid_pattern);
    nvs_commit(s_sf_nvs);
}

static void sf_nvs_save_tramp(void*)
{
    sf_nvs_save_now();
}

// NVS commit erases/writes flash; the setters run on the UI task (PSRAM
// stack), which is unreachable while the cache is down during the write.
static void sf_nvs_save(void)
{
    flash_broker_exec(sf_nvs_save_tramp, nullptr);
}

void scan_filter_init(void)
{
    esp_err_t ret = nvs_open(SF_NVS_NS, NVS_READWRITE, &s_sf_nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(SF_TAG, "nvs_open failed (%s), using defaults", esp_err_to_name(ret));
        s_sf_nvs = 0;
    }
    sf_nvs_load();
    ESP_LOGI(SF_TAG, "filters: 2.4G=%s 5G=%s min_rssi=%d pat=%s",
             s_filter.band_2g ? "on" : "off",
             s_filter.band_5g ? "on" : "off",
             s_filter.min_rssi,
             s_filter.ssid_pattern);
}

const ScanFilter_t* scan_filter_get(void) { return &s_filter; }

void scan_filter_set_band_2g(bool en) { s_filter.band_2g = en; sf_nvs_save(); }
void scan_filter_set_band_5g(bool en) { s_filter.band_5g = en; sf_nvs_save(); }
void scan_filter_set_min_rssi(int8_t dbm)
{
    if (dbm < -100) dbm = -100;
    if (dbm > -30) dbm = -30;
    s_filter.min_rssi = dbm;
    sf_nvs_save();
}
void scan_filter_set_ssid_pattern(const char* pattern)
{
    if (!pattern) pattern = "";
    strncpy(s_filter.ssid_pattern, pattern, sizeof(s_filter.ssid_pattern) - 1);
    s_filter.ssid_pattern[sizeof(s_filter.ssid_pattern) - 1] = '\0';
    sf_nvs_save();
}

static bool sf_matches(const ScanResult_t* ap)
{
    const ScanFilter_t* f = &s_filter;
    if (ap->channel <= 14 && !f->band_2g) return false;
    if (ap->channel > 14 && !f->band_5g) return false;
    if (ap->rssi < f->min_rssi) return false;
    if (f->ssid_pattern[0]) {
        const char* ssid = (const char*)ap->ssid;
        if (!ssid[0]) return false;  // hidden SSID can't match pattern
        if (strstr(ssid, f->ssid_pattern) == nullptr) return false;
    }
    return true;
}

// Shared snapshot finalizer: strongest first, then mark rogue APs
// (same SSID, different BSSID = potential evil twin). Insertion sort is
// plenty for <= 64 entries.
static void snapshot_finalize(ScanResult_t* out, int n)
{
    for (int i = 1; i < n; i++) {
        ScanResult_t key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < key.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }

    for (int i = 0; i < n; i++) out[i].rogue = false;
    for (int i = 0; i < n; i++) {
        if (out[i].ssid[0] == 0) continue;  // skip hidden
        for (int j = i + 1; j < n; j++) {
            if (out[j].ssid[0] == 0) continue;
            if (strcmp((const char*)out[i].ssid, (const char*)out[j].ssid) == 0) {
                if (memcmp(out[i].bssid, out[j].bssid, 6) != 0) {
                    out[i].rogue = true;
                    out[j].rogue = true;
                }
            }
        }
    }
}

int scan_engine_snapshot_filtered(ScanResult_t* out, int max)
{
    taskENTER_CRITICAL(&s_pool_mux);
    int n = 0;
    for (const PoolEntry& e : s_pool) {
        if (e.used && sf_matches(&e.ap) && n < max) out[n++] = e.ap;
    }
    taskEXIT_CRITICAL(&s_pool_mux);

    snapshot_finalize(out, n);
    return n;
}

esp_err_t scan_engine_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta(), ESP_FAIL, TAG, "sta netif failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    // Lock country code to US permanently — do NOT allow nearby AP beacons to
    // override our regulatory domain.  ieee80211d_enabled=true caused 5 GHz
    // scanning to disappear when a neighbouring AP advertised a different
    // country code (e.g. "01" world-safe or "CN"/"JP" with restrictive 5G rules).
    ESP_RETURN_ON_ERROR(esp_wifi_set_country_code(SSP_WIFI_COUNTRY, false), TAG, "set country failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_country_code(SSP_WIFI_COUNTRY, true), TAG, "set country failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    // Only valid after esp_wifi_start() — returns ESP_ERR_WIFI_NOT_STARTED otherwise
    ESP_RETURN_ON_ERROR(esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO), TAG, "set band mode failed");

    s_queue = xQueueCreate(16, sizeof(ScanResult_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "scan_queue create failed");

    ESP_LOGI(TAG, "Wi-Fi station up: country=%s, dual-band auto, scan-only", SSP_WIFI_COUNTRY);
    return ESP_OK;
}

QueueHandle_t scan_engine_queue(void)
{
    return s_queue;
}

int scan_engine_snapshot(ScanResult_t* out, int max)
{
    taskENTER_CRITICAL(&s_pool_mux);
    int n = 0;
    for (const PoolEntry& e : s_pool) {
        if (e.used && n < max) out[n++] = e.ap;
    }
    taskEXIT_CRITICAL(&s_pool_mux);

    snapshot_finalize(out, n);
    return n;
}

void scan_engine_start_task(void)
{
    xTaskCreateWithCaps(wifi_scan_task, "wifi_scan_task", SCAN_TASK_STACK, nullptr, SCAN_TASK_PRIO, nullptr, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

int scan_engine_get_history(const uint8_t bssid[6], int8_t* out_rssi, int max_samples)
{
    taskENTER_CRITICAL(&s_pool_mux);
    for (const PoolEntry& e : s_pool) {
        if (e.used && memcmp(e.ap.bssid, bssid, 6) == 0) {
            int n = (e.history.count < max_samples) ? e.history.count : max_samples;
            int start = (e.history.head + RSSI_HISTORY_LEN - e.history.count) % RSSI_HISTORY_LEN;
            for (int i = 0; i < n; i++) {
                int idx = (start + i) % RSSI_HISTORY_LEN;
                out_rssi[i] = e.history.samples[idx];
            }
            taskEXIT_CRITICAL(&s_pool_mux);
            return n;
        }
    }
    taskEXIT_CRITICAL(&s_pool_mux);
    return 0;
}
