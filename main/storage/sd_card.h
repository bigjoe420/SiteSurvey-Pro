#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Mounts the SD card on the shared SPI2 bus (CS=SSP_SDCARD_CS) at /sdcard and
// runs a write/read-back self-test. Missing or unreadable card is not fatal:
// returns ESP_ERR_NOT_FOUND and the card is simply absent for this session.
esp_err_t sd_card_init(void);

bool sd_card_present(void);

// True when internal DMA-capable RAM has enough headroom for the SPI
// master's per-command private buffer (a 6-byte SD command needs ~32 B).
// When the DMA heap is exhausted the transaction fails its alloc and the
// IDF cleanup path NULL-derefs — every SD caller must check this first
// and skip/retry instead (Settings-tap panic, 2026-09-28).
bool sd_dma_headroom(void);
