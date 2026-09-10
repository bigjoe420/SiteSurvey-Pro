#include "flash_broker.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char* TAG = "flash_broker";

typedef struct {
    void (*fn)(void*);
    void* arg;
    SemaphoreHandle_t done;
} broker_msg_t;

static QueueHandle_t s_q;
static StaticQueue_t s_qbuf;
static uint8_t s_qstorage[2 * sizeof(broker_msg_t)];

static void broker_task(void*)
{
    broker_msg_t m;
    while (true) {
        if (xQueueReceive(s_q, &m, portMAX_DELAY) == pdTRUE) {
            m.fn(m.arg);
            xSemaphoreGive(m.done);
        }
    }
}

void flash_broker_init(void)
{
    s_q = xQueueCreateStatic(2, sizeof(broker_msg_t), s_qstorage, &s_qbuf);
    configASSERT(s_q);
    // Internal-RAM stack is the whole point: this task performs flash writes,
    // during which the cache (and PSRAM) are unreachable. Priority 24 (max
    // valid is configMAX_PRIORITIES-1 = 24): the caller blocks on a semaphore
    // immediately after queueing, so equal priority still runs the save at
    // once and can't deadlock.
    BaseType_t ok = xTaskCreateWithCaps(broker_task, "flash_broker", 4096, nullptr,
                                        24, nullptr,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "init %s", ok == pdPASS ? "ok" : "FAILED");
}

void flash_broker_exec(void (*fn)(void*), void* arg)
{
    if (!s_q) {  // not initialized: run inline as a last resort
        fn(arg);
        return;
    }
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (!done) {  // heap exhausted: inline is risky but better than losing the save
        fn(arg);
        return;
    }
    broker_msg_t m = { fn, arg, done };
    if (xQueueSend(s_q, &m, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "broker queue full; running inline");
        vSemaphoreDelete(done);
        fn(arg);
        return;
    }
    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
}
