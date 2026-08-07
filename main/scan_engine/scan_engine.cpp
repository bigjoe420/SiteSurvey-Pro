#include "scan_engine.h"

#include <cstring>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/task.h"

static const char* TAG = "scan_engine";

#define SSP_WIFI_COUNTRY "US"

static constexpr size_t POOL_SIZE = 64;
static constexpr uint16_t SCAN_BUF_MAX = 48;
static constexpr uint32_t SCAN_INTERVAL_MS = 5000;
static constexpr UBaseType_t SCAN_TASK_STACK = 3072;
static constexpr UBaseType_t SCAN_TASK_PRIO = 4;

typedef struct {
    ScanResult_t ap;
    TickType_t last_seen;
    bool used;
} PoolEntry;

static PoolEntry s_pool[POOL_SIZE];
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
    return &slot->ap;
}

static uint16_t scan_band(wifi_band_t band, TickType_t now)
{
    esp_err_t err = esp_wifi_set_band(band);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_band %d failed: %s", band, esp_err_to_name(err));
        return 0;
    }

    wifi_scan_config_t cfg = {};
    cfg.channel = 0;             // all channels of the selected band
    cfg.show_hidden = true;
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "band %d scan failed: %s", band, esp_err_to_name(err));
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
        uint16_t n2 = scan_band(WIFI_BAND_2G, now);
        uint16_t n5 = scan_band(WIFI_BAND_5G, now);

        size_t used = 0;
        for (const PoolEntry& e : s_pool) if (e.used) used++;

        ESP_LOGI(TAG, "cycle: 2.4G=%u 5G=%u pool=%u/%u evict=%lu drops=%lu hwm=%u heap=%lu",
                 n2, n5, (unsigned)used, (unsigned)POOL_SIZE,
                 (unsigned long)s_evictions, (unsigned long)s_drops,
                 (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                 (unsigned long)esp_get_free_heap_size());

        vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL_MS));
    }
}

esp_err_t scan_engine_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta(), ESP_FAIL, TAG, "sta netif failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    // Regulatory domain gates which 5 GHz channels may be scanned
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

void scan_engine_start_task(void)
{
    xTaskCreate(wifi_scan_task, "wifi_scan_task", SCAN_TASK_STACK, nullptr, SCAN_TASK_PRIO, nullptr);
}
