#include "sd_card.h"

#include <cstdio>
#include <cstring>
#include "board_pins.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char* TAG = "sdcard";
#define SD_MOUNT "/sdcard"

static bool s_present;

// 2026-09-26 audit: the IDF v6.1 sdmmc protocol layer routes unaligned sector
// reads/writes (all FATFS traffic) through a per-transaction MALLOC_CAP_DMA
// temp buffer allocated from the heap. By session-open time, BLE + Wi-Fi have
// drained internal DMA RAM to ~100 B, so every fopen fails with
// "allocate_dma_buf: not enough mem, err=0x101". Pin one small DMA buffer at
// init — internal RAM is still plentiful here (≈68 KB free) — and cap the
// chunk at 4 sectors (2 KB). CSV lines are tiny; throughput is irrelevant.
// NOTE: must be a heap_caps_malloc'd pointer (not static .bss) — the driver
// calls heap_caps_get_allocated_size() on it and asserts on non-heap pointers.
static void* s_sd_dma_buf;

esp_err_t sd_card_init(void)
{
    // SD is the third device on SPI2 (display 20 MHz, touch 2.5 MHz); the bus
    // is already up and CS is parked HIGH from board_init_gpio
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // Writes (not reads) were flaking at the card level (FR_DISK_ERR on write
    // data tokens, reads always OK) — drop to 10 MHz for margin. CSV lines are
    // tiny; throughput is irrelevant here.
    host.max_freq_khz = 10000;
    s_sd_dma_buf = heap_caps_aligned_alloc(8, 4 * 512, MALLOC_CAP_DMA);
    if (!s_sd_dma_buf) {
        ESP_LOGE(TAG, "SD DMA buffer alloc failed — logging will degrade");
    } else {
        host.unaligned_multi_block_rw_max_chunk_size = 4;
        host.dma_aligned_buffer = s_sd_dma_buf;
    }

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = SSP_SDCARD_CS;

    esp_vfs_fat_mount_config_t mnt = {};
    mnt.format_if_mount_failed = false;
    mnt.max_files = 6;

    sdmmc_card_t* card;
    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &mnt, &card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no SD card on SPI2 (CS=GPIO%d) - logging disabled (%s)",
                 SSP_SDCARD_CS, esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    s_present = true;
    ESP_LOGI(TAG, "SD up: %s, %llu MB",
             card->cid.name, ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));

    // Write/read-back self-test: proves the whole path, harmless one-liner file
    const char* path = SD_MOUNT "/ssp_test.txt";
    const char* payload = "sitesurvey-pro sd self-test\n";
    FILE* f = fopen(path, "w");
    if (!f) {
        ESP_LOGW(TAG, "self-test: fopen for write failed");
        return ESP_FAIL;
    }
    fputs(payload, f);
    fclose(f);

    char buf[64] = {};
    f = fopen(path, "r");
    if (f) {
        fgets(buf, sizeof(buf), f);
        fclose(f);
    }
    if (strcmp(buf, payload) != 0) {
        ESP_LOGW(TAG, "self-test: read-back mismatch");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "self-test OK: wrote and read back %s", path);
    return ESP_OK;
}

bool sd_card_present(void)
{
    return s_present;
}
