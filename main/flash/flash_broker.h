#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start the flash broker task. Call once during init, before the UI task.
void flash_broker_init(void);

// Run fn(arg) on the broker task (internal-RAM stack) and block until it
// finishes. REQUIRED before any flash write (NVS set/commit, OTA, partition
// erase) performed on behalf of a task whose stack lives in PSRAM: flash
// erase/write disables the cache, and PSRAM (including the caller's stack)
// is unreachable while the cache is down.
void flash_broker_exec(void (*fn)(void*), void* arg);

#ifdef __cplusplus
}
#endif
