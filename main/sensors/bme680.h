#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

// Compensated forced-mode measurement. Gas is raw heater resistance in ohms
// (Bosch BSEC IAQ intentionally deferred).
typedef struct {
    int32_t temp_c_x100;   // 0.01 °C
    uint32_t hum_x100;     // 0.01 %RH
    uint32_t press_pa;     // Pa
    uint32_t gas_ohm;      // 0 when the gas measurement was not valid/stable
} Bme680Data;

// Probes SSP_BME680_ADDR_0 then _1 on the given bus, verifies chip id,
// loads calibration, programs a 300 °C / 150 ms heater profile.
// Returns ESP_ERR_NOT_FOUND when no BME680 is on CN1.
esp_err_t bme680_init(i2c_master_bus_handle_t bus);

// One forced-mode shot. Blocks the calling task ~200 ms (heater + conversion);
// call only from sensor_task context.
esp_err_t bme680_read(Bme680Data* out);
