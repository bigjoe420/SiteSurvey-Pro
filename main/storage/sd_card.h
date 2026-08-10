#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Mounts the SD card on the shared SPI2 bus (CS=SSP_SDCARD_CS) at /sdcard and
// runs a write/read-back self-test. Missing or unreadable card is not fatal:
// returns ESP_ERR_NOT_FOUND and the card is simply absent for this session.
esp_err_t sd_card_init(void);

bool sd_card_present(void);
