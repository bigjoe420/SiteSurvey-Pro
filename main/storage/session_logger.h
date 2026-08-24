#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "scan_engine.h"
#include "gps.h"

// Session CSV logger: batches Wi-Fi scan results to SD card.
// Must be called after sd_card_init() succeeds.

// No-op if SD card is absent.
esp_err_t session_logger_init(void);

// Opens a new session file (survey_YYYYMMDD_HHMMSS.csv). Auto-started
// by the first logged AP; safe to call explicitly.
void session_logger_start(void);

// Closes the current session file. Safe to call even if no session active.
void session_logger_stop(void);

// Queues one AP observation for the current session. GPS may be nullptr
// or fix_valid=false — coordinates will be 0/0 in that case.
void session_logger_log_ap(const ScanResult_t* ap, const GpsState* gps);

// Flushes the RAM buffer to SD. Called automatically every 5 s or when
// the buffer fills; safe to call manually before a long idle period.
void session_logger_flush(void);

bool session_logger_active(void);
