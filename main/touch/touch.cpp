#include "touch.h"

#include "board_pins.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char* TAG = "touch";

// XPT2046 command bytes: 12-bit conversion, power-down between conversions
static constexpr uint8_t CMD_X  = 0xD0;
static constexpr uint8_t CMD_Y  = 0x90;
static constexpr uint8_t CMD_Z1 = 0xB0;

// Below this Z1 raw value the stylus is considered off the panel
static constexpr uint16_t PRESS_THRESHOLD = 100;

static spi_device_handle_t s_dev;

// One control byte out, two result bytes back; CS stays low for the whole frame
static uint16_t read_channel(uint8_t cmd)
{
    spi_transaction_t t = {};
    t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    t.length = 3 * 8;
    t.tx_data[0] = cmd;
    if (spi_device_transmit(s_dev, &t) != ESP_OK) {
        return 0;
    }
    // 12-bit result is left-justified in the two response bytes
    return ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;
}

static uint16_t read_averaged(uint8_t cmd)
{
    uint32_t acc = 0;
    for (int i = 0; i < 3; i++) {
        acc += read_channel(cmd);
    }
    return acc / 3;
}

// Linear map between two measured anchors, clamped to [0, out_max]
static int16_t map_anchored(int32_t v, int32_t raw_hi, int32_t raw_lo,
                            int16_t out_lo, int16_t out_hi, int16_t out_max)
{
    if (v >= raw_hi) return out_lo;
    if (v <= raw_lo) return out_hi;
    int32_t r = out_lo + (raw_hi - v) * (out_hi - out_lo) / (raw_hi - raw_lo);
    if (r < 0) return 0;
    if (r > out_max) return out_max;
    return (int16_t)r;
}

esp_err_t touch_init(void)
{
    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = SSP_TOUCH_SPI_FREQ_HZ;  // 2.5 MHz; display runs at 20 MHz
    dev.mode = 0;
    dev.spics_io_num = SSP_TOUCH_CS;
    dev.queue_size = 1;
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &dev, &s_dev),
                        TAG, "XPT2046 device add failed");

    ESP_LOGI(TAG, "XPT2046 up on SPI2 @ %lu kHz", SSP_TOUCH_SPI_FREQ_HZ / 1000UL);
    return ESP_OK;
}

bool touch_read_raw(uint16_t* raw_x, uint16_t* raw_y)
{
    if (read_channel(CMD_Z1) < PRESS_THRESHOLD) {
        return false;
    }
    *raw_x = read_averaged(CMD_X);
    *raw_y = read_averaged(CMD_Y);
    return true;
}

bool touch_read(int16_t* x, int16_t* y)
{
    uint16_t raw_x, raw_y;
    if (!touch_read_raw(&raw_x, &raw_y)) {
        return false;
    }

    // Measured mapping (board_pins.h): raw Y -> screen X, raw X -> screen Y,
    // both channels decrease as the screen coordinate increases
    *x = map_anchored(raw_y, SSP_TOUCH_RAWY_LEFT, SSP_TOUCH_RAWY_RIGHT,
                      SSP_TOUCH_ANCHOR_MIN, SSP_TOUCH_ANCHOR_MAX_X, SSP_TFT_WIDTH - 1);
    *y = map_anchored(raw_x, SSP_TOUCH_RAWX_TOP, SSP_TOUCH_RAWX_BOTTOM,
                      SSP_TOUCH_ANCHOR_MIN, SSP_TOUCH_ANCHOR_MAX_Y, SSP_TFT_HEIGHT - 1);
    return true;
}
