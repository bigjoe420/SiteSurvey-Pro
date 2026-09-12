#include "kml_export.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "sd_card.h"
#include "session_logger.h"
#include "session_csv.h"

static const char* TAG = "kml";
#define LINE_LEN 256

// XML escaping for KML text nodes
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
    char csv_path[128];
    if (!csv_find_active(csv_path, sizeof(csv_path))) {
        ESP_LOGW(TAG, "no session CSV found");
        return ESP_ERR_NOT_FOUND;
    }
    return kml_export_path(csv_path, out_path, out_path_len,
                           out_placemarks, out_no_fix);
}

esp_err_t kml_export_path(const char* csv_path, char* out_path, size_t out_path_len,
                          int* out_placemarks, int* out_no_fix)
{
    if (!sd_card_present()) {
        ESP_LOGW(TAG, "SD card absent");
        return ESP_ERR_NOT_FOUND;
    }
    if (!csv_path || !csv_path[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    // Output: same name with .kml
    const char* slash = strrchr(csv_path, '/');
    const char* base = slash ? slash + 1 : csv_path;
    snprintf(out_path, out_path_len, "/sdcard/%.*s.kml",
             (int)(strlen(base) - 4), base);

    // Make sure the active session (if any) has flushed its buffer first.
    session_logger_flush();

    FILE* fin = fopen(csv_path, "r");
    if (!fin) {
        ESP_LOGW(TAG, "fopen failed: %s", csv_path);
        return ESP_ERR_NOT_FOUND;
    }

    char line[LINE_LEN];
    int rows = 0, bad = 0, nofix = 0;
    ApTable table = {};
    if (!fgets(line, sizeof(line), fin)) {  // header
        fclose(fin);
        return ESP_ERR_INVALID_RESPONSE;
    }
    while (fgets(line, sizeof(line), fin)) {
        rows++;
        ApRec r;
        if (!csv_parse_row(line, &r)) { bad++; continue; }
        if (r.lat == 0.0 && r.lon == 0.0) { nofix++; continue; }
        ApRec* slot = aptable_add(&table, &r);
        if (!slot) { fclose(fin); aptable_free(&table); return ESP_ERR_NO_MEM; }
        if (r.rssi > slot->rssi) slot->rssi = r.rssi;
    }
    fclose(fin);

    FILE* fout = fopen(out_path, "w");
    if (!fout) {
        ESP_LOGW(TAG, "fopen write failed: %s", out_path);
        aptable_free(&table);
        return ESP_ERR_NOT_FOUND;
    }

    fprintf(fout, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(fout, "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n");
    fprintf(fout, "<name>SiteSurvey Pro - %s</name>\n", base);
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
    for (int i = 0; i < table.n; i++) {
        const ApRec* a = &table.recs[i];
        xml_escape(esc, sizeof(esc), a->ssid);
        char lon_s[24], lat_s[24];
        fmt_deg7(lon_s, sizeof(lon_s), a->lon);
        fmt_deg7(lat_s, sizeof(lat_s), a->lat);
        fprintf(fout, "<Placemark>\n<name>%s</name>\n", esc);
        fprintf(fout,
            "<description><![CDATA[MAC: %s<br/>Channel: %d<br/>RSSI: %d dBm<br/>"
            "Security: %s<br/>First seen: %s<br/>Sats: %d]]></description>\n",
            a->mac, a->channel, a->rssi, a->auth, a->ts, a->sats);
        fprintf(fout, "<styleUrl>%s</styleUrl>\n", style_for_rssi(a->rssi));
        fprintf(fout, "<Point><coordinates>%s,%s,0</coordinates></Point>\n",
                lon_s, lat_s);
        fprintf(fout, "</Placemark>\n");
    }
    fprintf(fout, "</Document>\n</kml>\n");
    fclose(fout);

    ESP_LOGI(TAG, "KML: %d placemarks (%d rows, %d no-fix, %d bad) -> %s",
             table.n, rows, nofix, bad, out_path);
    if (out_placemarks) *out_placemarks = table.n;
    if (out_no_fix)     *out_no_fix = nofix;
    aptable_free(&table);
    return ESP_OK;
}
