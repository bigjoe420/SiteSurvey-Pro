#include "led_rgb.h"
#include "board_pins.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "led_rgb";

static rmt_channel_handle_t s_chan = nullptr;
static rmt_encoder_handle_t s_enc  = nullptr;

#define RES_HZ  (10 * 1000 * 1000)   // 10 MHz = 100 ns per tick

// WS2812B-ish timing (conservative, works with 3.3 V logic)
// rmt_symbol_word_t layout: duration0(15), level0(1), duration1(15), level1(1)
static const rmt_symbol_word_t SYM_ZERO = {
    .duration0 = 4,
    .level0    = 1,   // 400 ns HIGH
    .duration1 = 9,
    .level1    = 0,   // 900 ns LOW
};

static const rmt_symbol_word_t SYM_ONE = {
    .duration0 = 9,
    .level0    = 1,   // 900 ns HIGH
    .duration1 = 4,
    .level1    = 0,   // 400 ns LOW
};

static const rmt_symbol_word_t RESET_SYM = {
    .duration0 = 500, // 50 us LOW (latch)
    .level0    = 0,
    .duration1 = 0,
    .level1    = 0,
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t led_rgb_init(void)
{
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num          = SSP_LED_RGB,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .intr_priority     = 0,
        .flags             = {},
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&chan_cfg, &s_chan), TAG, "rmt channel");

    rmt_copy_encoder_config_t enc_cfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&enc_cfg, &s_enc), TAG, "copy encoder");

    ESP_RETURN_ON_ERROR(rmt_enable(s_chan), TAG, "rmt enable");

    led_rgb_off();  // Start dark
    ESP_LOGI(TAG, "RGB LED ready on GPIO %d", SSP_LED_RGB);
    return ESP_OK;
}

void led_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_chan) return;

    // GRB order per board_pins.h
    uint8_t grb[3] = { g, r, b };

    // 24 data symbols + 1 reset latch
    rmt_symbol_word_t symbols[25];
    int idx = 0;

    for (int byte = 0; byte < 3; byte++) {
        for (int bit = 7; bit >= 0; bit--) {
            symbols[idx++] = (grb[byte] >> bit) & 1 ? SYM_ONE : SYM_ZERO;
        }
    }
    symbols[idx++] = RESET_SYM;

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags      = {},
    };
    rmt_transmit(s_chan, s_enc, symbols, idx * sizeof(symbols[0]), &tx_cfg);
    rmt_tx_wait_all_done(s_chan, pdMS_TO_TICKS(100));
}

void led_rgb_off(void)
{
    led_rgb_set(0, 0, 0);
}

void led_rgb_alert(void)
{
    // Quick red flash — 100 ms on, 100 ms off.
    // Called from late_init_task (prio 1), so a brief spin is harmless.
    led_rgb_set(0xFF, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    led_rgb_off();
}
