#include "scan_ble.h"

#include <cstring>
#include "esp_check.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"

static const char* TAG = "ble_scan";

static constexpr size_t POOL_SIZE = 32;
static constexpr uint32_t SCAN_DURATION_S = 10;
static constexpr uint32_t SCAN_PAUSE_MS = 2000;
static constexpr UBaseType_t SCAN_TASK_STACK = 3072;
static constexpr UBaseType_t SCAN_TASK_PRIO = 4;

static constexpr int8_t SSP_RSSI_STRONG_DBM = -50;
static constexpr int8_t SSP_RSSI_MODERATE_DBM = -70;
static constexpr int8_t SSP_RSSI_WEAK_DBM = -85;

typedef struct {
    BleScanResult_t dev;
    TickType_t last_seen;
    bool used;
} PoolEntry;

static PoolEntry s_pool[POOL_SIZE];
static portMUX_TYPE s_pool_mux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_queue;
static uint32_t s_evictions;
static uint32_t s_drops;
static volatile bool s_scanning;
static volatile bool s_ble_ready;
static uint8_t s_own_addr_type;

static uint8_t classify_ble_rssi(int8_t rssi)
{
    if (rssi >= SSP_RSSI_STRONG_DBM) return 0;
    if (rssi >= SSP_RSSI_MODERATE_DBM) return 1;
    if (rssi >= SSP_RSSI_WEAK_DBM) return 2;
    return 3;
}

// Insert or refresh by MAC; evicts the least recently seen entry when full.
static void pool_upsert(const uint8_t mac[6], int8_t rssi,
                        const uint8_t* name, uint8_t name_len,
                        const uint8_t* mfg_data, uint8_t mfg_len)
{
    taskENTER_CRITICAL(&s_pool_mux);
    PoolEntry* free_slot = nullptr;
    PoolEntry* lru = nullptr;
    for (PoolEntry& e : s_pool) {
        if (!e.used) {
            if (!free_slot) free_slot = &e;
            continue;
        }
        if (memcmp(e.dev.mac, mac, 6) == 0) {
            e.dev.rssi = rssi;
            e.dev.severity = classify_ble_rssi(e.dev.rssi);
            e.last_seen = xTaskGetTickCount();
            if (name_len > 0 && name_len < sizeof(e.dev.name)) {
                memcpy(e.dev.name, name, name_len);
                e.dev.name[name_len] = '\0';
            }
            if (mfg_len > 0 && mfg_len <= sizeof(e.dev.mfg_data)) {
                memcpy(e.dev.mfg_data, mfg_data, mfg_len);
                e.dev.mfg_len = mfg_len;
            }
            taskEXIT_CRITICAL(&s_pool_mux);
            if (xQueueSend(s_queue, &e.dev, 0) != pdTRUE) s_drops++;
            return;
        }
        if (!lru || e.last_seen < lru->last_seen) lru = &e;
    }

    PoolEntry* slot = free_slot ? free_slot : lru;
    if (!free_slot) s_evictions++;

    memset(&slot->dev, 0, sizeof(slot->dev));
    memcpy(slot->dev.mac, mac, 6);
    slot->dev.rssi = rssi;
    slot->dev.severity = classify_ble_rssi(slot->dev.rssi);
    if (name_len > 0 && name_len < sizeof(slot->dev.name)) {
        memcpy(slot->dev.name, name, name_len);
        slot->dev.name[name_len] = '\0';
    }
    if (mfg_len > 0 && mfg_len <= sizeof(slot->dev.mfg_data)) {
        memcpy(slot->dev.mfg_data, mfg_data, mfg_len);
        slot->dev.mfg_len = mfg_len;
    }
    slot->last_seen = xTaskGetTickCount();
    slot->used = true;
    taskEXIT_CRITICAL(&s_pool_mux);

    if (xQueueSend(s_queue, &slot->dev, 0) != pdTRUE) s_drops++;
}

static int gap_event(struct ble_gap_event* event, void* arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        const struct ble_gap_disc_desc* disc = &event->disc;
        struct ble_hs_adv_fields fields = {};
        int rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
        if (rc != 0) {
            // Fallback: upsert with raw MAC/RSSI even if parse fails
            pool_upsert(disc->addr.val, disc->rssi, nullptr, 0, nullptr, 0);
            break;
        }
        pool_upsert(disc->addr.val, disc->rssi,
                    fields.name, fields.name_len,
                    fields.mfg_data, fields.mfg_data_len);
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        s_scanning = false;
        ESP_LOGI(TAG, "scan stopped");
        break;
    default:
        break;
    }
    return 0;
}

static void ble_on_sync(void)
{
    s_ble_ready = true;
    ESP_LOGI(TAG, "NimBLE host synced");
}

static void ble_on_reset(int reason)
{
    s_ble_ready = false;
    ESP_LOGW(TAG, "NimBLE reset, reason=%d", reason);
}

static void ble_host_task(void* param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_scan_task(void*)
{
    // Wait for NimBLE host-controller sync
    while (!s_ble_ready) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Infer our address type (required before scanning)
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "address infer failed, rc=%d", rc);
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        if (!s_scanning) {
            struct ble_gap_disc_params params = {};
            params.itvl = 0x50;           // 50 ms (80 * 0.625ms)
            params.window = 0x30;         // 30 ms (48 * 0.625ms)
            params.passive = 1;
            params.filter_duplicates = 0;
            params.limited = 0;
            params.filter_policy = 0;

            rc = ble_gap_disc(s_own_addr_type,
                              SCAN_DURATION_S * 1000,
                              &params, gap_event, nullptr);
            if (rc == 0) {
                s_scanning = true;
                ESP_LOGI(TAG, "scan started");
            } else {
                ESP_LOGW(TAG, "start_scanning failed: rc=%d", rc);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SCAN_DURATION_S * 1000 + SCAN_PAUSE_MS));

        if (s_scanning) {
            ble_gap_disc_cancel();
            s_scanning = false;
        }

        size_t used = 0;
        for (const PoolEntry& e : s_pool) if (e.used) used++;
        ESP_LOGI(TAG, "pool=%u/%u evict=%lu drops=%lu hwm=%u heap=%lu",
                 (unsigned)used, (unsigned)POOL_SIZE,
                 (unsigned long)s_evictions, (unsigned long)s_drops,
                 (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                 (unsigned long)esp_get_free_heap_size());
    }
}

esp_err_t ble_scan_init(void)
{
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble_port_init failed");

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    nimble_port_freertos_init(ble_host_task);

    s_queue = xQueueCreate(16, sizeof(BleScanResult_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "ble_queue create failed");

    ESP_LOGI(TAG, "BLE observer up: NimBLE passive scan, interval=50ms window=30ms");
    return ESP_OK;
}

QueueHandle_t ble_scan_queue(void)
{
    return s_queue;
}

int ble_scan_snapshot(BleScanResult_t* out, int max)
{
    taskENTER_CRITICAL(&s_pool_mux);
    int n = 0;
    for (const PoolEntry& e : s_pool) {
        if (e.used && n < max) out[n++] = e.dev;
    }
    taskEXIT_CRITICAL(&s_pool_mux);

    // Strongest signal first
    for (int i = 1; i < n; i++) {
        BleScanResult_t key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < key.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}

void ble_scan_start_task(void)
{
    xTaskCreate(ble_scan_task, "ble_scan_task", SCAN_TASK_STACK, nullptr, SCAN_TASK_PRIO, nullptr);
}
