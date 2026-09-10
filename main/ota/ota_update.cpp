#include "ota_update.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"

static const char* TAG = "ota";
#define SD_ROOT "/sdcard"

// Sanity window: a real app image is somewhere between 256 KB and the 2 MB
// OTA slot size. Anything outside that is not a firmware image.
#define OTA_MIN_IMAGE_SIZE (256 * 1024)
#define OTA_MAX_IMAGE_SIZE (0x200000)

static bool has_bin_suffix(const char* name)
{
    size_t n = strlen(name);
    if (n < 5) return false;
    const char* ext = name + n - 4;
    return (ext[0] == '.') &&
           (ext[1] == 'b' || ext[1] == 'B') &&
           (ext[2] == 'i' || ext[2] == 'I') &&
           (ext[3] == 'n' || ext[3] == 'N');
}

// esp_app_desc_t lives at offset 0x20 of every app image. Returns true when a
// valid descriptor was read; false leaves version as "?".
static bool read_image_version(const char* path, char* version, size_t ver_len)
{
    strcpy(version, "?");
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0x20, SEEK_SET) != 0) { fclose(f); return false; }
    esp_app_desc_t desc;
    bool ok = fread(&desc, sizeof(desc), 1, f) == 1 &&
              desc.magic_word == ESP_APP_DESC_MAGIC_WORD;
    fclose(f);
    if (ok) {
        strncpy(version, desc.version, ver_len - 1);
        version[ver_len - 1] = '\0';
    }
    return ok;
}

int ota_update_scan(OtaFile_t* out, int max)
{
    if (!out || max <= 0) return 0;

    DIR* dir = opendir(SD_ROOT);
    if (!dir) {
        ESP_LOGW(TAG, "cannot open %s", SD_ROOT);
        return 0;
    }

    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr && count < max) {
        if (ent->d_type != DT_REG) continue;
        if (!has_bin_suffix(ent->d_name)) continue;

        char path[320];
        snprintf(path, sizeof(path), "%s/%s", SD_ROOT, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (st.st_size < OTA_MIN_IMAGE_SIZE || st.st_size > OTA_MAX_IMAGE_SIZE) {
            ESP_LOGW(TAG, "skip %s: size %ld out of range", ent->d_name, (long)st.st_size);
            continue;
        }

        strncpy(out[count].name, ent->d_name, sizeof(out[count].name) - 1);
        out[count].name[sizeof(out[count].name) - 1] = '\0';
        out[count].size = (uint32_t)st.st_size;
        read_image_version(path, out[count].version, sizeof(out[count].version));
        ESP_LOGI(TAG, "candidate: %s (%u bytes, v%s)", out[count].name, out[count].size, out[count].version);
        count++;
    }
    closedir(dir);
    return count;
}

esp_err_t ota_update_flash(const char* sd_path,
                           void (*progress_cb)(int pct, void* ctx),
                           void* ctx)
{
    FILE* f = fopen(sd_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "cannot open %s", sd_path);
        return ESP_ERR_NOT_FOUND;
    }

    // ESP image magic check — refuse random .bin files before touching flash
    uint8_t magic = 0;
    if (fread(&magic, 1, 1, f) != 1 || magic != ESP_IMAGE_HEADER_MAGIC) {
        fclose(f);
        ESP_LOGE(TAG, "%s is not an ESP image (magic 0x%02X)", sd_path, magic);
        return ESP_ERR_INVALID_ARG;
    }
    fseek(f, 0, SEEK_SET);
    struct stat fst;
    uint32_t file_size = (stat(sd_path, &fst) == 0) ? (uint32_t)fst.st_size : 0;

    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part) {
        fclose(f);
        ESP_LOGE(TAG, "no OTA partition available");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "flashing %s -> %s (0x%lX, %u bytes)",
             sd_path, part->label, (unsigned long)part->address, part->size);

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        fclose(f);
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    static uint8_t buf[4096];  // static: keep 4 KB off the task stack
    size_t total = 0;
    int last_pct = -1;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        err = esp_ota_write(handle, buf, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed at %u: %s", total, esp_err_to_name(err));
            esp_ota_abort(handle);
            fclose(f);
            return err;
        }
        total += n;
        int pct = file_size ? (int)((total * 100) / file_size) : 0;
        if (pct > 99) pct = 99;  // 100 is reserved for "verified, rebooting"
        if (pct != last_pct) {
            last_pct = pct;
            if (progress_cb) progress_cb(pct, ctx);
        }
        // keep the watchdog happy on single-core-heavy writes
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    fclose(f);

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed (bad image?): %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "update complete (%u bytes) — restarting into %s", total, part->label);
    if (progress_cb) progress_cb(100, ctx);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();  // never returns
    return ESP_OK;
}
