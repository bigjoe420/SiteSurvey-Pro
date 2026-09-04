#include "led_rgb.h"
#include "board_pins.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "led_rgb";

static rmt_channel_handle_t s_chan = nullptr;
static rmt_encoder_handle_t s_enc  = nullptr;

#define RES_HZ  (10 * 1000 * 1000)   // 10 MHz = 100 ns per tick

// WS2812B-ish timing (conservative, works with 3.3 V logic)
static const rmt_symbol_word_t BIT0 = {
    .level0 = 1, .duration0 = 4,   // 400 ns HIGH
    .level1 = 0, .duration1 = 9,   // 900 ns LOW
};

static const rmt_symbol_word_t BIT1 = {
    .level0 = 1, .duration0 = 9,   // 900 ns HIGH
    .level1 = 0, .duration1 = 4,   // 400 ns LOW
};

static const rmt_symbol_word_t RESET_SYM = {
    .level0 = 0, .duration0 = 500, // 50 us LOW (latch)
    .level1 = 0, .duration1 = 0,
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t led_rgb_init(void)
{
    rmt_tx_channel_config_t chan_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .gpio_num          = SSP_LED_RGB,
        .mem_block_symbols = 64,
        .resolution_hz     = RES_HZ,
        .trans_queue_depth = 4,
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
            symbols[idx++] = (grb[byte] >> bit) & 1 ? BIT1 : BIT0;
        }
    }
    symbols[idx++] = RESET_SYM;

    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
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
