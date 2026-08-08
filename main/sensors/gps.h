#pragma once

#include <stdint.h>
#include "esp_err.h"

// Latest decoded GNSS state. Positions are degrees x 1e7 (integer fixed point).
typedef struct {
    int32_t lat_e7;
    int32_t lon_e7;
    uint8_t sats;
    uint8_t fix_quality;   // NMEA GGA quality: 0 = no fix
    uint32_t utc_hhmmss;   // from RMC, packed HHMMSS
    bool fix_valid;
    uint32_t rx_bytes;       // raw UART bytes seen (wiring/power canary)
    uint32_t sentences_ok;
    uint32_t sentences_bad;
} GpsState;

// Installs the LP-UART driver on SSP_GPS_UART (GPIO 4/5 fixed LP-IO), 9600 8N1.
esp_err_t gps_init(void);

// Drains the UART (100 ms read timeout) and folds complete, checksum-valid
// GGA/RMC sentences into state. Talker-agnostic: $GP/$GN/$BD all accepted.
// Call only from sensor_task context.
void gps_poll(GpsState* state);
