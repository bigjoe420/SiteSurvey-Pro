#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Converts the most recent session CSV on the SD card (/sdcard/survey_*.csv)
// to a KML file for Google Earth (/sdcard/<same name>.kml).
//
// Behavior:
//  - Dedups by BSSID; strongest RSSI wins, first-seen timestamp kept.
//  - Rows without a GPS fix (lat/lon both 0) are skipped and counted —
//    plotting them would drop pins in the Gulf of Guinea.
//  - Robust to commas inside SSIDs (right-anchored field parse) and to a
//    partially-written trailing line (field-count check).
//  - Working set lives in PSRAM; only small stack buffers touch internal RAM.
//
// out_path receives the .kml path on success. out_placemarks / out_no_fix
// may be nullptr. Returns ESP_ERR_NOT_FOUND if no SD or no CSV.
esp_err_t kml_export_latest(char* out_path, size_t out_path_len,
                            int* out_placemarks, int* out_no_fix);
