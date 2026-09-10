#include "power_mgr.h"

#include "board_pins.h"
#include "flash_broker.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "power_mgr";

// LEDC configuration
#define PM_LEDC_TIMER       LEDC_TIMER_0
#define PM_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define PM_LEDC_CHANNEL     LEDC_CHANNEL_0
#define PM_LEDC_FREQ_HZ     5000
#define PM_LEDC_RESOLUTION  LEDC_TIMER_8_BIT
#define PM_LEDC_MAX_DUTY    255

// Defaults
#define DEFAULT_TIMEOUT_S   30
#define DEFAULT_DIM_PCT     25
#define DEFAULT_SLEEP_EN    false

// State
static volatile ssp_pm_bl_state_t s_bl_state = SSP_PM_BL_FULL;
static uint8_t  s_dim_pct     = DEFAULT_DIM_PCT;
static uint16_t s_timeout_s   = DEFAULT_TIMEOUT_S;
static bool     s_sleep_en    = DEFAULT_SLEEP_EN;
static uint32_t s_last_activity_ms = 0;
static bool     s_initialized = false;

static nvs_handle_t s_nvs;
static TimerHandle_t s_idle_timer;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------
static void nvs_load(void)
{
    s_timeout_s = DEFAULT_TIMEOUT_S;
    s_dim_pct   = DEFAULT_DIM_PCT;
    s_sleep_en  = DEFAULT_SLEEP_EN;

    if (s_nvs == 0) return;

    int32_t t = DEFAULT_TIMEOUT_S;
    int32_t d = DEFAULT_DIM_PCT;
    uint8_t e = DEFAULT_SLEEP_EN ? 1 : 0;

    nvs_get_i32(s_nvs, SSP_PM_NVS_KEY_TIMEOUT, &t);
    nvs_get_i32(s_nvs, SSP_PM_NVS_KEY_DIM, &d);
    nvs_get_u8 (s_nvs, SSP_PM_NVS_KEY_SLEEP, &e);

    s_timeout_s = (uint16_t)t;
    s_dim_pct   = (uint8_t) d;
    s_sleep_en  = (e != 0);
}

static void nvs_save_now(void)
{
    if (s_nvs == 0) return;
    nvs_set_i32(s_nvs, SSP_PM_NVS_KEY_TIMEOUT, (int32_t)s_timeout_s);
    nvs_set_i32(s_nvs, SSP_PM_NVS_KEY_DIM,     (int32_t)s_dim_pct);
    nvs_set_u8 (s_nvs, SSP_PM_NVS_KEY_SLEEP,   s_sleep_en ? 1 : 0);
    nvs_commit(s_nvs);
}

static void nvs_save_tramp(void*)
{
    nvs_save_now();
}

// NVS commit erases/writes flash. The setters run on the UI task, whose
// stack lives in PSRAM — unreachable while the cache is down during a flash
// write. Run the commit on the broker task (internal-RAM stack) instead.
static void nvs_save(void)
{
    flash_broker_exec(nvs_save_tramp, nullptr);
}

// ---------------------------------------------------------------------------
// Backlight driver
// ---------------------------------------------------------------------------
static void bl_apply(uint8_t pct)
{
    if (!s_initialized) return;
    if (pct > 100) pct = 100;
    uint32_t duty = (pct * PM_LEDC_MAX_DUTY) / 100;
    ledc_set_duty(PM_LEDC_MODE, PM_LEDC_CHANNEL, duty);
    ledc_update_duty(PM_LEDC_MODE, PM_LEDC_CHANNEL);
    ESP_LOGD(TAG, "backlight %u%% (duty=%lu)", pct, duty);
}

// ---------------------------------------------------------------------------
// Idle timer callback — runs every 1 s
// ---------------------------------------------------------------------------
static void idle_timer_cb(TimerHandle_t)
{
    if (s_timeout_s == 0) return;   // auto-dim disabled

    uint32_t idle_ms = pdTICKS_TO_MS(xTaskGetTickCount()) - s_last_activity_ms;
    uint32_t timeout_ms = s_timeout_s * 1000U;

    if (s_bl_state == SSP_PM_BL_FULL && idle_ms >= timeout_ms) {
        s_bl_state = SSP_PM_BL_DIM;
        bl_apply(s_dim_pct);
        ESP_LOGI(TAG, "idle → dim (%u%%)", s_dim_pct);
    } else if (s_bl_state == SSP_PM_BL_DIM && idle_ms >= timeout_ms * 2) {
        s_bl_state = SSP_PM_BL_OFF;
        bl_apply(0);
        ESP_LOGI(TAG, "idle → off");
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t power_mgr_init(void)
{
    if (s_initialized) return ESP_OK;

    // LEDC timer
    ledc_timer_config_t timer = {};
    timer.speed_mode      = PM_LEDC_MODE;
    timer.duty_resolution = PM_LEDC_RESOLUTION;
    timer.timer_num       = PM_LEDC_TIMER;
    timer.freq_hz         = PM_LEDC_FREQ_HZ;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer config failed");

    // LEDC channel on backlight GPIO
    ledc_channel_config_t ch = {};
    ch.gpio_num       = SSP_TFT_BL;
    ch.speed_mode     = PM_LEDC_MODE;
    ch.channel        = PM_LEDC_CHANNEL;
    ch.intr_type      = LEDC_INTR_DISABLE;
    ch.timer_sel      = PM_LEDC_TIMER;
    ch.duty           = 0;
    ch.hpoint         = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "ledc channel config failed");

    // NVS
    esp_err_t ret = nvs_open(SSP_PM_NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed (%s), using defaults", esp_err_to_name(ret));
        s_nvs = 0;
    }
    nvs_load();

    s_initialized = true;

    // Start at full brightness (bl_apply needs s_initialized set first —
    // otherwise the boot backlight never turns on and the splash runs dark)
    s_last_activity_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    s_bl_state = SSP_PM_BL_FULL;
    bl_apply(100);

    // 1-second periodic idle timer
    s_idle_timer = xTimerCreate("pm_idle", pdMS_TO_TICKS(1000), pdTRUE,
                                nullptr, idle_timer_cb);
    if (s_idle_timer) xTimerStart(s_idle_timer, 0);
    ESP_LOGI(TAG, "init ok: timeout=%us dim=%u%% sleep=%s",
             s_timeout_s, s_dim_pct, s_sleep_en ? "on" : "off");
    return ESP_OK;
}

void power_mgr_set_brightness(uint8_t pct)
{
    bl_apply(pct);
    if (pct == 0)       s_bl_state = SSP_PM_BL_OFF;
    else if (pct < 55)  s_bl_state = SSP_PM_BL_DIM;
    else                s_bl_state = SSP_PM_BL_FULL;
}

void power_mgr_activity(void)
{
    if (!s_initialized) return;
    s_last_activity_ms = pdTICKS_TO_MS(xTaskGetTickCount());

    if (s_bl_state != SSP_PM_BL_FULL) {
        s_bl_state = SSP_PM_BL_FULL;
        bl_apply(100);
        ESP_LOGD(TAG, "activity → full");
    }
}

ssp_pm_bl_state_t power_mgr_bl_state(void)
{
    return s_bl_state;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
uint16_t power_mgr_get_timeout_s(void) { return s_timeout_s; }
void     power_mgr_set_timeout_s(uint16_t seconds)
{
    s_timeout_s = seconds;
    nvs_save();
    // Reset idle tracking so new timeout takes effect immediately
    s_last_activity_ms = pdTICKS_TO_MS(xTaskGetTickCount());
}

uint8_t power_mgr_get_dim_pct(void) { return s_dim_pct; }
void    power_mgr_set_dim_pct(uint8_t pct)
{
    if (pct < 5)  pct = 5;
    if (pct > 50) pct = 50;
    s_dim_pct = pct;
    nvs_save();
}

bool power_mgr_get_sleep_en(void) { return s_sleep_en; }
void power_mgr_set_sleep_en(bool en)
{
    s_sleep_en = en;
    nvs_save();
}

void power_mgr_power_off(void)
{
    ESP_LOGI(TAG, "power off: backlight off, entering deep sleep (press BOOT to wake)");
    ledc_set_duty(PM_LEDC_MODE, PM_LEDC_CHANNEL, 0);
    ledc_update_duty(PM_LEDC_MODE, PM_LEDC_CHANNEL);
    s_bl_state = SSP_PM_BL_OFF;
    // let the log line flush before the UART goes quiet
    vTaskDelay(pdMS_TO_TICKS(150));
    // GPIO0 is RTC-capable on ESP32-C5 (RTCIO channel 0) -> EXT1 deep-sleep
    // wakeup; BOOT button pulls it low. The internal pull-up must be enabled
    // explicitly and the RTC_PERIPH power domain kept on, otherwise the pin
    // floats low in deep sleep and ANY_LOW fires instantly -> the device
    // "resets" the moment it should be sleeping.
    rtc_gpio_pullup_en(SSP_WAKE_PIN);
    rtc_gpio_pulldown_dis(SSP_WAKE_PIN);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_err_t err = esp_sleep_enable_ext1_wakeup_io(1ULL << SSP_WAKE_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ext1 wakeup config failed (%s) - aborting power off", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(50));  // let pull-up settle before sampling
    esp_deep_sleep_start();  // never returns
}
