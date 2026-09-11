#include "kml_export.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "sd_card.h"
#include "session_logger.h"

static const char* TAG = "kml";
#define SD_DIR    "/sdcard"
#define LINE_LEN  256

// CSV columns: timestamp,mac,ssid,authmode,channel,rssi,lat,lon,sats,fix_q
typedef struct {
    char   ts[24];
    char   mac[20];
    char   ssid[44];
    char   auth[20];
    int    channel;
    int    rssi;
    double lat;
    double lon;
    int    sats;
} ApRec;

static ApRec* s_aps = nullptr;   // PSRAM
static int    s_aps_n = 0;
static int    s_aps_cap = 0;

// ---------------------------------------------------------------------------
// Find newest non-empty /sdcard/survey_*.csv (name embeds a sortable
// timestamp). Zero-byte files exist: a session that crashed before its first
// flush leaves an empty CSV behind, and exporting one is meaningless.
// ---------------------------------------------------------------------------
static bool find_latest_csv(char* out, size_t n)
{
    DIR* d = opendir(SD_DIR);
    if (!d) return false;
    out[0] = 0;
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
        if (out[0] == 0 || strcmp(nm, out) > 0) {
            snprintf(out, n, "%s", nm);
        }
    }
    closedir(d);
    return out[0] != 0;
}

// ---------------------------------------------------------------------------
// Parse one CSV line, right-anchored so commas inside the SSID are safe.
// Returns false on malformed / partial lines (skipped silently by caller).
// ---------------------------------------------------------------------------
static bool parse_row(char* line, ApRec* r)
{
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) return false;

    // From the end: c0..c5 are the commas preceding fix_q, sats, lon, lat,
    // rssi, channel respectively.
    char* cut[6];
    char* p = line + strlen(line);
    for (int i = 0; i < 6; i++) {
        while (p > line && *p != ',') p--;
        if (p == line) return false;
        cut[i] = p;
        if (i < 5) p--;
    }
    // Terminate each numeric field and parse it.
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

// ---------------------------------------------------------------------------
// Dedup table (PSRAM): one slot per unique BSSID
// ---------------------------------------------------------------------------
static ApRec* ap_find_or_add(const ApRec* r)
{
    for (int i = 0; i < s_aps_n; i++) {
        if (strcmp(s_aps[i].mac, r->mac) == 0) return &s_aps[i];
    }
    if (s_aps_n == s_aps_cap) {
        int newcap = s_aps_cap ? s_aps_cap * 2 : 64;
        ApRec* np = (ApRec*)heap_caps_realloc(
            s_aps, (size_t)newcap * sizeof(ApRec),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!np) {
            ESP_LOGE(TAG, "PSRAM alloc failed growing to %d APs", newcap);
            return nullptr;
        }
        s_aps = np;
        s_aps_cap = newcap;
    }
    s_aps[s_aps_n] = *r;
    return &s_aps[s_aps_n++];
}

static void xml_escape(char* dst, size_t n, const char* src)
{
    size_t o = 0;
    for (const char* s = src; *s && o + 7 < n; s++) {
        const char* rep = nullptr;
        switch (*s) {
        case '&':  rep = "&amp;";  break;
        case '<':  rep = "&lt;";   break;
        case '>':  rep = "&gt;";   break;
        case '"':  rep = "&quot;"; break;
        default: break;
        }
        if (rep) {
            size_t l = strlen(rep);
            memcpy(dst + o, rep, l);
            o += l;
        } else {
            dst[o++] = *s;
        }
    }
    dst[o] = 0;
}

// RSSI tier -> KML icon color (aabbggrr)
static const char* style_for_rssi(int rssi)
{
    if (rssi >= -50) return "#rssiS";   // green
    if (rssi >= -65) return "#rssiM";   // yellow
    if (rssi >= -80) return "#rssiW";   // orange
    return "#rssiX";                    // red
}

esp_err_t kml_export_latest(char* out_path, size_t out_path_len,
                            int* out_placemarks, int* out_no_fix)
{
    if (!sd_card_present()) {
        ESP_LOGW(TAG, "SD card absent");
        return ESP_ERR_NOT_FOUND;
    }

    char name[96];
    if (!find_latest_csv(name, sizeof(name))) {
        ESP_LOGW(TAG, "no session CSV found in %s", SD_DIR);
        return ESP_ERR_NOT_FOUND;
    }
    char csv_path[128];
    snprintf(csv_path, sizeof(csv_path), "%s/%s", SD_DIR, name);
    snprintf(out_path, out_path_len, "%s/%.*s.kml", SD_DIR,
             (int)(strlen(name) - 4), name);

    // Make sure the active session (if any) has flushed its buffer first.
    session_logger_flush();

    FILE* fin = fopen(csv_path, "r");
    if (!fin) {
        ESP_LOGW(TAG, "fopen failed: %s", csv_path);
        return ESP_ERR_NOT_FOUND;
    }

    char line[LINE_LEN];
    int rows = 0, bad = 0, nofix = 0;
    s_aps_n = 0;
    if (!fgets(line, sizeof(line), fin)) {  // header
        fclose(fin);
        return ESP_ERR_INVALID_RESPONSE;
    }
    while (fgets(line, sizeof(line), fin)) {
        rows++;
        ApRec r;
        if (!parse_row(line, &r)) { bad++; continue; }
        if (r.lat == 0.0 && r.lon == 0.0) { nofix++; continue; }
        ApRec* slot = ap_find_or_add(&r);
        if (!slot) { fclose(fin); return ESP_ERR_NO_MEM; }
        if (r.rssi > slot->rssi) slot->rssi = r.rssi;
    }
    fclose(fin);

    FILE* fout = fopen(out_path, "w");
    if (!fout) {
        ESP_LOGW(TAG, "fopen write failed: %s", out_path);
        return ESP_ERR_NOT_FOUND;
    }

    fprintf(fout, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(fout, "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n");
    fprintf(fout, "<name>SiteSurvey Pro - %s</name>\n", name);
    fprintf(fout,
        "<Style id=\"rssiS\"><IconStyle><color>ff00ff00</color><scale>0.9</scale>"
        "</IconStyle></Style>\n"
        "<Style id=\"rssiM\"><IconStyle><color>ff00ffff</color><scale>0.8</scale>"
        "</IconStyle></Style>\n"
        "<Style id=\"rssiW\"><IconStyle><color>ff0080ff</color><scale>0.7</scale>"
        "</IconStyle></Style>\n"
        "<Style id=\"rssiX\"><IconStyle><color>ff0000ff</color><scale>0.6</scale>"
        "</IconStyle></Style>\n");

    char esc[96];
    for (int i = 0; i < s_aps_n; i++) {
        const ApRec* a = &s_aps[i];
        xml_escape(esc, sizeof(esc), a->ssid);
        fprintf(fout, "<Placemark>\n<name>%s</name>\n", esc);
        fprintf(fout,
            "<description><![CDATA[MAC: %s<br/>Channel: %d<br/>RSSI: %d dBm<br/>"
            "Security: %s<br/>First seen: %s<br/>Sats: %d]]></description>\n",
            a->mac, a->channel, a->rssi, a->auth, a->ts, a->sats);
        fprintf(fout, "<styleUrl>%s</styleUrl>\n", style_for_rssi(a->rssi));
        fprintf(fout, "<Point><coordinates>%.7f,%.7f,0</coordinates></Point>\n",
                a->lon, a->lat);
        fprintf(fout, "</Placemark>\n");
    }
    fprintf(fout, "</Document>\n</kml>\n");
    fclose(fout);

    ESP_LOGI(TAG, "KML: %d placemarks (%d rows, %d no-fix, %d bad) -> %s",
             s_aps_n, rows, nofix, bad, out_path);
    if (out_placemarks) *out_placemarks = s_aps_n;
    if (out_no_fix)     *out_no_fix = nofix;
    return ESP_OK;
}
