# SiteSurvey Pro

> Handheld wireless security and RF site survey utility for the NM-CYD-C5 (ESP32-C5).

## Overview

SiteSurvey Pro is a standalone, battery-powered Wi-Fi 6 scanner built for field diagnostics. It performs real-time 2.4/5 GHz spectrum analysis, captures access point telemetry (SSID, BSSID, RSSI, channel, security mode), and overlays environmental data (BME680) and GPS coordinates onto a 2.8" LVGL-driven touchscreen. No laptop, no cloud, no companion app.

| Spec | Value |
|------|-------|
| MCU | ESP32-C5-WROOM-1 (RISC-V 32-bit @ 240 MHz) |
| Flash | 16 MB (QIO @ 80 MHz) |
| PSRAM | 8 MB (Octal, used for LVGL draw buffers) |
| Wireless | Wi-Fi 6 (802.11ax) dual-band 2.4/5 GHz |
| Display | 2.8" ST7789 TFT, 320×240 landscape |
| Touch | XPT2046 resistive (shared SPI) |
| Environmental | Bosch BME680 (I²C) |
| Positioning | GPS module (UART 9600 baud, NMEA) |
| Storage | Micro SD (SPI shared bus) |

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              SiteSurvey Pro                                  │
│  FreeRTOS Task Architecture                                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌─────────────┐      scan_queue      ┌─────────────┐     gui_queue      ┌───────────┐
│   │ wifi_scan   │ ───────────────────► │             │ ─────────────────► │           │
│   │   task      │   (fixed 16 slots)   │             │   (fixed 4 slots)  │  ui_task  │
│   └─────────────┘                      │             │                    │  (LVGL)   │
│                                        │   main_task │                    └─────┬─────┘
│   ┌─────────────┐      env_queue       │  (event     │                          │
│   │  sensor     │ ───────────────────► │   loop)     │                          │
│   │   task      │   (fixed 8 slots)    │             │                          │
│   │ (BME680 +   │                      │             │                    ┌─────┴─────┐
│   │   GPS)      │                      └─────────────┘                    │  ST7789   │
│   └─────────────┘                                                         │   + XPT   │
│                                                                           │  (SPI)    │
└───────────────────────────────────────────────────────────────────────────┴───────────┘
```

### Task Responsibilities

| Task | Priority | Stack | Duty |
|------|----------|-------|------|
| `ui_task` | 3 | 4 KB | LVGL `lv_timer_handler()` tick + render. Consumes `gui_queue`. |
| `wifi_scan_task` | 2 | 3 KB | Active scan loops (2.4/5 GHz). Posts `ScanResult_t` to `scan_queue`. |
| `sensor_task` | 2 | 3 KB | BME680 poll (≥ 1 Hz), GPS NMEA parse. Posts `EnvSnapshot_t` to `env_queue`. |
| `main_task` | 2 | 4 KB | Event loop: consumes queues, updates model, pushes `GuiUpdate_t` to `gui_queue`. |

### Inter-Task Communication

```
        wifi_scan_task                main_task                   ui_task
              │                            │                          │
              │  ScanResult_t {            │                          │
              │    ssid[32],               │                          │
              │    bssid[6],               │                          │
              │    rssi,                   │                          │
              │    channel,                │                          │
              │    auth_mode               │                          │
              │  }                         │                          │
              │ ───────────────────────►   │                          │
              │     xQueueSend             │                          │
              │                            │  merges scan + env data  │
              │                            │  into ModelState_t       │
              │                            │                          │
              │         EnvSnapshot_t {    │                          │
              │           temp, hum,       │                          │
              │           pressure, voc,   │                          │
              │           lat, lon,        │                          │
              │           fix_valid        │                          │
              │         }                  │                          │
              │ ───────────────────────►   │                          │
              │     xQueueSend             │                          │
              │                            │  GuiUpdate_t {           │
              │                            │    ap_list[N],           │
              │                            │    env_snapshot,         │
              │                            │    scan_status           │
              │                            │  }                       │
              │                            │ ─────────────────────►   │
              │                            │     xQueueSend           │
              │                            │                          │
```

## Hardware Pinout

### NM-CYD-C5 (ESP32-C5)

| Function | Signal | GPIO | Notes |
|----------|--------|------|-------|
| **Shared SPI** | SCK | 6 | 20 MHz (display), 2.5 MHz (touch) |
| | MISO | 2 | — |
| | MOSI | 7 | — |
| **ST7789** | CS | 23 | Active LOW |
| | DC | 24 | Data/Command |
| | BL | 25 | Active HIGH, PWM backlight |
| | RST | — | Chip-level reset (no GPIO) |
| **XPT2046** | CS | 1 | Active LOW |
| | IRQ | — | Not connected |
| **SD Card** | CS | 10 | Active LOW |
| **GPS (P5)** | TX (ESP→GPS) | 5 | LP-UART |
| | RX (GPS→ESP) | 4 | LP-UART |
| | Baud | — | 9600, NMEA GGA/RMC |
| **BME680** | SCL | 8 | I²C @ 100 kHz |
| | SDA | 9 | Addresses: 0x76, 0x77 |
| **RGB LED** | Data | 27 | WS2812, GRB order |
| **Wake** | — | 0 | Deepsleep source |

## Build & Flash

Requires [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/) and `riscv32-esp-elf-gcc`.

```bash
cd D:\SiteSurvey Pro

# One-time target setup
idf.py set-target esp32c5

# Build
idf.py build

# Flash + monitor
idf.py flash monitor

# Clean rebuild (after sdkconfig changes)
idf.py fullclean
idf.py set-target esp32c5
idf.py build
```

## Project Structure

```
SiteSurvey Pro/
├── CMakeLists.txt              # Root project
├── sdkconfig.defaults          # Mandatory Kconfig (target, flash, PSRAM)
├── partitions/
│   └── partitions.csv          # 16MB custom layout
├── main/
│   ├── CMakeLists.txt          # Component registration
│   ├── main.cpp                # FreeRTOS init + task creation
│   ├── include/
│   │   └── board_pins.h        # Canonical pin macros
│   ├── ui/                     # LVGL screens & styles (declarative)
│   ├── model/                  # Telemetry state structs
│   ├── scan_engine/            # Wi-Fi active scan logic
│   └── sensors/                # BME680 + GPS drivers
├── docs/                       # Hardware docs (project-isolated)
├── tools/                      # Diagnostic scripts
```

## License

MIT — See repository for full text.
