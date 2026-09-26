#include "session_csv.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

static const char* TAG = "session_csv";
#define SD_DIR "/sdcard"

// Written by session_logger at session start; lets exporters pick the
// currently-active session even with a 1970 clock. Deliberately NOT NVS:
// flash/NVS access from the PSRAM-stack export callbacks locks up the CPU.
#define ACTIVE_POINTER  SD_DIR "/current_session"

bool csv_find_active(char* out_path, size_t n)
{
    FILE* pf = fopen(ACTIVE_POINTER, "r");
    if (pf) {
        char p[320] = {0};
        if (fgets(p, sizeof(p), pf)) {
            p[strcspn(p, "\r\n")] = 0;
            fclose(pf);
            struct stat st;
            if (p[0] && stat(p, &st) == 0 && st.st_size > 0) {
                snprintf(out_path, n, "%s", p);
                return true;
            }
        } else {
            fclose(pf);
        }
    }
    return csv_find_latest(out_path, n);
}

bool csv_find_latest(char* out_path, size_t n)
{
    DIR* d = opendir(SD_DIR);
    if (!d) return false;
    char best[288] = {0};
    time_t best_mtime = 0;
    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        const char* nm = de->d_name;
        size_t L = strlen(nm);
        if (L < 12 || strncmp(nm, "survey_", 7) != 0) continue;
        if (strcmp(nm + L - 4, ".csv") != 0) continue;
        char p[320];
        snprintf(p, sizeof(p), "%s/%s", SD_DIR, nm);
        struct stat st;
        if (stat(p, &st) != 0 || st.st_size == 0) continue;
        // No-RTC boards name every file 1970..., so name sort is wrong.
        // The session of interest is the one still being written -> newest
        // mtime wins; name breaks ties (2 s FAT resolution).
        if (best[0] == 0 || st.st_mtime > best_mtime ||
            (st.st_mtime == best_mtime && strcmp(nm, best) > 0)) {
            snprintf(best, sizeof(best), "%s", nm);
            best_mtime = st.st_mtime;
        }
    }
    closedir(d);
    if (best[0] == 0) return false;
    snprintf(out_path, n, "%s/%s", SD_DIR, best);
    return true;
}

static CsvScanStats s_stats;

int csv_list_sessions(CsvSession* out, int max)
{
    if (!out || max <= 0) return -1;
    s_stats = {};

    // Active session basename (if the pointer file exists) for marking.
    char active[40] = {0};
    FILE* pf = fopen(ACTIVE_POINTER, "r");
    if (pf) {
        char p[320] = {0};
        if (fgets(p, sizeof(p), pf)) {
            p[strcspn(p, "\r\n")] = 0;
            const char* slash = strrchr(p, '/');
            snprintf(active, sizeof(active), "%.*s",
                     (int)sizeof(active) - 1, slash ? slash + 1 : p);
        }
        fclose(pf);
    }

    DIR* d = opendir(SD_DIR);
    if (!d) {
        ESP_LOGW(TAG, "opendir(%s) failed — SD absent or not mounted", SD_DIR);
        return -1;
    }
    int n = 0;
    struct dirent* de;
    while ((de = readdir(d)) != nullptr && n < max) {
        const char* nm = de->d_name;
        size_t L = strlen(nm);
        s_stats.dir_entries++;
        if (L < 12 || L >= sizeof(out[0].name)) continue;
        if (strncmp(nm, "survey_", 7) != 0) continue;
        if (strcmp(nm + L - 4, ".csv") != 0) continue;
        s_stats.survey_named++;
        char p[320];
        snprintf(p, sizeof(p), "%s/%s", SD_DIR, nm);
        struct stat st;
        if (stat(p, &st) != 0) { s_stats.stat_fail++; continue; }
        if (st.st_size == 0) { s_stats.empty++; continue; }
        snprintf(out[n].name, sizeof(out[n].name), "%.*s",
                 (int)sizeof(out[n].name) - 1, nm);
        out[n].size  = (uint32_t)st.st_size;
        out[n].mtime = st.st_mtime;
        out[n].is_active = active[0] && strcmp(nm, active) == 0;
        n++;
    }
    closedir(d);
    s_stats.kept = n;
    // One-shot diagnosis aid: shows whether the root dir is truly empty of
    // sessions, or whether entries are being dropped by filter/stat.
    ESP_LOGI(TAG, "scan: dir_entries=%d survey_csv_named=%d stat_fail=%d "
                  "empty=%d kept=%d", s_stats.dir_entries, s_stats.survey_named,
             s_stats.stat_fail, s_stats.empty, s_stats.kept);

    // Newest first: mtime desc, file name desc as tiebreak.
    for (int i = 1; i < n; i++) {
        CsvSession key = out[i];
        int j = i - 1;
        while (j >= 0 &&
               (out[j].mtime < key.mtime ||
                (out[j].mtime == key.mtime && strcmp(out[j].name, key.name) < 0))) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}

const CsvScanStats* csv_last_scan_stats(void)
{
    return &s_stats;
}

bool csv_parse_row(char* line, ApRec* r)
{
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) return false;

    // From the end: cut[0..5] are the commas preceding fix_q, sats, lon,
    // lat, rssi, channel respectively.
    char* cut[6];
    char* p = line + strlen(line);
    for (int i = 0; i < 6; i++) {
        while (p > line && *p != ',') p--;
        if (p == line) return false;
        cut[i] = p;
        if (i < 5) p--;
    }
    int fix_q, sats;
    double lat, lon;
    int channel, rssi;
    if (sscanf(cut[0] + 1, "%d", &fix_q) != 1) return false;
    *cut[0] = 0;
    if (sscanf(cut[1] + 1, "%d", &sats) != 1) return false;
    *cut[1] = 0;
    if (sscanf(cut[2] + 1, "%lf", &lon) != 1) return false;
    *cut[2] = 0;
    if (sscanf(cut[3] + 1, "%lf", &lat) != 1) return false;
    *cut[3] = 0;
    if (sscanf(cut[4] + 1, "%d", &rssi) != 1) return false;
    *cut[4] = 0;
    if (sscanf(cut[5] + 1, "%d", &channel) != 1) return false;
    *cut[5] = 0;

    // Prefix is now "timestamp,mac,ssid,authmode". First two commas are
    // unambiguous; the LAST comma separates ssid from authmode.
    char* c_ts   = strchr(line, ',');
    if (!c_ts) return false;
    char* c_mac  = strchr(c_ts + 1, ',');
    if (!c_mac) return false;
    char* c_auth = strrchr(c_mac + 1, ',');
    if (!c_auth) return false;

    *c_ts  = 0;
    *c_mac = 0;
    *c_auth = 0;
    snprintf(r->ts,  sizeof(r->ts),  "%s", line);
    snprintf(r->mac, sizeof(r->mac), "%s", c_ts + 1);
    snprintf(r->ssid, sizeof(r->ssid), "%s", c_mac + 1);
    snprintf(r->auth, sizeof(r->auth), "%s", c_auth + 1);
    r->channel = channel;
    r->rssi    = rssi;
    r->lat     = lat;
    r->lon     = lon;
    r->sats    = sats;
    (void)fix_q;
    return r->mac[0] != 0;
}

void aptable_reset(ApTable* t)
{
    t->n = 0;
}

ApRec* aptable_add(ApTable* t, const ApRec* r)
{
    for (int i = 0; i < t->n; i++) {
        if (strcmp(t->recs[i].mac, r->mac) == 0) return &t->recs[i];
    }
    if (t->n == t->cap) {
        int newcap = t->cap ? t->cap * 2 : 64;
        ApRec* np = (ApRec*)heap_caps_realloc(
            t->recs, (size_t)newcap * sizeof(ApRec),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!np) {
            ESP_LOGE(TAG, "PSRAM alloc failed growing to %d APs", newcap);
            return nullptr;
        }
        t->recs = np;
        t->cap = newcap;
    }
    t->recs[t->n] = *r;
    return &t->recs[t->n++];
}

void aptable_free(ApTable* t)
{
    if (t->recs) heap_caps_free(t->recs);
    t->recs = nullptr;
    t->n = t->cap = 0;
}
