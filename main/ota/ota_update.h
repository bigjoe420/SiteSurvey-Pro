#pragma once

#include <stddef.h>
#include "esp_err.h"

// One firmware candidate found on the SD card.
typedef struct {
    char     name[64];   // file name only (e.g. "SiteSurvey-Pro.bin")
    uint32_t size;       // bytes
    char     version[16];// app version from the image's embedded descriptor ("?" if unreadable)
} OtaFile_t;

// Scan the SD card root ("/sdcard") for *.bin files that plausibly fit an
// OTA app partition. Returns the number of entries written to `out` (<= max),
// 0 when the SD card is absent or no valid image is found.
int ota_update_scan(OtaFile_t* out, int max);

// Flash the SD file `sd_path` (full path, e.g. "/sdcard/SiteSurvey-Pro.bin")
// into the next OTA app partition, mark it bootable, and restart into it.
// `progress_cb` (may be NULL) is called with 0..100 as writing proceeds.
// On success this function never returns (esp_restart). On failure it
// returns the error and the previous firmware keeps running — the new
// image is not marked valid, so the bootloader still boots the old app.
esp_err_t ota_update_flash(const char* sd_path,
                           void (*progress_cb)(int pct, void* ctx),
                           void* ctx);
