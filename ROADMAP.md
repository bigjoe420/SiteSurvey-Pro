================================================================================
# ROADMAP.md — SiteSurvey Pro Master Roadmap & Execution Queue
================================================================================

> **Single source of truth for project direction.** Update this file when milestones land.
> **Last updated:** 2026-09-04
> **Workspace:** `D:\SiteSurvey Pro` | **GitHub (manual backup only):** `bigjoe420/SiteSurvey-Pro`
> **Platform:** NM-CYD-C5 (ESP32-C5 RISC-V, ESP-IDF v6.0.1)
> **Manufacturer Repo:** https://github.com/RockBase-iot/NM-CYD-C5

---

## 1. Core Vision

SiteSurvey Pro is a **handheld wireless security and RF site survey utility built for on-site scanning, signal monitoring, and field diagnostics**.

### On-Site RF Scanning
A portable, battery-powered tool that performs real-time scanning of WiFi spectra (2.4/5 GHz) in the field, capturing signal strength, access point identifiers, and channel occupancy without requiring a laptop or phone tether.

### Signal Monitoring & Diagnostics
Live on-screen visualization of RSSI, channel maps, and network lists via a 2.8" ST7789 display with XPT2046 resistive touchscreen. Built for security professionals and RF technicians who need immediate situational awareness in the field.

### Field-First Design
Optimized for standalone operation: local storage of scan logs, battery-conscious power modes, environmental sensing (BME680), and GPS tagging. No cloud dependency. No companion app required.

---

## 2. Environment Baseline — ✅ CLOSED (Phase 1)

Hardware verification and board support package (BSP) establishment completed.

| Subsystem | Component | Status | Key Parameters |
|-----------|-----------|--------|----------------|
| MCU | ESP32-C5-WROOM-1 (RISC-V @ 240 MHz) | ✅ | 16MB Flash, 8MB PSRAM |
| Framework | ESP-IDF v6.0.1 | ✅ | Ubuntu 24.04 LTS build host |
| Display | 2.8" ST7789 | ✅ | 320×240 landscape, SPI 20 MHz |
| Touch | XPT2046 Resistive | ✅ | Shared SPI, CS=GPIO1, 2.5 MHz |
| PSRAM | 8MB | ✅ | `CONFIG_SPIRAM=y` required |
| Wireless | Wi-Fi 6 (802.11ax) dual-band | ✅ | 2.4 GHz + 5 GHz |
| UI Framework | LVGL v9 with C++ / ESP-IDF | ✅ | PSRAM draw buffers, 256 KB secondary pool |
| Sensors | BME680 (I2C, CN1) | ✅ | SCL=GPIO8, SDA=GPIO9 |
| Positioning | GPS Module (UART P5) | ✅ | RX=GPIO4, TX=GPIO5, 9600 baud |
| Storage | Micro SD (SPI shared) | ✅ | CS=GPIO10 |
| LED | WS2812 RGB | ✅ | GPIO27, GRB order (wiring pending) |

---

## 3. Execution Queue

### Phase 0: Product Discovery & Requirements — ✅ CLOSED (2026-08-06)
- [x] **0.1 Requirements Interview**
  - [x] Core Product Vision & Use Cases — validated
  - [x] Feature Scope & Operational Workflows — validated
  - [x] UI/UX Screen Map & Navigation — validated (7 screens, dark theme, landscape)
  - [x] Data Logging, Telemetry Formats, & Storage Rules — validated (CSV + KML, session naming, 3–5 s batch writes)

### Phase 1: Foundation & BSP — ✅ CLOSED (2026-08-09)
- [x] **1.1 Project & BSP Scaffold**
- [x] **1.2 Display & LVGL UI Scaffold** — verified on-device 2026-08-07
- [x] **1.3 Core Scanning Engine** — verified on-device 2026-08-07
- [x] **1.4 Sensor Integration** — verified on-device 2026-08-09
  - [x] BME680 over I2C (0x76, live data confirmed)
  - [x] GPS module over UART (NMEA streaming verified, fix pending sky view)
  - [x] Boot splash screen with readiness gates
  - [x] Clean-boot display sequence
  - [x] Live env readout on home screen

### Phase 2: Feature Expansion — 🟡 ACTIVE
- [x] Tabbed home navigation (WI-FI / ENV tabview, dark + green accent) — verified 2026-08-10
- [x] Live Wi-Fi Scan List (SCR_WIFI) — verified 2026-08-10 (commit `f53cab3`)
- [x] Environmental Dashboard (SCR_ENV) — verified 2026-08-10 (commit `e3b3446`)
- [x] SD card driver bring-up — verified 2026-08-10 (commit `52d78d0`)
- [x] Channel occupancy / spectrum view (SCR_SPECTRUM) — verified 2026-08-24
- [x] Data export (CSV to SD card) — session logger CSV landed 2026-08-24
- [x] Environmental overlay on scan screen — verified 2026-08-24
- [x] BLE Device List (SCR_BLE) — verified 2026-08-26
- [x] **Home screen 2×4 color block grid** — verified 2026-09-04 (commit `87cdd2d`)
- [x] **GPS Status screen (SCR_GPS)** — verified 2026-09-04 (commit `87cdd2d`)
- [x] **Alert Log screen (SCR_ALERTS)** — verified 2026-09-04 (commit `87cdd2d`)
- [x] **Settings screen (SCR_SETTINGS)** — verified 2026-09-04 (commit `87cdd2d`)
- [x] **Alert engine with NVS-backed target SSID list** — verified 2026-09-04 (commit `87cdd2d`)
- [x] **Navigation debounce + PSRAM LVGL pool hardening** — verified 2026-09-04 (commit `87cdd2d`)
- [ ] Dual-band concurrent scan (2.4 + 5 GHz) — deferred
- [x] **On-device RSSI graphing (tap AP row for detail chart)** — verified build 2026-09-04 (commit `021c2d9`)
- [ ] Dual-band concurrent scan (2.4 + 5 GHz) — deferred
- [ ] GPS tagging of scan logs — deferred
- [ ] GPS tagging of scan logs — deferred

### Phase 3: Polish & Hardening — ⏸️ DEFERRED
- [ ] Power management & battery life optimization
- [ ] UI themes (outdoor high-contrast mode)
- [ ] Configurable scan filters (by RSSI threshold, SSID regex, channel)
- [ ] Firmware update mechanism (OTA or USB)
- [ ] On-device scan report generation

---

## 4. Feature Master List

### 4.1 Screen / UI Architecture (LVGL)

**Orientation:** Landscape 320×240  
**Theme:** Dark high-contrast, outdoor-optimized  
**Navigation:** 2×4 color block grid on home; back-button on all sub-screens

| # | Screen | ID | Status | Content |
|---|--------|----|--------|---------|
| 1 | Home / Launcher | `SCR_HOME` | ✅ Done | 2×4 color block grid: Wi-Fi, BLE, Spectrum, Environment, GPS, Alerts, Settings |
| 2 | Live Wi-Fi Scan List | `SCR_WIFI` | ✅ Done | APs with RSSI bars, SSID, security, channel |
| 3 | BLE Device List | `SCR_BLE` | ✅ Done | MAC, RSSI, device name, manufacturer data |
| 4 | Channel Spectrum | `SCR_SPECTRUM` | ✅ Done | Channel density / occupancy |
| 5 | GPS Status | `SCR_GPS` | ✅ Done | Live lat/lon, fix quality, sat count, UTC |
| 6 | Alert Log | `SCR_ALERTS` | ✅ Done | Target SSID matches, RSSI threshold breaches |
| 7 | Settings + Data Manager | `SCR_SETTINGS` | ✅ Done | Scan config, target SSID list, export, SD status |

### 4.2 Wireless Scanning

| # | Feature | Status | Notes |
|---|---------|--------|-------|
| 1.1 | WiFi 2.4 GHz active scan | ✅ Done | Blocking all-channel scan, ch 1–11 |
| 1.2 | WiFi 5 GHz active scan | ✅ Done | `esp_wifi_set_country_code("US", true)`; ch 36+ confirmed |
| 1.3 | Dual-band concurrent scan | ⏸️ Deferred | ESP32-C5 native dual-band support |
| 1.4 | Hidden SSID detection | ✅ Done | Probe response analysis |
| 1.5 | Security mode classification | ✅ Done | WPA2/WPA3/Open tagging |
| 1.6 | BLE device discovery | ✅ Done | Passive scan; 32-entry static pool |
| 1.7 | Target-based alerting | ✅ Done | BSSID/SSID match, NVS persistence, real-time check |

### 4.3 Data Management

| # | Feature | Status | Notes |
|---|---------|--------|-------|
| 2.1 | In-memory scan buffer | ✅ Done | 64-entry static pool, LRU eviction |
| 2.2 | GPS tagging per scan | ⏸️ Deferred | NMEA GGA/RMC; pending sky-view GPS fix validation |
| 2.3 | BME680 env snapshot per scan | ✅ Done | Temp/humidity/pressure/VOC at scan time |
| 2.4 | BME680 continuous telemetry | ✅ Done | 5 s interval; live readout on home + scan screens |
| 2.5 | SD card log export (CSV) | ✅ Done | WiGLE-compatible; session-based naming |
| 2.6 | SD card log export (KML) | ⏸️ Deferred | Google Earth mapping |
| 2.7 | Serial telemetry stream | ✅ Done | Debug fallback via `idf.py monitor` |
| 2.8 | On-device graphing (LVGL charts) | ✅ Done | RSSI history per AP; tap row in SCR_WIFI for detail chart 2026-09-04 |

### 4.4 System & Infrastructure

| # | Tool | Status | Description | Risk Level |
|---|------|--------|-------------|------------|
| 3.1 | Build system | ✅ Done | `idf.py` + `sdkconfig.defaults` | Low |
| 3.2 | Pin mapping | ✅ Done | `board_pins.h` from manufacturer repo | Low |
| 3.3 | LVGL + ST7789 | ✅ Done | SPI display, 320×240, PSRAM buffers | Medium |
| 3.4 | XPT2046 touch | ✅ Done | Resistive, shared SPI @ 2.5 MHz, measured calibration | Medium |
| 3.5 | PSRAM config | ✅ Done | 8MB; 256 KB secondary LVGL pool | Low |
| 3.6 | Power management | ⏸️ Deferred | Deep sleep between scans | Medium |
| 3.7 | OTA updates | ⏸️ Deferred | Secure firmware delivery | High |

---

## 5. Severity / Signal Classification System (Version 1.0)

### Current Definitions

| Tier / State | Color / Value | Criteria | Examples |
|--------------|---------------|----------|----------|
| **Strong** | Green | RSSI ≥ -50 dBm | AP within 1–2 m, line-of-sight |
| **Moderate** | Yellow | RSSI -50 to -70 dBm | AP in same room |
| **Weak** | Orange | RSSI -70 to -85 dBm | AP behind wall/floor |
| **Marginal** | Red | RSSI < -85 dBm | At range limit, likely unreliable |

### Adjustability Plan

1. **Phase 1 (Current):** Hardcoded thresholds in `scan_engine.h`
2. **Phase 2:** Move thresholds to `sdkconfig` or runtime config struct
3. **Phase 3:** Per-channel noise-floor calibration for adaptive thresholds

**Adjustment is non-breaking:** Changing thresholds only affects UI coloring and filtering, never the raw capture data.

---

## 6. Known Watch Items

| Item | Status | Note |
|------|--------|------|
| ST7789 init timing | Verified | 120ms delay + BL after DISPON confirmed working. `LV_COLOR_FORMAT_RGB565_SWAPPED`. |
| XPT2046 SPI frequency | Verified | 2.5 MHz touch / 20 MHz display on shared SPI2 — stable. |
| XPT2046 calibration | Resolved | Measured anchored linear map; 5-point verification pixel-perfect. |
| 5 GHz regulatory domain | Resolved | `esp_wifi_set_country_code("US", true)` before `esp_wifi_start()`. |
| BME680 CN1 bus | Resolved | Device ACKs at 0x76; streams compensated data every 5 s. |
| GPS UART | Verified | 1 Hz NMEA streaming; no fix indoors (expected without antenna). |
| SD card on shared SPI2 | Verified | 32 GB card mounted; write/read self-test OK. |
| PSRAM LVGL pool | Resolved 2026-09-04 | 256 KB secondary pool + TLSF max pool fix; ASSERT_NULL disabled. |
| RGB LED wiring | Pending | GPIO27 confirmed in software; physical wiring not connected. |
| GPS antenna | Pending | Module seated; sky-view fix validation pending. |
| GPIO 8/9 I2C conflict | Known | Also used for CC1101/NRF24 in Bruce firmware. Reserved for I2C only. |
| Deepsleep wake GPIO | Known | GPIO 0 is wake source. Must be HIGH at boot. |

---

## 7. Backlog — New Ideas Captured

| ID | Idea | Source | Priority | Status |
|----|------|--------|----------|--------|
| B-01 | Spectrum analyzer waterfall display (LVGL) | User | Medium | `[investigate]` |
| B-02 | Alert mode: notify on target SSID or RSSI threshold | User | High | `[done]` 2026-09-04 — alert engine + NVS target list |
| B-03 | WiFi 6 (802.11ax) feature detection (HE capabilities) | User | Low | `[backlog]` |
| B-04 | Rogue AP detection / evil twin warning | User | Medium | `[backlog]` |
| B-05 | Offline map overlay with GPS-tagged scan points | User | Low | `[backlog]` |
| B-06 | Expressive UI language: rich color + motion | User | High | `[done]` — rainbow splash, gauge gradients, block grid 2026-09-04 |
| B-07 | Environmental gauges with gradient scales | User | High | `[done]` 2026-08-10 (commit `e3b3446`) |

---

## 8. Development Workflow

### Git Branch Strategy
```
main  ──→ stable releases (tagged: v0.1, v0.2, etc.)
  │
  ├── feature/{{NAME}}    → experimental features (if needed)
  └── hotfix/*            → critical fixes (cherry-pick to main)
```

### Build Numbering
- CI builds auto-increment (GitHub Actions)
- Human-readable tags for milestones: `v0.1-alpha`, `v0.1-stable`
- `PROGRESS.md` documents each stable build

### Adding New Features
1. Add to this roadmap with `[planned]` status
2. Implement with reference docs verified from `D:\SiteSurvey Pro\docs\` and manufacturer repo
3. Test on target hardware before declaring done
4. Update this doc with `[done]` status
5. Tag a new milestone build

---

*This document is maintained locally by the Kimi agent and the project owner. Now tracked in Git since 2026-09-04. KIMI.md and PROGRESS.md remain local-only.*
