#include "sensors.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/i2c_master.h"
#include "freertos/task.h"

#include "board_pins.h"

static const char* TAG = "sensors";

static constexpr uint32_t ENV_PERIOD_MS = 5000;
static constexpr UBaseType_t SENSOR_TASK_STACK = 3072;
static constexpr UBaseType_t SENSOR_TASK_PRIO = 3;

static QueueHandle_t s_queue;
static bool s_bme680_present;
static uint32_t s_drops;

// Sweeps the whole 7-bit address space once at boot
// so a silent / mis-wired / wrong-part module shows up in the log. A probe is
// address-phase + STOP only — harmless to any device on the bus.
static void i2c_bus_scan(i2c_master_bus_handle_t bus)
{
    int found = 0;
    for (uint16_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "i2c scan: device ACKs at 0x%02X", addr);
            found++;
        }
    }
    if (!found) {
        ESP_LOGW(TAG, "i2c scan: bus silent across 0x08-0x77 - check CN1 wiring/power");
    }
}

static void sensor_task(void*)
{
    GpsState gps = {};
    TickType_t last_env = 0;

    while (true) {
        gps_poll(&gps);  // 100 ms UART timeout bounds the loop cadence

        TickType_t now = xTaskGetTickCount();
        if (now - last_env < pdMS_TO_TICKS(ENV_PERIOD_MS)) continue;
        last_env = now;

        EnvSnapshot_t snap = {};
        snap.gps = gps;
        if (s_bme680_present) {
            snap.env_valid = bme680_read(&snap.env) == ESP_OK;
            if (!snap.env_valid) ESP_LOGW(TAG, "BME680 shot failed");
        }

        if (xQueueSend(s_queue, &snap, 0) != pdTRUE) s_drops++;

        ESP_LOGI(TAG, "env: valid=%d gps_rx=%lu ok=%lu bad=%lu drops=%lu hwm=%u heap=%lu",
                 snap.env_valid,
                 (unsigned long)snap.gps.rx_bytes,
                 (unsigned long)snap.gps.sentences_ok, (unsigned long)snap.gps.sentences_bad,
                 (unsigned long)s_drops,
                 (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                 (unsigned long)esp_get_free_heap_size());
    }
}

esp_err_t sensors_init(void)
{
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = SSP_I2C_PORT;
    bus_cfg.scl_io_num = SSP_I2C_SCL;
    bus_cfg.sda_io_num = SSP_I2C_SDA;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;  // CN1 may run without the sensor attached

    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c bus init failed");

    i2c_bus_scan(bus);
    s_bme680_present = bme680_init(bus) == ESP_OK;  // absence is not fatal
    ESP_RETURN_ON_ERROR(gps_init(), TAG, "gps uart init failed");

    s_queue = xQueueCreate(8, sizeof(EnvSnapshot_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "env_queue create failed");

    ESP_LOGI(TAG, "sensors up: BME680 %s, GPS streaming", s_bme680_present ? "present" : "ABSENT");
    return ESP_OK;
}

QueueHandle_t sensors_queue(void)
{
    return s_queue;
}

bool sensors_bme680_present(void)
{
    return s_bme680_present;
}

void sensors_start_task(void)
{
    xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK, nullptr, SENSOR_TASK_PRIO, nullptr);
}
