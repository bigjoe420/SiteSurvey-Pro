#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "bme680.h"
#include "gps.h"

// One environment + position snapshot, posted to env_queue (depth 8) every 5 s.
// env_valid is false when the BME680 is absent or a shot failed; GPS fields
// report fix_valid=false until satellites are acquired (never gates anything).
typedef struct {
    Bme680Data env;
    bool env_valid;
    GpsState gps;
} EnvSnapshot_t;

// Brings up the I2C bus, probes the BME680, installs the GPS LP-UART.
esp_err_t sensors_init(void);

// Consumer handle, valid after sensors_init().
QueueHandle_t sensors_queue(void);

// False when no BME680 answered the probe — callers must not gate on env data.
bool sensors_bme680_present(void);

// Starts sensor_task (3 KB stack): GPS poll loop + 5 s BME680 snapshot cycle.
void sensors_start_task(void);
