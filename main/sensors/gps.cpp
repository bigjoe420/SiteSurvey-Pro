#include "gps.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctype.h>
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"

static const char* TAG = "gps";

static constexpr size_t RX_BUF = 1024;
static constexpr size_t NMEA_LINE_MAX = 96;

static char s_line[NMEA_LINE_MAX];
static size_t s_len;

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// "ddmm.mmmm" style NMEA coordinate -> degrees x 1e7
static int32_t nmea_coord(const char* field, char hemi)
{
    if (!field[0]) return 0;
    double raw = strtod(field, nullptr);
    int deg = (int)(raw / 100.0);
    double dec = deg + (raw - deg * 100.0) / 60.0;
    if (hemi == 'S' || hemi == 'W') dec = -dec;
    return (int32_t)(dec * 1e7 + (dec >= 0 ? 0.5 : -0.5));
}

static void parse_gga(char** f, int nfields, GpsState* st)
{
    if (nfields < 8) return;
    st->fix_quality = (uint8_t)atoi(f[6]);
    st->sats = (uint8_t)atoi(f[7]);
    st->fix_valid = st->fix_quality > 0;
    if (st->fix_valid && f[2][0] && f[4][0]) {
        st->lat_e7 = nmea_coord(f[2], f[3][0]);
        st->lon_e7 = nmea_coord(f[4], f[5][0]);
    }
}

static void parse_rmc(char** f, int nfields, GpsState* st)
{
    if (nfields < 3) return;
    if (strlen(f[1]) >= 6) {
        char t[7];
        memcpy(t, f[1], 6);
        t[6] = 0;
        st->utc_hhmmss = (uint32_t)atoi(t);
    }
}

// Splitter that keeps empty fields (",,") — strtok would collapse them and
// shift every index, which is exactly wrong for no-fix NMEA sentences.
static int split_fields(char* line, char** f, int maxf)
{
    int nf = 0;
    f[nf++] = line;
    for (char* p = line; *p && nf < maxf; p++) {
        if (*p == ',') {
            *p = 0;
            f[nf++] = p + 1;
        }
    }
    return nf;
}

static void parse_sentence(char* line, GpsState* st)
{
    // line: "$.....*HH" without CR/LF
    char* star = strchr(line, '*');
    if (!star || strlen(star) < 3) { st->sentences_bad++; return; }

    uint8_t cks = 0;
    for (char* p = line + 1; p < star; p++) cks ^= (uint8_t)*p;
    int h1 = hexv(star[1]), h2 = hexv(star[2]);
    if (h1 < 0 || h2 < 0 || cks != (uint8_t)((h1 << 4) | h2)) {
        st->sentences_bad++;
        return;
    }
    st->sentences_ok++;

    *star = 0;
    char* f[20];
    int nf = split_fields(line, f, 20);
    if (nf < 1 || strlen(f[0]) != 6) return;  // "$GNGGA" style header

    const char* type = f[0] + 3;  // skip 2-char talker id
    if (strcmp(type, "GGA") == 0) parse_gga(f, nf, st);
    else if (strcmp(type, "RMC") == 0) parse_rmc(f, nf, st);
}

esp_err_t gps_init(void)
{
    uart_config_t cfg = {};
    cfg.baud_rate = SSP_GPS_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.lp_source_clk = LP_UART_SCLK_DEFAULT;  // LP UART clock, not the HP source_clk field

    esp_err_t err = uart_driver_install(SSP_GPS_UART, RX_BUF, 0, 0, nullptr, 0);
    if (err != ESP_OK) return err;
    err = uart_param_config(SSP_GPS_UART, &cfg);
    if (err != ESP_OK) return err;
    err = uart_set_pin(SSP_GPS_UART, SSP_GPS_TX, SSP_GPS_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "LP-UART up: RX=GPIO%d TX=GPIO%d @ %d 8N1", SSP_GPS_RX, SSP_GPS_TX, SSP_GPS_BAUD);
    return ESP_OK;
}

void gps_poll(GpsState* st)
{
    uint8_t buf[128];
    int n = uart_read_bytes(SSP_GPS_UART, buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (n > 0) {
        st->rx_bytes += (uint32_t)n;
        // Hex+ASCII dump of the first few RX chunks: tells
        // a silent or mis-bauded module apart from a floating RX pin
        static int s_dump_budget = 4;
        if (s_dump_budget > 0) {
            s_dump_budget--;
            char hex[384];
            char txt[132];
            int off = 0;
            for (int i = 0; i < n; i++) {
                off += snprintf(hex + off, sizeof(hex) - off, "%02X ", buf[i]);
                txt[i] = isprint(buf[i]) ? (char)buf[i] : '.';
            }
            txt[n] = 0;
            ESP_LOGI(TAG, "rx chunk[%d] hex: %s", n, hex);
            ESP_LOGI(TAG, "rx chunk[%d] asc: %s", n, txt);
        }
    }
    for (int i = 0; i < n; i++) {
        char c = (char)buf[i];
        if (c == '$') {
            s_len = 0;
            s_line[s_len++] = c;
        } else if (c == '\r' || c == '\n') {
            if (s_len > 1) {
                s_line[s_len] = 0;
                parse_sentence(s_line, st);
            }
            s_len = 0;
        } else if (s_len > 0) {
            if (s_len < NMEA_LINE_MAX - 1) {
                s_line[s_len++] = c;
            } else {
                s_len = 0;  // oversized line — drop
            }
        }
    }
}
