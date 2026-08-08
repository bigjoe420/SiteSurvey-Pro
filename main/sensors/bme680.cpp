#include "bme680.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"

static const char* TAG = "bme680";

// Register map per docs/datasheets/bst-bme680-ds001.pdf §5. Compensation math
// ported from Bosch BME68x-Sensor-API (BSD-3), which the datasheet cites as
// the canonical reference for T/P/H compensation (§3.4).
static constexpr uint8_t REG_COEFF3     = 0x00;  // 5 bytes: res_heat_val/range, range_sw_err
static constexpr uint8_t REG_FIELD0     = 0x1D;  // meas status + T/P/H/gas data block
static constexpr uint8_t REG_RES_HEAT0  = 0x5A;
static constexpr uint8_t REG_GAS_WAIT0  = 0x64;
static constexpr uint8_t REG_CTRL_GAS_0 = 0x70;
static constexpr uint8_t REG_CTRL_GAS_1 = 0x71;
static constexpr uint8_t REG_CTRL_HUM   = 0x72;
static constexpr uint8_t REG_CTRL_MEAS  = 0x74;
static constexpr uint8_t REG_CONFIG     = 0x75;
static constexpr uint8_t REG_COEFF1     = 0x8A;  // 23 bytes
static constexpr uint8_t REG_CHIP_ID    = 0xD0;
static constexpr uint8_t REG_RESET      = 0xE0;
static constexpr uint8_t REG_COEFF2     = 0xE1;  // 14 bytes
static constexpr uint8_t REG_VARIANT    = 0xF0;

static constexpr uint8_t CHIP_ID     = 0x61;
static constexpr uint8_t RESET_CMD   = 0xB6;
static constexpr uint8_t MODE_FORCED = 0x01;
static constexpr uint8_t OSRS_X2     = 0x02;

static constexpr uint16_t HEATER_TARGET_C = 300;
static constexpr uint16_t HEATER_WAIT_MS  = 150;
static constexpr uint8_t  AMBIENT_C       = 25;  // heater compensation reference ambient

typedef struct {
    uint16_t par_t1;  int16_t par_t2;  int8_t par_t3;
    uint16_t par_p1;  int16_t par_p2;  int8_t par_p3;
    int16_t par_p4;   int16_t par_p5;  int8_t par_p6;  int8_t par_p7;
    int16_t par_p8;   int16_t par_p9;  uint8_t par_p10;
    uint16_t par_h1;  uint16_t par_h2; int8_t par_h3;  int8_t par_h4;
    int8_t par_h5;    uint8_t par_h6;  int8_t par_h7;
    int8_t par_gh1;   int16_t par_gh2; int8_t par_gh3;
    int8_t res_heat_val;
    uint8_t res_heat_range;
    int8_t range_sw_err;
    int32_t t_fine;
} Calib;

static i2c_master_dev_handle_t s_dev;
static Calib s_c;
static bool s_gas_high_variant;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t rd(uint8_t reg, uint8_t* out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, 100);
}

// --- Integer compensation, ported 1:1 from BME68x-Sensor-API bme68x.c -------

static int16_t calc_temp(uint32_t adc)
{
    int64_t var1 = ((int32_t)adc >> 3) - ((int32_t)s_c.par_t1 << 1);
    int64_t var2 = (var1 * (int32_t)s_c.par_t2) >> 11;
    int64_t var3 = ((var1 >> 1) * (var1 >> 1)) >> 12;
    var3 = (var3 * ((int32_t)s_c.par_t3 << 4)) >> 14;
    s_c.t_fine = (int32_t)(var2 + var3);
    return (int16_t)(((s_c.t_fine * 5) + 128) >> 8);
}

static uint32_t calc_press(uint32_t adc)
{
    const int32_t ovf = INT32_C(0x40000000);
    int32_t var1 = (s_c.t_fine >> 1) - 64000;
    int32_t var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)s_c.par_p6) >> 2;
    var2 += (var1 * (int32_t)s_c.par_p5) << 1;
    var2 = (var2 >> 2) + ((int32_t)s_c.par_p4 << 16);
    var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) * ((int32_t)s_c.par_p3 << 5)) >> 3)
         + (((int32_t)s_c.par_p2 * var1) >> 1);
    var1 >>= 18;
    var1 = ((32768 + var1) * (int32_t)s_c.par_p1) >> 15;
    int32_t p = 1048576 - (int32_t)adc;
    p = (int32_t)(((uint32_t)(p - (var2 >> 12))) * (uint32_t)3125);
    p = (p >= ovf) ? ((p / var1) << 1) : ((p << 1) / var1);
    var1 = ((int32_t)s_c.par_p9 * (int32_t)(((p >> 3) * (p >> 3)) >> 13)) >> 12;
    var2 = ((p >> 2) * (int32_t)s_c.par_p8) >> 13;
    int32_t var3 = ((p >> 8) * (p >> 8) * (p >> 8) * (int32_t)s_c.par_p10) >> 17;
    return (uint32_t)(p + ((var1 + var2 + var3 + ((int32_t)s_c.par_p7 << 7)) >> 4));
}

static uint32_t calc_hum(uint16_t adc)
{
    int32_t t = ((s_c.t_fine * 5) + 128) >> 8;
    int32_t var1 = (int32_t)adc - ((int32_t)s_c.par_h1 * 16)
                 - (((t * (int32_t)s_c.par_h3) / 100) >> 1);
    int32_t var2 = ((int32_t)s_c.par_h2 * (((t * (int32_t)s_c.par_h4) / 100)
                 + (((t * ((t * (int32_t)s_c.par_h5) / 100)) >> 6) / 100)
                 + (1 << 14))) >> 10;
    int32_t var3 = var1 * var2;
    int32_t var4 = ((int32_t)s_c.par_h6 << 7) + ((t * (int32_t)s_c.par_h7) / 100);
    var4 >>= 4;
    int32_t var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;
    int32_t var6 = (var4 * var5) >> 1;
    int32_t hum = (((var3 + var6) >> 10) * 1000) >> 12;
    if (hum > 100000) hum = 100000;
    if (hum < 0) hum = 0;
    return (uint32_t)hum;
}

static uint32_t calc_gas_low(uint16_t adc, uint8_t range)
{
    static const uint32_t k1[16] = {
        2147483647u, 2147483647u, 2147483647u, 2147483647u, 2147483647u,
        2126008810u, 2147483647u, 2130303777u, 2147483647u, 2147483647u,
        2143188679u, 2136746228u, 2147483647u, 2126008810u, 2147483647u,
        2147483647u,
    };
    static const uint32_t k2[16] = {
        4096000000u, 2048000000u, 1024000000u, 512000000u, 255744255u,
        127110228u, 64000000u, 32258064u, 16016016u, 8000000u,
        4000000u, 2000000u, 1000000u, 500000u, 250000u, 125000u,
    };
    int64_t var1 = ((1340 + 5 * (int64_t)s_c.range_sw_err) * (int64_t)k1[range]) >> 16;
    int64_t var2 = ((int64_t)adc << 15) - INT64_C(16777216) + var1;
    int64_t var3 = ((int64_t)k2[range] * var1) >> 9;
    return (uint32_t)((var3 + (var2 >> 1)) / var2);
}

static uint8_t calc_res_heat(uint16_t target_c)
{
    if (target_c > 400) target_c = 400;
    int32_t var1 = (((int32_t)AMBIENT_C * s_c.par_gh3) / 1000) * 256;
    int32_t var2 = (s_c.par_gh1 + 784)
                 * (((((s_c.par_gh2 + 154009) * (int32_t)target_c * 5) / 100) + 3276800) / 10);
    int32_t var3 = var1 + (var2 / 2);
    int32_t var4 = var3 / ((int32_t)s_c.res_heat_range + 4);
    int32_t var5 = 131 * (int32_t)s_c.res_heat_val + 65536;
    return (uint8_t)(((((var4 / var5) - 250) * 34) + 50) / 100);
}

static uint8_t calc_gas_wait(uint16_t dur_ms)
{
    uint8_t factor = 0;
    if (dur_ms >= 0xFC0) return 0xFF;
    while (dur_ms > 0x3F) {
        dur_ms /= 4;
        factor++;
    }
    return (uint8_t)(dur_ms + factor * 64);
}

static esp_err_t load_calib(void)
{
    uint8_t c[42];
    ESP_RETURN_ON_ERROR(rd(REG_COEFF1, c, 23), TAG, "coeff1 read failed");
    ESP_RETURN_ON_ERROR(rd(REG_COEFF2, c + 23, 14), TAG, "coeff2 read failed");
    ESP_RETURN_ON_ERROR(rd(REG_COEFF3, c + 37, 5), TAG, "coeff3 read failed");

    s_c.par_t1 = (uint16_t)(c[32] << 8 | c[31]);
    s_c.par_t2 = (int16_t)(c[1] << 8 | c[0]);
    s_c.par_t3 = (int8_t)c[2];
    s_c.par_p1 = (uint16_t)(c[5] << 8 | c[4]);
    s_c.par_p2 = (int16_t)(c[7] << 8 | c[6]);
    s_c.par_p3 = (int8_t)c[8];
    s_c.par_p4 = (int16_t)(c[11] << 8 | c[10]);
    s_c.par_p5 = (int16_t)(c[13] << 8 | c[12]);
    s_c.par_p6 = (int8_t)c[15];
    s_c.par_p7 = (int8_t)c[14];
    s_c.par_p8 = (int16_t)(c[19] << 8 | c[18]);
    s_c.par_p9 = (int16_t)(c[21] << 8 | c[20]);
    s_c.par_p10 = c[22];
    s_c.par_h1 = (uint16_t)((c[25] << 4) | (c[24] & 0x0F));
    s_c.par_h2 = (uint16_t)((c[23] << 4) | (c[24] >> 4));
    s_c.par_h3 = (int8_t)c[26];
    s_c.par_h4 = (int8_t)c[27];
    s_c.par_h5 = (int8_t)c[28];
    s_c.par_h6 = c[29];
    s_c.par_h7 = (int8_t)c[30];
    s_c.par_gh1 = (int8_t)c[35];
    s_c.par_gh2 = (int16_t)(c[34] << 8 | c[33]);
    s_c.par_gh3 = (int8_t)c[36];
    s_c.res_heat_val = (int8_t)c[37];
    s_c.res_heat_range = (c[39] & 0x30) >> 4;
    s_c.range_sw_err = (int8_t)(c[41] & 0xF0) >> 4;
    return ESP_OK;
}

esp_err_t bme680_init(i2c_master_bus_handle_t bus)
{
    uint16_t addr = 0;
    if (i2c_master_probe(bus, SSP_BME680_ADDR_0, 150) == ESP_OK) {
        addr = SSP_BME680_ADDR_0;
    } else if (i2c_master_probe(bus, SSP_BME680_ADDR_1, 150) == ESP_OK) {
        addr = SSP_BME680_ADDR_1;
    }
    if (!addr) {
        ESP_LOGW(TAG, "no BME680 on CN1 (0x76/0x77 silent) - env channel disabled");
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr;
    dev_cfg.scl_speed_hz = SSP_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add device failed");

    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(rd(REG_CHIP_ID, &id, 1), TAG, "chip id read failed");
    if (id != CHIP_ID) {
        ESP_LOGW(TAG, "0x%02X answered but chip id 0x%02X != 0x61", addr, id);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(wr(REG_RESET, RESET_CMD), TAG, "soft reset failed");
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_RETURN_ON_ERROR(load_calib(), TAG, "calib load failed");

    uint8_t variant = 0;
    ESP_RETURN_ON_ERROR(rd(REG_VARIANT, &variant, 1), TAG, "variant read failed");
    s_gas_high_variant = (variant & 0x01);
    if (s_gas_high_variant) {
        ESP_LOGW(TAG, "gas-high variant (BME688) - gas channel unsupported in this build");
    }

    ESP_RETURN_ON_ERROR(wr(REG_RES_HEAT0, calc_res_heat(HEATER_TARGET_C)), TAG, "heater set failed");
    ESP_RETURN_ON_ERROR(wr(REG_GAS_WAIT0, calc_gas_wait(HEATER_WAIT_MS)), TAG, "gas wait failed");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL_GAS_0, 0x00), TAG, "gas ctrl failed");   // heater on
    ESP_RETURN_ON_ERROR(wr(REG_CONFIG, 0x00), TAG, "config failed");        // IIR off

    ESP_LOGI(TAG, "up at 0x%02X: T/P/H forced x2, heater %u C / %u ms", addr, HEATER_TARGET_C, HEATER_WAIT_MS);
    return ESP_OK;
}

esp_err_t bme680_read(Bme680Data* out)
{
    ESP_RETURN_ON_ERROR(wr(REG_CTRL_HUM, OSRS_X2), TAG, "hum ctrl failed");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL_GAS_1, 1 << 4), TAG, "gas run failed");  // run_gas, nb_conv=0
    ESP_RETURN_ON_ERROR(wr(REG_CTRL_MEAS, (OSRS_X2 << 5) | (OSRS_X2 << 2) | MODE_FORCED),
                        TAG, "trigger failed");

    vTaskDelay(pdMS_TO_TICKS(HEATER_WAIT_MS + 50));  // T/P/H conversion, then plate heating

    uint8_t f[15];
    ESP_RETURN_ON_ERROR(rd(REG_FIELD0, f, sizeof(f)), TAG, "field read failed");
    if (!(f[0] & 0x80)) {
        ESP_LOGW(TAG, "no new data after forced shot");
        return ESP_ERR_NOT_FINISHED;
    }

    uint32_t adc_p = ((uint32_t)f[2] << 12) | ((uint32_t)f[3] << 4) | (f[4] >> 4);
    uint32_t adc_t = ((uint32_t)f[5] << 12) | ((uint32_t)f[6] << 4) | (f[7] >> 4);
    uint16_t adc_h = (uint16_t)((f[8] << 8) | f[9]);

    out->temp_c_x100 = calc_temp(adc_t);
    out->press_pa = calc_press(adc_p);
    out->hum_x100 = calc_hum(adc_h) / 10;

    bool gas_ok = (f[14] & 0x20) && !s_gas_high_variant;  // gas_valid_r
    if (gas_ok && !(f[14] & 0x10)) {                      // heat_stab_r
        ESP_LOGD(TAG, "heater not stable yet");
    }
    if (gas_ok) {
        uint16_t adc_g = (uint16_t)((f[13] << 2) | (f[14] >> 6));
        out->gas_ohm = calc_gas_low(adc_g, f[14] & 0x0F);
    } else {
        out->gas_ohm = 0;
    }
    return ESP_OK;
}
