#include "touch.h"

#include <cstring>
#include "board_pins.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "touch";

// XPT2046 command bytes: 12-bit conversion, power-down between conversions
static constexpr uint8_t CMD_X  = 0xD0;
static constexpr uint8_t CMD_Y  = 0x90;
static constexpr uint8_t CMD_Z1 = 0xB0;

// Below this Z1 raw value the stylus is considered off the panel
static constexpr uint16_t PRESS_THRESHOLD = 100;

static spi_device_handle_t s_dev;

// ---- Background sampler state (lock-free via critical section) ----
static int16_t s_sample_x;
static int16_t s_sample_y;
static bool    s_sample_pressed;
static portMUX_TYPE s_sample_mux = portMUX_INITIALIZER_UNLOCKED;

// One control byte out, two result bytes back; CS stays low for the whole frame
static uint16_t read_channel(uint8_t cmd)
{
    // DMA-capable STATIC buffers, never the transaction's inline arrays.
    // The sampler task's stack lives in PSRAM (xTaskCreateWithCaps), so with
    // SPI_TRANS_USE_TXDATA/RXDATA the tx/rx inline arrays were NOT DMA-
    // capable and the driver alloc/freed a "priv" internal buffer for EVERY
    // transaction — 7 per sample, ~350/s, against the ~4 KB internal DMA
    // heap. When those allocs failed, the read returned 0 (spurious
    // release / (0,239) corner spikes) and twice the driver crashed in
    // uninstall_priv_desc (Load access fault). Static internal buffers keep
    // every transaction on the driver's no-alloc path.
    static uint8_t s_tx[4] __attribute__((aligned(4)));
    static uint8_t s_rx[4] __attribute__((aligned(4)));
    spi_transaction_t t = {};
    // 32 bits (4 bytes), not 24: the driver's no-alloc path requires the byte
    // length to satisfy the cache/DMA alignment too ((buf|len) & (align-1)).
    // One extra clocked-out byte is ignored by the XPT2046.
    t.length = 4 * 8;
    s_tx[0] = cmd;
    t.tx_buffer = s_tx;
    t.rx_buffer = s_rx;
    esp_err_t err = spi_device_transmit(s_dev, &t);
    if (err != ESP_OK) {
        return 0;
    }
    // 12-bit result is left-justified in the two response bytes
    return ((s_rx[1] << 8) | s_rx[2]) >> 3;
}

static uint16_t read_averaged(uint8_t cmd)
{
    uint32_t acc = 0;
    for (int i = 0; i < 3; i++) {
        acc += read_channel(cmd);
    }
    return acc / 3;
}

// Linear map between two measured anchors, extrapolated linearly to the panel
// edge and clamped to [0, out_max]. Clamping at the anchor values instead
// (the old behavior) created a 24 px dead band at every screen edge where the
// reported coordinate froze — a drag into the band stalled, which made
// upward scroll strokes (finger ends near the bottom edge) nearly impossible.
static int16_t map_anchored(int32_t v, int32_t raw_hi, int32_t raw_lo,
                            int16_t out_lo, int16_t out_hi, int16_t out_max)
{
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
    uint16_t z1 = read_channel(CMD_Z1);
    if (z1 < PRESS_THRESHOLD) {
        return false;
    }
    *raw_x = read_averaged(CMD_X);
    *raw_y = read_averaged(CMD_Y);
    return true;
}

// Inner read: one ADC acquisition, mapping + spike rejection. Exposed raw
// values pair with the mapped ones (single acquisition).
static bool touch_read_inner(int16_t* x, int16_t* y, uint16_t* raw_x, uint16_t* raw_y)
{
    static int16_t s_last_x = 0, s_last_y = 0;
    static bool    s_had_contact = false;
    static uint8_t s_reject_n = 0;

    uint16_t rx, ry;
    if (!touch_read_raw(&rx, &ry)) {
        s_had_contact = false;
        return false;
    }

    // Measured mapping (board_pins.h): raw Y -> screen X, raw X -> screen Y,
    // both channels decrease as the screen coordinate increases
    int16_t mx = map_anchored(ry, SSP_TOUCH_RAWY_LEFT, SSP_TOUCH_RAWY_RIGHT,
                              SSP_TOUCH_ANCHOR_MIN, SSP_TOUCH_ANCHOR_MAX_X, SSP_TFT_WIDTH - 1);
    int16_t my = map_anchored(rx, SSP_TOUCH_RAWX_TOP, SSP_TOUCH_RAWX_BOTTOM,
                              SSP_TOUCH_ANCHOR_MIN, SSP_TOUCH_ANCHOR_MAX_Y, SSP_TFT_HEIGHT - 1);

    // Spike rejection. The XPT2046 contact bounces on press (first samples
    // read up to 160 px off) and drops a garbage sample mid-drag under SPI
    // contention; either one yanks the LVGL scroll position. A real drag
    // moves at most ~72 px per 20 ms sample. A rejected sample reports the
    // last good coordinate (finger holds still) — invisible in the UI. If
    // rejects persist 3 samples the new position is real (press baseline was
    // the junk), so accept it and re-base.
    if (s_had_contact) {
        int dx = mx - s_last_x; if (dx < 0) dx = -dx;
        int dy = my - s_last_y; if (dy < 0) dy = -dy;
        if (dx > 72 || dy > 72) {
            if (++s_reject_n < 3) {
                *x = s_last_x;
                *y = s_last_y;
                if (raw_x) *raw_x = rx;
                if (raw_y) *raw_y = ry;
                return true;   // hold last good position, stay pressed
            }
            s_reject_n = 0;    // persistent: baseline was junk, accept new pos
        } else {
            s_reject_n = 0;
        }
    }
    s_had_contact = true;
    s_last_x = mx;
    s_last_y = my;
    *x = mx;
    *y = my;
    if (raw_x) *raw_x = rx;
    if (raw_y) *raw_y = ry;
    return true;
}

bool touch_read(int16_t* x, int16_t* y)
{
    return touch_read_inner(x, y, nullptr, nullptr);
}

// ---- Background sampler task (prio 24, tied with ui_task at the top) ----
// Previously this was an esp_timer callback doing blocking SPI — bad practice.
// A dedicated task lets the SPI bus driver arbitrate fairly with display DMA.

static void sampler_task(void*)
{
    const TickType_t period = pdMS_TO_TICKS(20);  // 50 Hz = exactly 2 ticks @ 100 Hz
    TickType_t last = xTaskGetTickCount();
    while (true) {
        int16_t x = 0, y = 0;
        bool pressed = touch_read(&x, &y);

        portENTER_CRITICAL(&s_sample_mux);
        s_sample_pressed = pressed;
        if (pressed) {
            s_sample_x = x;
            s_sample_y = y;
        }
        portEXIT_CRITICAL(&s_sample_mux);

        // Phase-locked cadence: wakes exactly one period after the previous
        // wake, regardless of how long touch_read() took.  Plain vTaskDelay
        // would add execution time to every period and quantize the delay to
        // (10, 20] ms at the 100 Hz tick.  If a display-DMA flood stalls a
        // sample past the period, DelayUntil returns immediately and the loop
        // re-syncs within a few iterations (each still bounded by SPI time).
        vTaskDelayUntil(&last, period);
    }
}

esp_err_t touch_start_sampler(void)
{
    // Prio 24: ties ui_task at the top of the user range
    // (configMAX_PRIORITIES=25, so 24 is the max valid priority), above the
    // Wi-Fi driver (23), esp_timer/lv_tick (22) and the NimBLE host (21).
    // Blocking SPI transactions sleep on the driver semaphore, so the sampler
    // cannot starve LVGL — the priority only guarantees its next transaction
    // is issued the instant the shared SPI bus frees.  A sample window stalled
    // longer than a tap's press duration erases the tap entirely (no PRESSED,
    // no CLICKED), which is why sampling sits at the top of the ladder.
    BaseType_t ok = xTaskCreateWithCaps(sampler_task, "touch_sampler",
                                2048, nullptr, 24, nullptr,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "sampler task create failed");
    ESP_LOGI(TAG, "touch sampler task running @ 50 Hz");
    return ESP_OK;
}

bool touch_read_latest(int16_t* x, int16_t* y)
{
    portENTER_CRITICAL(&s_sample_mux);
    bool pressed = s_sample_pressed;
    int16_t lx = s_sample_x;
    int16_t ly = s_sample_y;
    portEXIT_CRITICAL(&s_sample_mux);

    if (pressed) {
        *x = lx;
        *y = ly;
    }
    return pressed;
}

