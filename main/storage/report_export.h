#pragma once

#include <stddef.h>
#include "esp_err.h"

// Generates a human-readable summary report (.txt) from the most recent
// session CSV on the SD card, next to the KML output:
//   /sdcard/<session name>.txt
//
// Contents: observation counts, 2.4G/5G split, busiest channels with average
// RSSI, security breakdown, strongest/weakest unique networks, and GPS
// coverage bounds when fixes were logged.
//
// out_path receives the .txt path on success; out_aps the unique-AP count
// (either may be nullptr). Working set is PSRAM + small stack buffers.
esp_err_t report_export_latest(char* out_path, size_t out_path_len,
                               int* out_aps);

// Generates the report from an EXPLICIT session CSV path
// (/sdcard/survey_*.csv) instead of resolving the active session.
// Same output and semantics as report_export_latest.
esp_err_t report_export_path(const char* csv_path, char* out_path, size_t out_path_len,
                             int* out_aps);
