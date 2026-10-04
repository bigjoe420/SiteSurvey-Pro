# Changelog

All notable changes to SiteSurvey Pro are documented here. The format is
loosely based on [Keep a Changelog](https://keepachangelog.com/), and the
project follows [Semantic Versioning](https://semver.org/).

## [1.0.0] — 2026-10-03

First stable release for the Guition NM-CYD-C5 (ESP32-C5).

### Added

- Dual-band Wi-Fi survey engine (2.4 / 5 GHz) with channel graph, per-AP
  detail, and evil-twin / rogue-AP flagging
- BLE observer-mode scanning
- GPS mapping (NMEA on LP-UART) with KML survey export and on-device report
  generation to SD
- BME680 environmental telemetry overlay
- Alert engine: SSID / BSSID / RSSI-threshold targets, circular on-screen
  alert log, RGB LED notification
- SD session logging with CSV capture
- OTA updates from SD card
- GitHub Actions CI: every push builds; every `v*` tag auto-cuts a release
  with flashable binaries
- Browser web installer (ESP Web Tools) served from GitHub Pages

### Fixed / hardened

- NVS alert-config validation: counts clamped, target strings force-terminated
- GPS coordinate parsing rejects NaN / out-of-range double→int32 conversion
- CSV session export neutralizes formula-injection and column-breaking bytes
  in attacker-controlled SSIDs
- Settings-tap stability fixes (DMA headroom guard, deferred SD scan)

[1.0.0]: https://github.com/bigjoe420/SiteSurvey-Pro/releases/tag/v1.0.0
