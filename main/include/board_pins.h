#pragma once

// =============================================================================
// SiteSurvey Pro — Board Pin Definitions
// NM-CYD-C5 (ESP32-C5 RISC-V)
// Source: https://github.com/RockBase-iot/NM-CYD-C5
// =============================================================================

#include <stdint.h>

// =============================================================================
// Shared SPI Bus (FSPI/SPI2)
// Used by: Display, Touch, SD Card
// =============================================================================
#define SSP_SPI_SCK     GPIO_NUM_6
#define SSP_SPI_MISO    GPIO_NUM_2
#define SSP_SPI_MOSI    GPIO_NUM_7

// =============================================================================
// ST7789 Display (2.8" TFT, 320x240, Landscape Rotation=3)
// =============================================================================
#define SSP_TFT_CS      GPIO_NUM_23
#define SSP_TFT_DC      GPIO_NUM_24
#define SSP_TFT_RST     GPIO_NUM_NC     // No dedicated GPIO; uses chip reset
#define SSP_TFT_BL      GPIO_NUM_25     // Backlight, active HIGH, PWM-capable

#define SSP_TFT_WIDTH   320
#define SSP_TFT_HEIGHT  240
#define SSP_TFT_SPI_FREQ_HZ     20000000UL  // 20 MHz max

// =============================================================================
// XPT2046 Resistive Touch Controller
// Shares SPI bus with display; separate CS line
// =============================================================================
#define SSP_TOUCH_CS    GPIO_NUM_1
#define SSP_TOUCH_SPI_FREQ_HZ   2500000UL   // 2.5 MHz max for XPT2046
#define SSP_TOUCH_IRQ   GPIO_NUM_NC         // Not connected on NM-CYD-C5

// Touch calibration data (from Bruce firmware)
#define SSP_TOUCH_CAL_0 225
#define SSP_TOUCH_CAL_1 3413
#define SSP_TOUCH_CAL_2 403
#define SSP_TOUCH_CAL_3 3334
#define SSP_TOUCH_CAL_4 1

// =============================================================================
// Micro SD Card (SPI shared bus)
// =============================================================================
#define SSP_SDCARD_CS   GPIO_NUM_10

// =============================================================================
// GPS Module (LP-UART, P5 connector)
// =============================================================================
#define SSP_GPS_UART    UART_NUM_1
#define SSP_GPS_TX      GPIO_NUM_5      // ESP TX -> GPS RX
#define SSP_GPS_RX      GPIO_NUM_4      // GPS TX -> ESP RX
#define SSP_GPS_BAUD    9600

// =============================================================================
// I2C Bus (CN1 connector / Grove)
// BME680 Environmental Sensor
// =============================================================================
#define SSP_I2C_PORT    I2C_NUM_0
#define SSP_I2C_SCL     GPIO_NUM_8
#define SSP_I2C_SDA     GPIO_NUM_9
#define SSP_I2C_FREQ_HZ 100000          // Standard mode 100kHz

#define SSP_BME680_ADDR_0   0x76        // SDO = GND
#define SSP_BME680_ADDR_1   0x77        // SDO = VCC

// =============================================================================
// WS2812 RGB LED
// =============================================================================
#define SSP_LED_RGB     GPIO_NUM_27     // GRB order, single LED

// =============================================================================
// Deepsleep / Boot
// =============================================================================
#define SSP_WAKE_PIN    GPIO_NUM_0      // Deepsleep wakeup source

// =============================================================================
// Display Init Timing Constants (milliseconds)
// =============================================================================
#define SSP_TFT_RST_HOLD_MS     10
#define SSP_TFT_RST_WAIT_MS     120
#define SSP_TFT_SLPOUT_WAIT_MS  120
