#include "report_export.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "sd_card.h"
#include "session_logger.h"
#include "session_csv.h"

static const char* TAG = "report";
#define LINE_LEN   256
#define CH_MAX     200   // covers 2.4G ch1-14 and 5G ch36-177

static int cmp_rssi_desc(const void* a, const void* b)
{
    return ((const ApRec*)b)->rssi - ((const ApRec*)a)->rssi;
}

esp_err_t report_export_latest(char* out_path, size_t out_path_len,
                               int* out_aps)
{
    if (!sd_card_present()) {
        ESP_LOGW(TAG, "SD card absent");
        return ESP_ERR_NOT_FOUND;
    }
    char csv_path[128];
    if (!csv_find_active(csv_path, sizeof(csv_path))) {
        ESP_LOGW(TAG, "no session CSV found");
        return ESP_ERR_NOT_FOUND;
    }
    return report_export_path(csv_path, out_path, out_path_len, out_aps);
}

esp_err_t report_export_path(const char* csv_path, char* out_path, size_t out_path_len,
                             int* out_aps)
{
    if (!sd_card_present()) {
        ESP_LOGW(TAG, "SD card absent");
        return ESP_ERR_NOT_FOUND;
    }
    if (!csv_path || !csv_path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    const char* slash = strrchr(csv_path, '/');
    const char* base = slash ? slash + 1 : csv_path;
    snprintf(out_path, out_path_len, "/sdcard/%.*s.txt",
             (int)(strlen(base) - 4), base);

    session_logger_flush();

    FILE* fin = fopen(csv_path, "r");
    if (!fin) {
        ESP_LOGW(TAG, "fopen failed: %s", csv_path);
        return ESP_ERR_NOT_FOUND;
    }

    // Channel histogram: transient PSRAM (too big for stack, pointless in BSS)
    int* ch_obs = (int*)heap_caps_malloc(CH_MAX * sizeof(int),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int* ch_sum = (int*)heap_caps_malloc(CH_MAX * sizeof(int),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ch_obs || !ch_sum) {
        ESP_LOGE(TAG, "PSRAM histogram alloc failed");
        if (ch_obs) heap_caps_free(ch_obs);
        if (ch_sum) heap_caps_free(ch_sum);
        fclose(fin);
        return ESP_ERR_NO_MEM;
    }
    memset(ch_obs, 0, CH_MAX * sizeof(int));
    memset(ch_sum, 0, CH_MAX * sizeof(int));

    char line[LINE_LEN];
    int rows = 0, bad = 0, with_fix = 0;
    double min_lat = 90, max_lat = -90, min_lon = 180, max_lon = -180;
    char ts_first[24] = {0}, ts_last[24] = {0};
    ApTable table = {};
    aptable_reset(&table);

    if (!fgets(line, sizeof(line), fin)) {
        fclose(fin);
        heap_caps_free(ch_obs);
        heap_caps_free(ch_sum);
        return ESP_ERR_INVALID_RESPONSE;
    }
    while (fgets(line, sizeof(line), fin)) {
        rows++;
        ApRec r;
        if (!csv_parse_row(line, &r)) { bad++; continue; }
        if (!ts_first[0]) snprintf(ts_first, sizeof(ts_first), "%s", r.ts);
        snprintf(ts_last, sizeof(ts_last), "%s", r.ts);
        if (r.channel >= 1 && r.channel < CH_MAX) {
            ch_obs[r.channel]++;
            ch_sum[r.channel] += r.rssi;
        }
        bool has_fix = (r.lat != 0.0 || r.lon != 0.0);
        if (has_fix) {
            with_fix++;
            if (r.lat < min_lat) min_lat = r.lat;
            if (r.lat > max_lat) max_lat = r.lat;
            if (r.lon < min_lon) min_lon = r.lon;
            if (r.lon > max_lon) max_lon = r.lon;
        }
        ApRec* slot = aptable_add(&table, &r);
        if (!slot) {
            fclose(fin);
            heap_caps_free(ch_obs);
            heap_caps_free(ch_sum);
            aptable_free(&table);
            return ESP_ERR_NO_MEM;
        }
        if (r.rssi > slot->rssi) slot->rssi = r.rssi;
    }
    fclose(fin);

    // Sort unique APs by RSSI for strongest/weakest sections
    if (table.n > 1) {
        qsort(table.recs, table.n, sizeof(ApRec), cmp_rssi_desc);
    }

    FILE* f = fopen(out_path, "w");
    if (!f) {
        ESP_LOGW(TAG, "fopen write failed: %s", out_path);
        aptable_free(&table);
        return ESP_ERR_NOT_FOUND;
    }

    int n24 = 0, n5 = 0;
    struct { const char* name; int count; } sec[12];
    int sec_n = 0;
    for (int i = 0; i < table.n; i++) {
        const ApRec* a = &table.recs[i];
        if (a->channel <= 14) n24++; else n5++;
        int j;
        for (j = 0; j < sec_n; j++) {
            if (strcmp(sec[j].name, a->auth) == 0) { sec[j].count++; break; }
        }
        if (j == sec_n && sec_n < 12) {
            sec[sec_n].name = a->auth;
            sec[sec_n].count = 1;
            sec_n++;
        }
    }

    fprintf(f, "SITESURVEY PRO - SESSION REPORT\n");
    fprintf(f, "================================\n");
    fprintf(f, "Session:  %s\n", base);
    fprintf(f, "Span:     %s .. %s\n", ts_first[0] ? ts_first : "?",
            ts_last[0] ? ts_last : "?");
    fprintf(f, "\nOBSERVATIONS\n");
    fprintf(f, "  rows logged:   %d\n", rows);
    fprintf(f, "  unique APs:    %d\n", table.n);
    fprintf(f, "  with GPS fix:  %d\n", with_fix);
    fprintf(f, "  no fix:        %d\n", rows - with_fix - bad);
    if (bad) fprintf(f, "  bad rows:      %d\n", bad);

    fprintf(f, "\nBANDS (unique APs)\n");
    if (table.n > 0) {
        fprintf(f, "  2.4 GHz:  %d (%d%%)\n", n24, n24 * 100 / table.n);
        fprintf(f, "  5 GHz:    %d (%d%%)\n", n5, n5 * 100 / table.n);
    } else {
        fprintf(f, "  (none)\n");
    }

    fprintf(f, "\nSTRONGEST\n");
    for (int i = 0; i < table.n && i < 5; i++) {
        const ApRec* a = &table.recs[i];
        fprintf(f, "  %4d dBm  %-24.24s ch%-3d %s\n",
                a->rssi, a->ssid, a->channel, a->mac);
    }
    fprintf(f, "\nWEAKEST (heard at all)\n");
    for (int i = table.n - 1; i >= 0 && i >= table.n - 5; i--) {
        const ApRec* a = &table.recs[i];
        fprintf(f, "  %4d dBm  %-24.24s ch%-3d %s\n",
                a->rssi, a->ssid, a->channel, a->mac);
    }

    fprintf(f, "\nCHANNELS (top 5 by observations)\n");
    {
        // selection pass over the histogram, no full sort needed
        for (int k = 0; k < 5; k++) {
            int best = -1, best_obs = 0;
            for (int c = 1; c < CH_MAX; c++) {
                if (ch_obs[c] > best_obs) { best_obs = ch_obs[c]; best = c; }
            }
            if (best < 0) break;
            fprintf(f, "  ch%-3d %-4s %4d obs  avg %4d dBm\n",
                    best, best <= 14 ? "2.4G" : "5G", ch_obs[best],
                    ch_sum[best] / ch_obs[best]);
            ch_obs[best] = 0;  // exclude from next pass
        }
    }

    fprintf(f, "\nSECURITY (unique APs)\n");
    for (int j = 0; j < sec_n; j++) {
        fprintf(f, "  %-12s %d\n", sec[j].name, sec[j].count);
    }

    if (with_fix > 0) {
        char lat_a[24], lat_b[24], lon_a[24], lon_b[24];
        fmt_deg7(lat_a, sizeof(lat_a), min_lat);
        fmt_deg7(lat_b, sizeof(lat_b), max_lat);
        fmt_deg7(lon_a, sizeof(lon_a), min_lon);
        fmt_deg7(lon_b, sizeof(lon_b), max_lon);
        fprintf(f, "\nCOVERAGE\n");
        fprintf(f, "  lat %s .. %s\n", lat_a, lat_b);
        fprintf(f, "  lon %s .. %s\n", lon_a, lon_b);
    }

    fprintf(f, "\n-- generated by SiteSurvey Pro v%s --\n", "0.9");
    fclose(f);
    heap_caps_free(ch_obs);
    heap_caps_free(ch_sum);

    ESP_LOGI(TAG, "report: %d unique APs (%d rows, %d bad, %d fixed) -> %s",
             table.n, rows, bad, with_fix, out_path);
    if (out_aps) *out_aps = table.n;
    aptable_free(&table);
    return ESP_OK;
}
