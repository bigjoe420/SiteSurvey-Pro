================================================================================
# FILE 3 OF 3 — PROGRESS.md
# PROGRESS.md — SiteSurvey Pro Historical Log, Audits & Bench Metrics
================================================================================

> **Append-only historical record.** Do not delete entries; add new ones at the top of each section.
> **Last updated:** 2026-09-12 06:20

---

## 1. Milestone History (Chronological — Newest First)

### Update: 2026-09-12 — Session picker for export: KML/Report buttons convert the CHOSEN session, not just the active one

**Why:** the walk-test export (see previous entry) silently converted the *active*
session — a 9 h home session — because the owner had left it open across the walk.
The active-session fallback made it impossible to export any other session from
the device.

1. **Exporters refactored** (`kml_export_path` / `report_export_path`): both take an
   explicit `/sdcard/survey_*.csv` path; the `_latest` wrappers keep the old
   active-session resolution so existing call sites are unchanged.
2. **`csv_list_sessions()`** (session_csv): scans the SD card, skips empty files,
   sorts newest-first (mtime desc, name-desc tiebreak — same heuristic and same
   no-RTC caveat as `csv_find_latest`), flags the active session from the
   `current_session` pointer. Capped at 48 entries.
3. **UI** (Settings → Data export): new "Select session" button opens a modal
   (OTA-file-list pattern: 28 px rows, 4 visible, scroll within modal, Cancel).
   Rows show short name + size in KB, `*` marks the active session, current
   selection highlighted. Status label shows "will export: <name>"; the export
   buttons then convert the picked session and report "KML: N APs (<name>)".
   Default on screen entry = active session = old behavior. Picker list rebuilt
   on every open so card changes are picked up.
4. House-rule notes: no NVS anywhere in the new paths (SD only, LVGL-task safe);
   `%.*s` bounded copies (format-truncation is -Werror); integer KB sizes, no %f.
   Boot VERIFIED clean: POWERON reset, `SD up: SL32G` + self-test OK, DMA free
   77.6 KB, zero faults. Interactive picker test pending owner.

### Update: 2026-09-12 — Owner GPS walk test VERIFIED: DGPS fixes, real trajectory, valid KML

Owner took the unit out traveling (no battery, powered over USB). Post-walk analysis of
the SD card:

1. **Correction to earlier notes:** NMEA GGA fix quality has no q=3 — the scale is
   0=no fix / 1=GPS / 2=DGPS. **q=2 (differential) is the best achievable.** Sessions
   earlier logged as "target q=3" were chasing a value that does not exist.
2. **Walk session `survey_19700101_000132.csv`:** 44 min span, 715 rows, 640 (90%) with
   fix, 634 of those q=2 DGPS, up to 11 sats. Trajectory traced in time order: starts at
   home coordinates, loops NE to 39.85268/-75.17169 and returns — matches a real walk.
   One single-sample lon outlier at 04:02:41 (4 sats), self-corrected next sample.
3. **On-device export caveat found:** the KML button converts the *active* session
   (`current_session` pointer). The active session was a 9 h home session (`_66`), so the
   owner's first export was all home pins. Walk KML was produced on-PC with the same
   dedup rules (kml_export.cpp logic): 21 placemarks, XML validated, coordinate spread
   matches the walked loop. `survey_19700101_000613.csv` is a second, shorter walk
   (45 min, q=1 fixes, 8 sats max).
4. **Device-exported KML validated:** `_66.kml` well-formed, 12 placemarks, dedup by
   BSSID strongest-RSSI, RSSI-tiered styles match kml_export.cpp thresholds.

### Update: 2026-09-12 — Outdoor mode REMOVED per owner decision; white-on-white sweep; final dark build verified

**Owner verdict on the light theme:** "a lot of writing and things disappeared... its not really
a contrast and doesnt seem like it does much" -> "just delete the option period."

1. White-on-white root cause (`c5bb635`): 53 label colors were hardcoded `lv_color_white()`,
   invisible on the light palette. All theme-driven to `ui_theme()->text`.
2. Removal (`3da7e7c`): light palette, Settings toggle, and power_mgr outdoor flag fully
   excised. Single dark palette is permanent; the orphaned NVS key is never read.
3. Boot VERIFIED clean on-device (compile stamp Sep 12 03:06:34, `UI theme: dark (single
   palette)`), zero faults. Scroll fixes from `e972e4c` ride unchanged.

### Update: 2026-09-12 — Scroll fix proven by data: edge-extrapolated touch map + XPT2046 spike rejection; outdoor light theme; GPS replacement module up

**Owner report:** settings scroll "fast and jumpy, then impossible going back up" (after the
2026-09-11 tuning), outdoor mode "no visible difference", and the replacement GPS module
installed and locking.

1. **Root cause, proven by instrumented capture, not guessed** (temp SD touch logger at
   50 Hz, dumped over serial on boot — diag fully reverted before commit):
   a. `map_anchored()` clamped at the calibration anchors instead of extrapolating,
      creating a 24 px dead band at every screen edge (comment in board_pins.h said
      "extrapolates" — the code lied). Upward scroll strokes end near the bottom edge:
      the reported Y froze, LVGL saw a motionless finger, the scroll died.
   b. XPT2046 contact bounce: the first 1-2 samples after press read 86-160 px off
      (junk baseline), and SPI contention dropped mid-drag garbage samples
      (single-sample leaps to y=239). Either one yanks the LVGL scroll position.
   **Fixes** (`touch.cpp`, commit `e972e4c`): map extrapolates linearly to the panel edge;
   samples deviating >72 px from the last accepted position are rejected (report last
   good coordinate, stay pressed), with a 3-sample persistence accept so press-bounce
   re-bases within 60 ms. Post-fix capture: all mid-drag spikes eliminated.
2. **Outdoor mode** — owner saw no visual change: correct, because the old outdoor
   palette was too subtle. Outdoor is now a true daylight **light theme** (black on
   white, darkened accents/tiers). The toggle restarts the unit (already did); settings
   modals had hardcoded black backgrounds — now palette-driven. Visual check pending
   owner.
3. **Power Off button now requires a 1 s hold** (LV_EVENT_LONG_PRESSED) — owner
   accidentally deep-slept the unit when a scroll stroke landed on it at the bottom of
   the settings list.
4. **GPS replacement module VERIFIED:** live fixes q=2 (3-6 sats) indoors at
   39.8517 N / 75.1730 W, NMEA parse ok=21k+ bad=0 drops=0. Outdoor walk test (q=3,
   CSV/KML coordinate check) still pending owner.

**Bench:** bin 0x1d4280, internal DMA 4008 B after all tasks (unchanged), clean boot,
zero faults. `fflush`-without-`fsync` SD gotcha re-learned by the diag logger: FAT dir
entry stays stale, file reads back 0-length (session_logger's sync_dir_entry exists for
exactly this).

---
### Update: 2026-09-11 (later) — Settings scroll-feel tuning: uniform 50 Hz indev reads + calmer momentum throw

**Owner report:** settings scroll "fast and jumpy at times" (only listed known annoyance
after the momentum restoration in `6856aeb`). Diagnosis from the 2026-08-26 sampler
architecture, two surgical changes in `lvgl_port.cpp` (commit `86f6a11`):

1. **Indev read period 10 ms → 20 ms.** The sampler produces a fresh coordinate at 50 Hz;
   at 100 Hz reads every other LVGL read repeated the previous coordinate, so drag deltas
   arrived as move/0/move/0 bursts — velocity estimates and the drag position quantize
   unevenly = "jumpy". Now every read gets a fresh, uniformly spaced sample. Worst-case
   tap latency grows 10 ms, invisible.
2. **`lv_indev_set_scroll_throw(indev, 25)`** (LVGL default is 10% — greater = faster
   slow-down). A flick no longer shoots the list; it settles close to where the finger
   meant = fixes "fast".

**Verified on-device (COM3):** clean POWERON boot, zero faults in 40 s capture, app stamp
`Sep 11 2026 04:31:04`. Note: ninja does not recompile `esp_app_desc` on incremental
builds — had to delete `build/esp-idf/esp_app_format/.../esp_app_desc.c.obj` to get an
honest timestamp (same quirk class as the stale-stamp audit finding earlier today).
Bench: bin 0x1d41c0 (+16 B), internal DMA **4008 B** after all tasks (was 3984 B — still
above the ~4 KB floor, thin as documented), total ~8.04 MB.

Scroll *feel* itself needs an owner finger-test; structural behavior verified by monitor.

---

### Update: 2026-09-11 — House-rules audit: stale compile time caught + fixed, ROADMAP closed out

Post-milestone audit after the outdoor theme commit `e265bad` (handoff docs re-read first this
time — README.md, ROADMAP.md, KIMI.md).

1. **Stale-build violation (Commandment VI / §12 checklist):** boot log after the theme flash
   showed `compile time Sep 10 2026 05:38:27` — the flashed bin was built today (03:12) but
   `esp_app_desc` kept the Sep 10 build stamp; incremental ninja does not recompile it.
   **Fix per §7 cheatsheet:** `fullclean` → delete `sdkconfig` → `set-target esp32c5` →
   rebuild → reflash COM3. Post-fix boot: bootloader `Sep 11 2026 03:32:48`, app
   `Sep 11 2026 03:32:15`, `UI theme: indoor`, clean POWERON, no panics. Bin size unchanged
   0x1d41b0 (same code, fresh stamp).
2. **ROADMAP.md brought current:** Phase 3 outdoor theme checked off (`e265bad`); power
   management marked partial-done (`01209ac`); stale Deferred rows fixed — KML export 2.6
   (`e6dfb63`), on-device graphing 2.8, power mgmt 3.6, OTA 3.7; B-04 rogue AP marked done;
   GPS module row updated. Header last-updated → 2026-09-11.
3. **GPS bring-up deferred:** replacement unit not on hand, dead module shows no green LED —
   GPS testing waits until next session (tomorrow).

---

### Update: 2026-09-09 — Boot freeze on v6.1 fixed (SPI PSRAM DMA) + BLE observer-only trim

**Symptom (new computer, IDF v6.1):** device froze on the splash screen forever. Serial log
showed two interleaved failures:

1. **Display freeze:** every LVGL flush logged
   `spicommon_dma_setup_priv_buffer: Failed to allocate priv TX buffer` +
   `panel_io_spi_tx_color: spi transmit (queue) color failed` (~120/s). On v6.1 the SPI
   driver allocates a per-transaction DMA bounce buffer from INTERNAL RAM when the source
   lives in PSRAM. LVGL draw buffers are PSRAM-resident, so after WiFi+BLE claimed internal
   RAM the ~30 KB allocation failed permanently and the panel never received another frame.
   Home actually finished loading (~15.6 s) but could never be displayed.
   **Fix:** `io_cfg.flags.psram_dma_direct = 1` in `display_init()`
   (`main/display/display.cpp`). ESP32-C5 is PSRAM-DMA-capable (`SOC_PSRAM_DMA_CAPABLE`),
   so SPI reads the PSRAM buffer directly — no bounce buffer, no allocation failure.
   Post-fix 40 s capture: zero SPI/LCD errors.

2. **BLE scan dead:** `ble_scan: start_scanning failed: rc=519`
   (controller `hci_err=0x207 BLE_ERR_MEM_CAPACITY`) on every retry. WiFi+BLE coexistence
   on C5 ran the controller out of memory while NimBLE still carried the full
   central/peripheral/GATT/security stack — but `main/ble/scan_ble.cpp` only ever uses
   passive observer APIs (`ble_gap_disc`, `ble_hs_adv_parse_fields`).
   **Fix:** observer-only Nimble in `sdkconfig.defaults`:
   `ROLE_CENTRAL/PERIPHERAL/BROADCASTER=n`, `GATT_CLIENT/SERVER=n`, `SECURITY_ENABLE=n`.
   Gotcha: **sdkconfig.defaults does NOT override an existing `sdkconfig`** — the first
   build silently kept the old config (identical binary size). Fix required deleting
   `sdkconfig` so kconfig re-generated from defaults. App shrank 0x1d79e0 → 0x1c1250.
   Post-fix: `BLE observer up` at 1.56 s, `scan started` every cycle, zero rc=519.

**Verified (boot_log2_clean.txt, 40 s):** backlight 806 ms, zero SPI/LCD errors, zero
BLE errors, WiFi cycle total=43 (2.4G=21 5G=22), `gates satisfied` 24.9 s, heap steady
~8.04 MB. Pre-existing notes: SD card absent (`0x101`, no card inserted — user to confirm);
scan queue `drops=27/43` per cycle caused by the 100 ms alert-LED spin in
`late_init_task` blocking the consumer behind AP bursts (non-fatal, parked).

### Update: 2026-09-09 — Boot backlight fix (splash was running dark) + new-computer toolchain

**Symptom:** screen stayed black at boot; splash graphics never visible; home screen only
"faintly appeared" later; RGB LED flashed green→red. Root cause was in `power_mgr_init()`:

- `board_init_gpio()` holds backlight GPIO LOW at boot (correct).
- `power_mgr_init()` configured LEDC on the pin (duty 0 = off), then called `bl_apply(100)`
  to light it — but `bl_apply()` early-returns while `s_initialized` is false, and
  `s_initialized` was only set at the very END of init. The boot brightness apply was a
  silent no-op.
- `backlight_on_once()` → `power_mgr_activity()` was also a no-op (state already FULL).
- Net effect: backlight never turned on at boot. The "home screen appears after a bit of
  time" was the 30–60 s idle timer firing the first real `bl_apply()` (dim %), and a tap
  then woke it to full. The splash's entire ~3–5 s life ran in the dark.

**Fix:** set `s_initialized = true` BEFORE the "Start at full brightness" block in
`power_mgr_init()`. Verified via serial log: `backlight on: first full frame drawn` at
~830 ms, splash now visible from power-on.

**Also this session (new computer, HPI7):**
- ESP-IDF v6.0.1 → v6.1 at `C:\esp\v6.1\esp-idf`; tools installed to
  `C:\Users\joeky\.espressif` (venv `idf6.1_py3.13_env`, cmake 4.0.3, ninja 1.12.1).
- Git Bash can no longer run idf.py directly (v6.1 refuses MSYS). Build/flash wrapper:
  `python idf_run.py build` / `python idf_run.py -p COM3 flash` (strips MSYSTEM, runs
  export.bat + idf.py under cmd). PowerShell path unchanged in spirit:
  `powershell -File tools\idfenv.ps1 idf.py build` (script updated to v6.1 paths).
- Device now on COM3.
- SSL note: installer downloads failed until `pip install --upgrade pip-system-certs certifi`.

---

### Update: 2026-09-04 — GPS scan-log tagging hardened (last-known-position cache)

**GPS tagging was already implemented 2026-08-24; this session adds robustness for real-world survey walks.**

**What was already working:**
- `session_logger.cpp` CSV format: `timestamp,mac,ssid,authmode,channel,rssi,lat,lon,sats,fix_q`
- `main.cpp` pipes `latest_gps` (from sensors queue) into `session_logger_log_ap()` on every scan result.
- Coordinates written as `lat_e7/1e7f`, `lon_e7/1e7f` when `fix_valid` is true; zeros when no fix.

**Improvement added:**
- `static GpsState s_last_fix` cache inside `session_logger.cpp`.
- On every valid fix: cache is updated with current GPS state.
- On brief fix dropout (tree cover, building shadow): scan logs use cached coordinates with `fix_q=0` instead of 0.0f/0.0f.
- This prevents "coordinate holes" during walking surveys where GPS flickers in and out.

---

### Update: 2026-09-04 — Concurrent dual-band Wi-Fi scan (channel bitmap)

**Hardware-native concurrent 2.4G + 5G scanning. Build clean, 0x1bc580.**

**Engine change:**
- Replaced alternating `scan_band(2G)` / `scan_band(5G)` calls with single `scan_all()` using `wifi_scan_config_t.channel_bitmap`.
- `channel_bitmap.ghz_2_channels = 0x7FFE` (channels 1-14) + `ghz_5_channels = US_5G_MASK` (channels 36-165).
- One `esp_wifi_scan_start()` call scans both bands concurrently — no `esp_wifi_set_band()` switching.
- Log format: `total=N (2.4G=X 5G=Y)` with per-band breakdown from channel numbers.
- Radio stays in `WIFI_BAND_MODE_AUTO` throughout; no band mode changes at runtime.

---

### Update: 2026-09-10 — SD OTA updates, v0.9.0, chart upgrade, deep-sleep power off

**Four features shipped and flashed same day. Build 0x1D0FF0 (1.84 MB).**

1. **SD-card OTA firmware updates** (`bb1072e`, `7a3cbac`): partition table gained
   `otadata` (SPIFFS shrunk 8 KB, all other offsets unchanged). `main/ota/ota_update`
   scans `/sdcard` for `*.bin` (256 KB–2 MB + ESP magic check), flashes to the next
   OTA slot with progress callback, sets boot partition, restarts. Settings → Firmware
   shows running version + picker with per-file versions read from each image's
   `esp_app_desc_t`; same-version rows dimmed, amber warning on confirm. Flash runs on
   a PSRAM-stack task; progress via `lv_async_call`; failed writes never marked valid
   (bootloader falls back to factory).
2. **Fixed app version `v0.9.0`** (`9d42e1e`, tag `v0.9.0`): `CONFIG_APP_PROJECT_VER_FROM_CONFIG`
   in sdkconfig.defaults — every image self-identifies.
3. **RSSI detail chart upgrade** (`df8ba37`): history 32 → 64 samples (~5.3 min window at
   5 s cadence, +2 KB static), now/min/max/avg stats line, grid div lines, -25/-100 dBm
   corner labels (LVGL v9 chart has no axis-tick API), point markers, live tier color.
   Roadmap item marked complete (first shipped 2026-09-04).
4. **Deep-sleep power off**: Settings → Power → Power Off → backlight off → deep sleep,
   wake on BOOT button. GPIO0 is RTC-capable on ESP32-C5 (RTCIO ch 0) → EXT1 wakeup via
   `esp_sleep_enable_ext1_wakeup_io(ANY_LOW)`. Boot logs reset reason.

**Power hardware reality (NM-CYD-C5):** no battery connector exists. Board powers from
either USB-C port. Portable options: USB power bank (zero hardware work), or LiPo +
boost/charger module feeding a USB-C plug (keeps board stock). On-board headers (CN1
3.3V, P1, FPC2) expose 3.3V rail for a custom pack in a product housing. Battery
voltage sensing would need a resistor divider into an ADC pin (e.g. IO26 on P1) —
software side ready when wired.

---

**On-device signal history chart. Build clean, ~0x1bc870.**

**New feature:**
- `ui_wifi_detail.cpp` / `ui_wifi_detail.h`: new detail screen showing per-AP RSSI history as an LVGL line chart.
- Tap any AP row in `SCR_WIFI` to open detail view. `LV_EVENT_SHORT_CLICKED` on row objects — scroll gesture safe.
- Chart: 280×130 px, Y-range -100 to -25 dBm, line colour matches current severity tier (green/yellow/orange/red).
- 32-sample circular history buffer per AP, stored inline in `scan_engine.cpp` `PoolEntry` (~2.2 KB total).
- Chart refreshes every 2 s via timer; info label shows channel, current RSSI, auth mode.
- Back button returns to Wi-Fi list; all visibility timers pause/resume correctly.

**Engine changes:**
- `scan_engine.h`: added `RSSI_HISTORY_LEN` (32) and `scan_engine_get_history()` API.
- `scan_engine.cpp`: `RssiHistory_t` appended to `PoolEntry`; `history_append()` on every `pool_upsert()` (new + existing).
- `ui_wifi.cpp`: `RowState` extended with `bssid[6]`, `channel`, `authmode`, `severity`. Rows made clickable.
- `main/CMakeLists.txt`: added `ui/ui_wifi_detail.cpp`.

---

### Update: 2026-09-04 — New screens (GPS, Alerts, Settings), home grid UI, alert engine, freeze fixes

**Major feature batch + stability hardening. Owner-verified on-device.**

**New screens landed:**
- `SCR_GPS` (`ui_gps.cpp`): live lat/lon, fix quality, sat count, UTC time. GPS data piped from `sensors.cpp` via `ui_gps_post_gps()`.
- `SCR_ALERTS` (`ui_alerts.cpp`): scrollable log of alert events (target SSID matches, RSSI threshold breaches). Alert engine (`alert_engine.cpp`) writes to NVS and checks every Wi-Fi scan result in real time.
- `SCR_SETTINGS` (`ui_settings.cpp`): SSID target list management with LVGL keyboard, add/delete/edit, NVS persistence. NULL guards on every `lv_*_create` call for graceful OOM handling.

**UI changes:**
- Home screen redesigned from vertical strip list to 2×4 color block grid (`make_block` helper). Blocks: Wi-Fi (green), BLE (cyan), Spectrum (orange), Environment (blue), GPS (purple), Alerts (red), Settings (grey).
- Navigation debounce: 150 ms `nav_guard()` on all home buttons prevents double-screen-load from bounce.
- "Environment" spelled out on home block (was "ENV").
- All screen visibility toggles (`ui_*_set_visible`) hooked through home nav so timers pause/resume correctly.

**Hardening / freeze fixes (2026-09-03):**
- `lvgl_port.cpp`: 256 KB PSRAM-backed secondary LVGL memory pool added after `lv_init()`. Absorbs overflow from heavy screens (Settings keyboard).
- `sdkconfig.defaults` + `sdkconfig`: `CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=256`. Root cause of prior freeze: TLSF `block_size_max` was 64 KB (from `LV_MEM_SIZE`), rejecting the 256 KB pool silently. Pool addition failed → OOM on Settings → `LV_ASSERT_NULL(obj)` hung `ui_task` in `while(1)` → watchdog trigger 5 s later.
- `sdkconfig.defaults` + `sdkconfig`: `CONFIG_LV_USE_ASSERT_NULL` disabled. OOM now returns NULL gracefully; existing NULL guards handle it.
- `sdkconfig.defaults`: `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` (carried forward from 2026-08-31).
- BLE screen: `ui_ble.cpp` refreshed with lazy row build (`BUILD_BATCH=2`), list y=48→64, back button topmost (z-order fix).

**Build & verification:**
- Commit `87cdd2d` pushed to `origin/main`.
- Binary `0x1bbc70` (~1.8 MB), 13% free in app partition.
- Boot capture: `LVGL PSRAM pool added: 256 KB` confirmed, no watchdogs, heap ~8.3 MB.
- Owner stress-tested: rapid in/out of Settings, Wi-Fi, BLE, GPS — no freezes.

**New lessons codified:**
- TLSF max pool size = `LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE`; pool allocation > this limit fails silently.
- `LV_USE_ASSERT_NULL` turns OOM into a watchdog death spiral on resource-constrained targets; disable it and guard at call sites.

---

### Update: 2026-08-31 — Hardening: disable secondary USB-SERIAL-JTAG console
> **Workspace:** `D:\SiteSurvey Pro` | **GitHub (manual backup only):** `https://github.com/bigjoe420/SiteSurvey-Pro`

---

## 1. Milestone History (Chronological — Newest First)

### Update: 2026-08-31 — Hardening: disable secondary USB-SERIAL-JTAG console

**One-line `sdkconfig.defaults` change to prevent task-blocking freeze recurrence.**
Root cause of the 2026-08-31 freeze incident ("tap tap tap, nothing, then it works") was identified as `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` + per-event diag logging + no monitor draining COM7. The secondary console blocks the calling task when its output buffer is full and unread. With this setting disabled, `ESP_LOG` / `printf` no longer stall `ui_task` if per-event instrumentation is re-enabled without an attached monitor.

- `sdkconfig.defaults`: added `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` with explanatory comment block
- Generated `sdkconfig`: `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG` unset, `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` at line 2721
- Build: clean, binary `0x1b8b10` (~4.3 KB smaller than prior)
- Flash & 40-second boot capture: all subsystems nominal, no stalls, heap stable ~8.3 MB
- Primary USB-OTG-Serial (CDC) console remains active for flash/monitor operations

**New lesson codified:** Per-event logging on a chip with `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` must never be left enabled without a monitor attached — or simply disable the secondary console entirely.

---

### Update: 2026-08-31 — Back-button multi-tap issue RESOLVED (geometry, not firmware) + freeze incident root-caused

### Update: 2026-08-31 — Back-button multi-tap issue RESOLVED (geometry, not firmware) + freeze incident root-caused

**Resolution of the issue parked 2026-08-29.** Temporary instrumentation (tagged `DIAG(audit S3)`: touch DOWN/UP transition logging in `lvgl_port.cpp`; PRESSED/RELEASED/CLICKED callbacks on all four back buttons) exonerated the firmware completely — across two monitored owner sessions every delivered sample reached LVGL and **every in-zone PRESSED converted to CLICKED (70/70; zero in-zone failures, zero pressed-but-no-click)**. The sampler hardening (prio 24 + `vTaskDelayUntil`) was correct and stays, but it was never the cause of the misses.

**Actual cause — pure hit-zone geometry:**
1. First miss class: presses landing in the dead strip y 45–59, below the old zone (bottom y=44) and above the list top (y=64). Fixed by `ext_click_area(8 → 24)` on wifi/ble back buttons → owner-measured ~97.5% first-tap; same fix then applied to env/spectrum (their apparent "regression" was simply those two screens still unpatched).
2. Residual class: one tight corner cluster at x 84–88 / y 62–68 (thumb habitually lands at the button's bottom-right corner, 2–8 px below the zone bottom y=60; successful presses skewed low-right overall, median (57,37) vs. visual center (44,20)). Fixed by `ext_click_area(24 → 32)` on **all four** back buttons → zone now y≤68 / x≤116; back button is topmost so it wins the overlap with the top 4 px of list row 1 (x≤116).

**Freeze incident (2026-08-31):** owner reported "tap tap tap, nothing happens, then it works" immediately after the ext-32 flash. Root cause was environmental, not the geometry change: freeing COM7 for flashing killed the owner's two `idf.py monitor` windows, and with `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` the secondary console **blocks the calling task when no host drains it** — the per-tap diag logging then stalled ui_task. **All `DIAG(audit S3)` instrumentation stripped from the five files (deletions-only diff), freezes gone, owner-verified same day.** Latent hazard: the secondary-USB-console config will stall any future verbose logging when no monitor is attached — disabling `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG` is a one-line hardening candidate (not applied).

**Files touched this arc:** `main/touch/touch.cpp` (prio 24, `vTaskDelayUntil`), `main/ui/lvgl_port.cpp` (diag added then stripped), `main/ui/ui_wifi.cpp` / `ui_ble.cpp` / `ui_env.cpp` / `ui_spectrum.cpp` (ext_click_area 32; diag added then stripped). Every step Corollary-20 diff-gated, built, flashed, boot-verified. **Final state owner-verified: freezes gone, back button first-tap reliable, "feels much better."**

---

### Update: 2026-08-29 — Touch sampler hardening: prio 24 + phase-locked 50 Hz (back-button issue parked)

**Context:** Full code audit vs `e3f5d8b` for the "back button needs multiple taps on Wi-Fi/BLE screens (30–50%)" symptom. Audit verdict: the working-tree geometry fixes (list y=64, back button topmost, 80×32 + ext_click_area(8)) were **correct**; residual misses are temporal — touch samples lost between sampler wake-ups. Control-group proof: env/spectrum back buttons are smaller targets (80×32, no ext area) yet responsive, so the cause is not geometric. Full audit report: Kimi workspace `SiteSurvey Pro\AUDIT_2026-08-29_back_button_touch.md`.

**Fixes landed (one at a time; each Corollary-20-reviewed via scoped diff, built, flashed, 40 s monitored):**
1. `main/touch/touch.cpp` — sampler task prio 20 → **24** (ties ui_task; 24 is max valid at configMAX_PRIORITIES=25). The old comment claimed "above Wi-Fi (23)" while passing 20 — factually wrong; the sampler sat below Wi-Fi (23), esp_timer/lv_tick (22), and NimBLE host (21, `configMAX_PRIORITIES-4` in nimble_port_freertos.c).
2. `main/touch/touch.cpp` — `vTaskDelay` → **`vTaskDelayUntil`**: phase-locked 20 ms cadence (was (10,20] ms tick quantization + execution drift at CONFIG_FREERTOS_HZ=100).

**On-device results:** Fix 1 owner-verified, no regressions. Fix 2: clean boot, `touch sampler task running @ 50 Hz`, no WDT/abort/panic, heap flat ≈8.34 MB over 40 s. **Back button still not single-tap reliable** — owner decision 2026-08-29: **park at bottom of queue**. Reopen path = audit §3 experiment: log `LV_EVENT_PRESSED` alongside `CLICKED` on the back buttons; PRESSED-never-fires → sample loss; PRESSED-without-CLICKED → LVGL event semantics / press-point drift.

**Housekeeping note:** boot-log "Compile time" is stale on incremental builds (`esp_app_desc.c.obj` regenerates only on full builds). Verify incremental flashes by object mtime + binary size + flash hash instead. Candidate footnote for Commandment VI.

---

### Update: 2026-08-27 — Task watchdog hang fix: ui_task stack overflow + timer lifecycle violations

**Problem:** Touch responsiveness degraded during testing; device crashed twice and rebooted. Serial capture showed `task_wdt` triggered on `ui_task` stuck at PC `0x4207bf30` (inside `lv_draw_add_task`). Register dump showed corrupted RA (`0x4207bece`, inside the same function) — classic stack-overflow signature.

**Root cause:** `ui_task` stack was 6,144 bytes — insufficient for concurrent load of 4× canvas-based 3D spinners (`lv_canvas_finish_layer` → draw dispatch), BLE/Wi-Fi list refreshes with `lv_obj_update_layout()`, and active LVGL animations. Stack overflow corrupted the saved return address, causing `lv_draw_add_task` to "return" into its own body → infinite loop. Secondary issues: Spectrum and Env screen timers were never paused when hidden (Corollary 16 violation), wasting CPU/draw-task cycles.

**Fix applied:**
1. **`main/ui/lvgl_port.cpp`**: `ui_task` stack increased from 6,144 → **12,288 bytes**.
2. **`main/ui/ui_spectrum.cpp`**: Saved `lv_timer_t* s_timer` handle; added `lv_timer_pause()/lv_timer_resume()` in `ui_spectrum_set_visible()`.
3. **`main/ui/ui_env.cpp`**: Saved `lv_timer_t* s_timer` handle; added `lv_timer_pause()/lv_timer_resume()` in `ui_env_set_visible()`.

**Build:** Clean compile, ESP-IDF v6.0.1, app binary 0x1b9630 bytes (14% free in 2 MB partition).

---

### Update: 2026-08-27 — NimBLE migration + PSRAM LVGL buffers: BLE+Wi-Fi coexistence fixed, on-device verified

### Update: 2026-08-27 — NimBLE migration + PSRAM LVGL buffers: BLE+Wi-Fi coexistence fixed, on-device verified

**Problem:** BLE Device screen (SCR_BLE) build was clean, but flashing produced an `ESP_ERR_NO_MEM` boot loop. Bluedroid consumed too much internal RAM alongside Wi-Fi on the ESP32-C5 (~126 KiB available).

**Root cause:** `r_ble_controller_init()` failed with `0x101` regardless of stack (Bluedroid or NimBLE) because internal RAM was exhausted by Wi-Fi driver + LVGL 60 KB draw buffers + other IDF structures.

**Fix applied (three-pronged):**
1. **Switched BLE host from Bluedroid to NimBLE** — `main/ble/scan_ble.cpp` completely rewritten using `nimble/nimble_port.h`, `host/ble_hs.h`, `ble_gap_disc()`, `ble_hs_adv_parse_fields()`. Public API (`scan_ble.h`) unchanged. `sdkconfig.defaults`: `CONFIG_BT_NIMBLE_ENABLED=y`, `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`, `CONFIG_BT_NIMBLE_MAX_BONDS=0`, `CONFIG_BT_NIMBLE_MAX_CCCDS=0`.
2. **Moved LVGL draw buffers from internal RAM to PSRAM** — `main/ui/lvgl_port.cpp`: `heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM)` instead of `MALLOC_CAP_INTERNAL`. Freed ~61 KB of internal RAM for the BLE controller.
3. **Aggressive Wi-Fi buffer reduction retained** — `sdkconfig.defaults`: `STATIC_RX=4`, `DYNAMIC_RX=8`, `STATIC_TX=4`, `CACHE_TX=8`, `MGMT_SBUF=8`, `TX_BA_WIN=4`, `RX_BA_WIN=4`.

**On-device verification (COM7, 40 s capture):**
- Boot → splash → home screen with 4 buttons: **PASS**
- `ble_scan: NimBLE host synced` → `scan started`: **PASS**
- BLE devices discovered: `1A:C9:45:8A:CF:07 -48 dBm S`, `3F:BA:D5:13:DA:D9 -56 dBm M`, `ckc_10028a37f8 C2:2F:8B:04:27:D0 -65 dBm M` — **PASS**
- Wi-Fi dual-band scan concurrent with BLE: `JK5 5G ch40 -32 dBm`, `Gbabies6 2.4G ch11 -50 dBm` — **PASS**
- BME680 env readings: `24.41 C  66.93 %RH  101738 Pa` — **PASS**
- GPS NMEA streaming (`$GPRMC`, `$GPGGA`) — **PASS**
- No abort, no reboot, 40+ s continuous operation — **PASS**

**Code state:** Build clean, flashed, verified. Binary shrank from 2.0 MB → 1.8 MB.

---

### Update: 2026-08-26 — BLE Device List screen (SCR_BLE) implemented and build-clean

**New feature:** passive BLE observer scan with live device list UI. Modeled after the Wi-Fi scan list pattern with lazy row creation, change guards, and timer pause/resume on visibility.

**Code landed:**
- `main/ble/scan_ble.{h,cpp}` — BLE Bluedroid GAP observer: passive scan (50 ms interval / 30 ms window), 10 s burst + 2 s pause cycle. Parses advertisement data for device name (AD type 0x08/0x09) and manufacturer data (0xFF). 32-entry static pool with LRU eviction, spinlock-protected snapshot, queue posting to main task.
- `main/ui/ui_ble.{h,cpp}` — LVGL screen: RSSI tier-colored bars, device name (or MAC if unnamed), manufacturer data prefix + RSSI. 3 s refresh timer, lazy row build, `LV_ANIM_OFF` change guards, scroll momentum enabled.
- `main/ui/ui_home.cpp` — Added BLE SCAN button (cyan, 4th button in vertical stack). Button heights reduced from 56 px to 40 px to fit 4 buttons in 320×240.
- `main/main.cpp` — `ble_scan_init()` in `late_init_task`, `ble_scan_queue()` added to queue set, BLE event logging in main loop.
- `main/CMakeLists.txt` — Added `ble/scan_ble.cpp` to SRCS, `ble` to INCLUDE_DIRS, `bt` to REQUIRES.
- `sdkconfig.defaults` — Added `CONFIG_BT_ENABLED=y`, `CONFIG_BT_BLE_ENABLED=y`, `CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y` (required for Bluedroid `esp_ble_gap_start_scanning` API on ESP-IDF v6.0.1).

**Build notes:**
- First attempt failed link: `esp_ble_gap_start_scanning` undefined. Root cause: `CONFIG_BT_BLE_42_FEATURES_SUPPORTED` was unset, so Bluedroid excluded the legacy GAP scan API despite `esp_gap_ble_api.h` being present. Enabling the config fixed the link.
- `BT_CONTROLLER_INIT_CONFIG_DEFAULT()` triggers `-Wmissing-field-initializers` on ESP32-C5 (new fields `cca_drop_mode`, `cca_low_tx_pwr` not in macro). Suppressed locally with `#pragma GCC diagnostic ignored`.
- Build clean 2026-08-26: `SiteSurvey-Pro.elf` ~21 MB (with debug symbols), partition fit OK.

---

### Update: 2026-08-26 — Background 50 Hz touch sampler fixes scroll stutter; single-sample read reverted

**Context:** Wi-Fi list scrolling was jerky and unreliable — it took multiple swipes to move, and sometimes the list would not respond to drag gestures at all. Touch tap responsiveness had been fixed earlier (Corollary 11, 18, 19) but scroll remained broken. The single-sample touch read experiment (removing 3-sample averaging) made touch responsiveness worse and killed scrolling entirely.

**Root cause:** `touch_read_cb()` was called from inside `lv_timer_handler()` (UI task, prio 24). When the user scrolled, LVGL queued multiple partial-frame DMA transfers on the shared SPI bus. Each `touch_read()` call performed 7 SPI transactions (Z1 + 3×X + 3×Y). During heavy scroll, these touch reads collided with active DMA, causing inconsistent timing and irregular drag deltas. The `s_flush_in_progress` counter + skip logic (Corollary 11) helped but was a band-aid — touch still stalled when DMA was active.

**Fixes applied:**
1. **`touch.cpp`:** Added `touch_start_sampler()` — creates an `esp_timer` periodic callback at 50 Hz (20 ms) that calls `touch_read()` independently of LVGL and stores results in a `portENTER_CRITICAL`-protected buffer.
2. **`touch.cpp`:** Added `touch_read_latest()` — lock-free buffer read, zero SPI, returns the most recent sampled coordinates.
3. **`lvgl_port.cpp`:** `touch_read_cb()` now calls `touch_read_latest()` instead of `touch_read()`. The UI task NEVER blocks on SPI for touch.
4. **`lvgl_port.cpp`:** Removed `s_flush_in_progress`, `s_flush_mux`, `flush_done_cb`, and `display_register_flush_done_cb()` call — the DMA tracking machinery is no longer needed since touch doesn't touch SPI.
5. **`lvgl_port.cpp`:** Removed timing instrumentation (`esp_timer_get_time()` in `ui_task()` and `touch_read_cb()`) — audit complete, now log noise.
6. **`ui_wifi.cpp`:** Added `LV_OBJ_FLAG_SCROLL_CHAIN` to row containers so drag gestures chain up to the scrollable list.
7. **`ui_wifi.cpp`:** Explicit scroll configuration on `s_list` — `SCROLLABLE`, `SCROLL_MOMENTUM`, `LV_DIR_VER`, `SNAP_NONE`.
8. **`ui_wifi.cpp`:** `lv_obj_update_layout(s_list)` after lazy row creation — forces LVGL to recalculate scrollable content height.
9. **`ui_wifi.cpp`:** Timer pause/resume on visibility — `lv_timer_pause()` when hidden, `lv_timer_resume()` when shown.

**Verification:** Device booted clean. Log confirms `touch: touch sampler running @ 50 Hz`. Touch tap responsiveness restored to baseline (first-tap entry/exit). Scroll on Wi-Fi list tested and confirmed working — smooth up/down swipes.

**Commit:** `871c9a9` — pushed to origin/main.


### Update: 2026-08-26 — DMA counter race fix: critical section on BOTH sides, timeout 50 ms → 500 ms, instrumentation stripped

**Context:** After restoring the non-blocking DMA flush (Corollary 11), touch was "way way better" but occasionally needed a double-tap on back. A 50 ms safety timeout was added to recover from stuck counters, but it fired constantly — on almost every frame — because partial rendering queues 5+ DMAs per frame (5 × ~12 ms = ~60 ms). The constant timeout resets caused touch to periodically block on SPI, making responsiveness worse than without the timeout.

**Root cause:** `flush_done_cb` decremented `s_flush_in_progress` without `portENTER_CRITICAL`. On single-core ESP32-C5, the callback runs from task context (not ISR), so `ui_task` (prio 24) could preempt it mid-read-modify-write, corrupting the counter over time. The 50 ms timeout masked this by resetting the counter constantly, but created its own problem.

**Fixes applied:**
1. `flush_done_cb`: wrapped decrement in `portENTER_CRITICAL` / `portEXIT_CRITICAL` — matches the increment side in `flush_cb`
2. Timeout increased from 50 ms → 500 ms — now a genuine failsafe for driver hangs, not a frame-rate interrupt
3. Timing instrumentation (`Stats` struct, `esp_timer_get_time()` in `touch_read_cb` and `ui_task`) stripped — served its purpose, now log noise
4. `ui_env.cpp`: added `status_set()` change guard — status label only updates when state transitions (OFFLINE → WAITING → LIVE), not every 500 ms

**Verification:**
- Build clean, binary `0x15f670` (31% free) — smaller than baseline due to instrumentation removal
- Zero `flush timeout` warnings in 3-minute monitor session
- Touch avg 38–66 µs, max <1.5 ms during navigation
- Handler avg 47–52 µs during active navigation; occasional 50–115 ms spikes during Wi-Fi scan cycles (expected, scan engine prio 23)
- Owner report: "by the end of the session it seemed to be working great. i could tap around with responsiveness"

**New corollaries:** KIMI.md §5.14 — Corollary 18 (shared counter sync must protect both sides), Corollary 19 (DMA timeouts must account for multi-flush frames)

### Update: 2026-08-24 — Cleanup: remove timing instrumentation from lvgl_port.cpp

### Update: 2026-08-24 — Cleanup: remove timing instrumentation from lvgl_port.cpp

**Context:** The spin3d audit (previous entry) added `esp_timer_get_time()` measurement in `touch_read_cb()` and `ui_task()` to diagnose touch latency. The root cause was found and fixed (bool-gate -> `lv_timer_pause/resume`). The instrumentation served its purpose and was becoming permanent log noise.

**Removed:**
- `touch_read_cb()`: `int64_t t0/elapsed` + `ESP_LOGW` when touch read >500 us
- `ui_task()`: `int64_t t0/elapsed` + `ESP_LOGW` when `lv_timer_handler()` >5 ms

**Result:** Build clean (`lvgl_port.cpp.obj` recompiled), binary `0x15f480` (31% free). Boot log verified compile time `Aug 24 2026 18:23:38` — fresh. No timing warning spam. All subsystems nominal.

### Update: 2026-08-24 — Spin3d timer audit: bool-gate was not enough; pause/resume + 100 ms period
**Owner field report:** touch on home tabs and back buttons intermittently requires multiple taps — not crisp like the pre-spectrum baseline. Owner: "please do an audit of the code and check it out. lets not keep guessing."

**Audit method:** added timing instrumentation to `lvgl_port.cpp` measuring `lv_timer_handler()` and `touch_read()` execution time. Captured 30-second boot log.

**Root cause:** `ui_spin3d.cpp` created a global timer at 66 ms that **never stopped firing**. `ui_spin3d_enable(false)` only set a boolean `s_enabled`; the timer kept ticking every 66 ms, entering `tick()`, checking the boolean, and returning early. But LVGL still processed the timer overhead. Canvas rendering to ARGB8888 buffers is CPU-intensive — four 40×40 spinners pushed `lv_timer_handler()` to 15–55 ms per call (spikes to 115 ms during Wi-Fi scan cycles). With `ui_task` yielding every 10 ms, touch polling dropped from 100 Hz to ~20–50 Hz.

**Fixes applied:**
1. **`ui_spin3d_enable()` now uses `lv_timer_pause()` / `lv_timer_resume()` (CRITICAL).** The timer is literally removed from LVGL's active timer list when the env screen is hidden. When `ui_env_set_visible(false)` is called (e.g., navigating back to home), `lv_timer_pause(s_timer)` stops all spin3d CPU consumption. When the env screen is shown again, `lv_timer_resume(s_timer)` restores it. Verified `lv_timer_pause` / `lv_timer_resume` exist in LVGL v8 headers (`managed_components/lvgl__lvgl/src/misc/lv_timer.h`).
2. **Spin3d timer period 66 ms → 100 ms.** Reduces CPU load by ~33% while the env screen is visible. The 3D wireframe animation is still visibly spinning at 10 Hz.

**Boot log findings (COM7, 30 s capture):**
- **Home screen** (after splash fully dismissed): `lv_timer_handler()` consistently **<5 ms**, `touch_read()` **<500 µs** — clean baseline confirmed.
- **Splash screen:** 8 infinite bar animations cause 10–25 ms spikes until deferred delete completes (~11.6 s → ~11.7 s). Transient and expected.
- **Scan cycles:** Occasional 7–10 ms spikes at 21 s correlate with SD card session CSV flush (shared SPI bus contention). Non-fatal and brief.
- **No evidence of `lv_timer_handler()` overload on home screen** after splash dismissal — the spin3d bool-gate was the sole remaining CPU hog when hidden.

**Code landed:**
- `main/ui/ui_spin3d.cpp` — `ui_spin3d_enable()` now calls `lv_timer_pause(s_timer)` / `lv_timer_resume(s_timer)`; timer creation changed from 66 ms to 100 ms.
- `main/ui/lvgl_port.cpp` — timing instrumentation kept for regression testing (measures `lv_timer_handler()` and `touch_read()`; logs warnings at >5 ms / >500 µs thresholds).

**Verification (COM7, compile time `Aug 24 2026 16:45:??`, 35 s capture):**
- Clean boot, zero resets/abort/Guru errors.
- `lv_timer_handler()` warnings cease after splash deferred delete completes (~11.7 s).
- Build: `0x15f5d0` (1.40 MB), 31% partition free.

**KIMI.md §5.14 updated** with Corollary 16 (bool-gate timers still consume CPU; must use `lv_timer_pause`/`resume`) and Corollary 17 (canvas animations are the most expensive widget type; benchmark on-device before accepting visual).

---

### Update: 2026-08-24 — Environmental overlay on Wi-Fi scan screen (BME680 live readout)

### Update: 2026-08-24 — Environmental overlay on Wi-Fi scan screen (BME680 live readout)
**New feature:** compact env readout label on the Wi-Fi scan screen (SCR_WIFI) showing real-time BME680 temperature (°F), humidity (%), and pressure (hPa). Positioned top-right, next to the back button. Updates alongside the Wi-Fi list refresh (5 s). Uses the same spinlock-protected snapshot pattern as `ui_env.cpp` (`ui_wifi_post_env()` / `portMUX_TYPE`).

**Code landed:**
- `main/ui/ui_wifi.{h,cpp}` — `s_env_overlay` label, `update_env_overlay()`, `ui_wifi_post_env()` with spinlock copy.
- `main/main.cpp` — `ui_wifi_post_env()` called alongside `ui_env_post_env()` on every `env_queue` message.

**Verification (COM7, compile time `Aug 24 2026 09:01:19`, 15 s capture):**
- Clean boot, zero resets/abort/Guru errors.
- Overlay updates from "ENV WAITING..." → live values after first BME680 sample.
- Build: `0x15f420` (1.40 MB), 31% partition free.

---

### Update: 2026-08-24 — Touch responsiveness restored (spectrum animation audit + fix)

### Update: 2026-08-24 — Touch responsiveness restored (spectrum animation audit + fix)
**Root cause:** `LV_ANIM_ON` on 24 spectrum bars created a ~300 ms render storm every 5 s, starving XPT2046 touch sampling on the shared 20 MHz SPI bus. Animations also survived screen unload, so navigating away mid-animation prolonged the lag.

**Fix applied to `main/ui/ui_spectrum.cpp`:**
- All `lv_bar_set_value(..., LV_ANIM_ON)` → `LV_ANIM_OFF` — instant snap, zero animation overhead.
- Per-bar change guards (`shown_rssi`, `was_active`) — widgets touched only when value or activity state actually changes.
- Footer label change guard (`s_footer[48]` string cache) — only rewritten when text differs.

**Side effect:** SD card mount and session logger now operational. Previously blocked by `allocate_dma_buf: not enough mem (0x101)` (2026-08-23 known issue). With animation overhead eliminated, SDSPI DMA buffer allocation succeeds.

**Verification (COM7, compile time `Aug 24 2026 09:01:19`, 40 s capture):**
- Clean boot, zero resets/abort/Guru errors.
- SD card mount OK: `SL32G, 30436 MB`; self-test passed.
- Session logger auto-started: `/sdcard/survey_19700101_000010.csv`.
- Build: `0x15f160` (1.40 MB), 31% partition free.
- **Corollary 15:** Animations on high-frequency refresh widgets are toxic on shared SPI buses — always benchmark touch latency after adding motion.

---

### Update: 2026-08-24 — Session CSV logger landed (SD write path **now operational** post-animation fix)
**New subsystem:** `session_logger` batches Wi-Fi scan results to SD card in CSV format (`survey_YYYYMMDD_HHMMSS.csv`). 16-entry RAM buffer, auto-flush every 5 s or when full. Header row: `timestamp,mac,ssid,authmode,channel,rssi,lat,lon,sats,fix_q`. GPS coordinates logged when fix is valid; timestamp uses GPS HHMMSS when available, system clock fallback.

**Code landed:**
- `main/storage/session_logger.{h,cpp}` — `session_logger_init/start/stop/log_ap/flush/active`. Auto-starts a session on first AP. `fmt_ts()` overrides time portion with GPS HHMMSS when fix is valid.
- `sdkconfig.defaults` — `CONFIG_FATFS_LONG_NAMES=y` (enables `survey_YYYYMMDD_HHMMSS.csv` filenames).
- `main/main.cpp` — `session_logger_init()` after `sd_card_init()`; cached `GpsState` updated from `env_queue`; every `ScanResult_t` passed to `session_logger_log_ap()`.
- `main/CMakeLists.txt` — `storage/session_logger.cpp` added to SRCS.

**Verification (COM7, compile time `Aug 24 2026 09:01:19`, 40 s capture):**
- Clean boot, zero resets/abort/Guru errors.
- ~~SD card mount fails with `allocate_dma_buf: not enough mem (0x101)` — same known issue from 2026-08-23.~~ **RESOLVED** by spectrum animation fix (see entry above) — SD mount and session logger now operational on-device.
- Build: `0x15f160` (1.40 MB), 31% partition free.

---

### Update: 2026-08-24 — Channel Spectrum screen (SCR_SPECTRUM) live
**New screen:** per-channel RSSI density/occupancy visualization for 2.4 GHz (channels 1–11) and 5 GHz (13 common channels: UNII-1 36–64 + UNII-3 149–165). Bars are `lv_bar` widgets colored by RSSI tier (green/yellow/orange/red), animated on refresh. Footer shows total AP count and busiest channel.

**Code landed:**
- `main/ui/ui_spectrum.{h,cpp}` — spectrum screen following the same lazy-create + visibility-gate pattern as `ui_wifi`/`ui_env`. `build_bar_row()` creates track backgrounds + per-channel `lv_bar` widgets. `do_refresh()` aggregates `scan_engine_snapshot()` by channel, updates bar values/colors, and computes busiest-channel summary. 5 s refresh timer.
- `main/ui/ui_home.cpp` — third button "SPECTRUM" (amber `#FF9800`) added between Wi-Fi and Environment.
- `main/CMakeLists.txt` — `ui/ui_spectrum.cpp` added to SRCS.

**Design notes:**
- 2.4G bars: 11 channels, slot_w=29, bar_w=20, all channels labelled.
- 5G bars: 13 channels, slot_w=24, bar_w=16, sparse labels (every other + last).
- Bars use `lv_bar` range -100..-25 dBm; inactive channels show a 2 px stub at minimum.
- Back button uses `LV_EVENT_CLICKED` (press-through prevention per KIMI.md Corollary 12).

**Verification (COM7, compile time `Aug 24 2026 05:46:??`, 40 s capture):**
- Clean boot, zero SW_CPU resets, zero aborts, zero Guru errors.
- Splash gates clear at ~11.7 s; home screen shows three buttons.
- Scan results observed on ch1, ch6 (2.4G) and ch44, ch161 (5G) — all within displayed channel maps.
- Build: `SiteSurvey-Pro.bin` 0x15d560 (1.39 MB), 32% partition free.

---

### Update: 2026-08-24 — Touch responsiveness "perfect baseline": non-blocking DMA flush + event discipline + lazy row creation"perfect baseline": non-blocking DMA flush + event discipline + lazy row creation
**Owner field report:** touch is "absolutely great" — the best responsiveness yet. Back button occasionally needs a second tap but is nearly instant. Wi-Fi tab loads cleanly without black-screen flashing. Boot is clean and fast.

**Fixes:**
1. **Non-blocking DMA flush in `lvgl_port.cpp` (CRITICAL).** The flush callback previously blocked on `esp_lcd_panel_draw_bitmap()` — a 320×240 partial flush over 20 MHz SPI takes ~12 ms, during which `ui_task` cannot sample touch. Replaced with non-blocking DMA: `esp_lcd_panel_draw_bitmap()` returns immediately, `display_register_flush_done_cb()` fires `flush_done_cb()` on the VSYNC interrupt, and `lv_disp_flush_ready()` completes the LVGL flush cycle. Touch sampling now runs continuously even during heavy redraw.
2. **Home buttons → `LV_EVENT_PRESSED` (instant).** `ui_home.cpp` buttons previously used `LV_EVENT_CLICKED` + `lv_indev_wait_release()`, adding an artificial release wait that delayed every tab switch. Forward navigation now fires on press, not release.
3. **Back buttons → `LV_EVENT_CLICKED` (prevents press-through).** `ui_wifi.cpp` and `ui_env.cpp` back buttons use `LV_EVENT_CLICKED` so a lingering finger on the back button does not immediately trigger the next screen's hit-box when the transition completes. Forward = PRESSED (instant), Back = CLICKED (safe).
4. **Lazy Wi-Fi row creation.** `ui_wifi_create()` no longer builds all 16 rows at init time. Rows are created on first `do_refresh()` when first needed, gated by `s_rows_built`. This removes ~200 ms of init-time object creation that was starving touch during the first Wi-Fi tab entry.
5. **Spin3d timer 40 ms → 66 ms (25 Hz → 15 Hz).** The ENV gauge canvases redraw every tick; 25 Hz consumed significant SPI bandwidth. 15 Hz is still smooth and cuts DMA contention by ~40%.
6. **XPT2046 2-sample debounce attempted and reverted.** A 2-sample majority filter in `touch_read_cb()` was tried to suppress single-sample noise, but it was too aggressive — quick taps were swallowed. Reverted; the non-blocking DMA flush addressed the root cause (touch starvation during SPI block), making debounce unnecessary.

**Verification (COM7, compile time `Aug 24 2026 05:01:??`, 40 s capture):**
- Clean boot, zero SW_CPU resets, zero aborts, zero Guru errors.
- Touch responsive immediately after splash fade; no perceptible lag on home buttons or back buttons.
- Wi-Fi list populates without black-screen flash; rows build lazily on first refresh.

**KIMI.md §5.14 updated** with Corollary 11 (non-blocking DMA flush), Corollary 12 (button event strategy), Corollary 13 (lazy widget creation), Corollary 14 (adaptive delay black screen — deferred).

---

### Update: 2026-08-23 — Boot pause eliminated: deferred BL + late_init_task + obsolete delay removed
**Owner field report (boot only):** "two partial blue bars show up in the corner, screen is black, pauses for a second, then continues flawlessly." Root cause was synchronous heavy init in `app_main()` blocking the boot path combined with an obsolete 300 ms delay and backlight turning on before LVGL had rendered.

**Fixes:**
1. **Deferred backlight:** `display.cpp` no longer turns on the backlight. `lvgl_port.cpp` `backlight_on_once()` fires on `lv_display_flush_is_last()` — the panel stays dark until the first complete splash frame is on the glass, eliminating any chance of seeing GRAM clear gaps or panel settle artifacts.
2. **Late-init task:** all heavy init (`scan_engine_init`, `sensors_init`, queue-set setup, task starts, `sd_card_init`) moved to `late_init_task` at FreeRTOS prio 1. `app_main()` returns immediately after starting `ui_task` (prio 24). Wi-Fi firmware load from flash and PHY calibration now run in the background without ever preempting the UI.
3. **Obsolete delay removed:** `main.cpp` had `vTaskDelay(100)` + `vTaskDelay(200)` with a stale comment referencing `ui_task` prio 5. With `ui_task` now at prio 24, Wi-Fi (prio 23) cannot preempt it — the delay served no purpose and added 300 ms of dead time.

**Verification (COM7, compile time `Aug 23 2026 01:08:14`, 40 s capture):**
- `app_main()` returns at `663 ms` (was previously blocked until ~1400 ms).
- Backlight on first frame at `713 ms` (was 812 ms).
- `late_init_task` starts at `663 ms`; Wi-Fi station up at `1363 ms` — splash animates freely throughout.
- Zero SW_CPU resets, zero aborts, zero Guru errors.
- SD card mount fails with `allocate_dma_buf: not enough mem (0x101)` — internal RAM pressure from 2×30 KB LVGL buffers; non-fatal, logging disabled. To be addressed separately.

**KIMI.md §5.14 updated** with Corollary 8 (deferred backlight) and Corollary 9 (late-init task pattern).

---

### Update: 2026-08-23 — Queue-set race crash fixed: init queues BEFORE starting tasks
**Root cause:** `xQueueAddToSet(env_queue, qs)` in `main.cpp` was called AFTER `sensors_start_task()`. The sensor task's first `gps_poll()` (100 ms timeout) could post an `EnvSnapshot_t` before `main_task` reached `xQueueAddToSet`. FreeRTOS `xQueueAddToSet` returns `pdFAIL` if the queue already holds data, triggering `ESP_ERROR_CHECK` → `abort()`. On first boot after power-on the race was lost; on SW_CPU reset (second boot) timing shifted slightly and the race was often won — producing the "stutters twice and restarts" symptom the owner reported.

**Fix:** restructured `main.cpp` init order:
1. `scan_engine_init()` + `sensors_init()` — creates queues, does NOT start tasks
2. Get queue handles → create queue set → `xQueueAddToSet` both queues
3. THEN `scan_engine_start_task()` + `sensors_start_task()` — safe to post

**Also fixed:** `lvgl_port.cpp` log and comment still said "PSRAM" after draw buffers were moved to `MALLOC_CAP_INTERNAL` in a prior session. Corrected to "RAM".

**Verification (COM3, `idf.py fullclean` build, compile time `Aug 23 2026 01:08:14`, 40 s capture):**
- `rst:0x1 (POWERON)` — single clean boot, zero SW_CPU resets, zero aborts, zero Guru errors.
- All subsystems up: display 20 MHz, touch 2.5 MHz, LVGL "draw buffers in RAM", Wi-Fi dual-band, BME680 @0x76, GPS fix q=2 sats=8, SD 32 GB self-test OK.
- Heap steady ~8.35 MB; scan hwm 2540/3072, sensor hwm 1592/3072.

**KIMI.md §5.14 updated with Corollary 7** (queue-set ordering rule). §10.2 draw-buffer note corrected from PSRAM to RAM.

---

### Update: 2026-08-10 — UI polish debug: full audit, four fixes, clean 40 s verify
**Owner field report:** swipe up/down unresponsive ("nothing happens for a few swipes, then it goes"), tab taps require multiple attempts, first row crowds the tab bar (photo 11), `<hidden>` rows show scroll artifacts (photo 12). Critical abort at `main.cpp:105` (`xQueueAddToSet`) ~2.5–3 s after boot on previous build. Full code audit performed; four root causes identified and fixed:

1. **`lvgl_port.cpp` adaptive delay → fixed 10 ms (CRITICAL).** `lv_timer_handler()` return value was being used as sleep duration, capped at 500 ms. Touch is sampled inside `lv_timer_handler()` — sleeping 500 ms meant touch was read at most twice per second. Swipes were lost, taps required multiple attempts, and the resulting LVGL timing chaos destabilized the system (manifested as a bogus `ESP_ERROR_CHECK` abort). Reverted to fixed `vTaskDelay(pdMS_TO_TICKS(10))`.
2. **`ui_home.cpp` tab button styling order bug (CRITICAL).** The button styling loop ran BEFORE `lv_tabview_add_tab()`, so `nbtns == 0` and zero buttons were styled. The default theme's orange checked background leaked through, producing the orange bar in photo 11. Buttons also retained default hit-box sizing. Fix: moved styling loop AFTER tab creation.
3. **`ui_wifi.cpp` transparent row backgrounds → opaque black.** `LV_OPA_TRANSP` rows let previous frame content bleed through during scroll, causing ghosting/artifacts (photo 12, especially on `<hidden>` rows with tiny bars). Fix: `LV_OPA_COVER` with `lv_color_black()`.
4. **`ui_wifi.cpp` list top padding too tight.** `pad_all = 2` gave only 2 px below the 34 px tab bar, crowding the first row. Fix: `pad_top = 6` for visual breathing room.

**Also fixed:** `ui_home.h` duplicate `bool ui_home_env_active(void);` declaration (line 14) — cleanup only, no functional impact.

**Verification (COM7, full clean build, compile time `Aug 10 2026 12:44:17`, 40 s capture):**
- Zero faults, zero aborts, zero Guru errors across full capture.
- Backlight on first frame @ ~1662 ms; splash gates clear @ ~11.4 s.
- Scan cycles steady: `pool=19/64 evict=0 drops=0`, hwm scan 2540/3072.
- BME680 streaming every 5 s (valid), GPS NMEA ok=285 bad=0, SD self-test OK.
- Build: `SiteSurvey-Pro.bin` 0x15c4c0 (1.54 MB), 32% partition free.

**KIMI.md §5.14 updated with four new corollaries** (Corollary 3–6) covering ui_task cadence, tab button styling order, opaque row backgrounds, and list padding discipline.

### Update: 2026-08-11 (handoff marker) — UI polish debug continues in new session
Owner: "it's getting better... I know we can do better." Continuing the Wi-Fi screen / UI responsiveness debug in a dedicated new chat. Open items carried forward:
- Owner input given (2026-08-11): rows need one more small adjustment (photo to come in new session); gray text too dim at angles; functionality still needs work; do NOT push yet.
- UNCOMMITTED working-tree change carried into new session: all `#9E9E9E` gray text bumped to `#E8E8E8` in `main/ui/ui_wifi.cpp` (info column), `ui_home.cpp` (inactive tabs), `ui_env.cpp` (gauge names). Edited but not built/flashed/committed — bundle with the row fix.
- Confirm on glass: fixed columns aligned, `<hidden>` no longer overlaps band tag, right-edge clip gone, tab bar dark/green with underline, scroll feel smooth.
- Push `011e1d2` + `d37c37a` + `fe75ba3` (4 ahead of origin) only on owner confirmation.
- Queued after polish: Channel Spectrum screen, SD session logging (`CONFIG_FATFS_LONG_NAMES` for `survey_YYYYMMDD_HHMMSS.csv`).

### Update: 2026-08-11 (evening) — Layout/theme polish round
Owner bench photo evidence: Wi-Fi list columns ragged (info column rode SSID width), `<hidden>` jammed into the band tag, info text clipped at the right edge, and the tab bar rendered in the default theme's orange/lavender instead of the dark/green design. Fixes:
1. **Splash auto-delete:** the gate now uses `lv_screen_load_anim(..., auto_del=true)` — the splash's 12 infinite waterfall anims + 2 pulse anims die at fade instead of running invisibly inside ui_task forever (top suspect for the lingering post-boot drag).
2. **Wi-Fi rows: flex -> absolute geometry.** Rows are placed at fixed `y = i*32`, size 312x30; bar/ssid/info keep absolute aligns. Kills per-refresh flex reflow storms, guarantees aligned columns, and the right-aligned info now hugs a fixed x instead of the row's content width. New rows snap-fill their RSSI bars (LV_ANIM_OFF); only live RSSI changes on visible rows animate.
3. **Tab bar theming:** LVGL's default theme paints checked tab buttons orange. Each bar button is now flattened (transparent bg, no border/shadow, all states), grey inactive / green active text, green 2 px bottom-border underline on the checked tab; bar itself is opaque #101010.
- Verified (COM7): flash OK, app descriptor compile time `Aug 10 2026 10:14:49`, 40 s capture clean (no Guru/abort), Wi-Fi driver + scan + env flow steady. Awaiting owner photo confirmation of columns/underline/scroll feel.

### Update: 2026-08-11 (later) — UI responsiveness overhaul
**Commit `d37c37a`:** owner field report — splash felt long, main screen "does not want to move" when swiping. Root causes and fixes, all verified on-device (COM7, compile time `Aug 10 2026 06:53:16`, 40 s clean):
1. **Hidden-tab render tax:** the four ENV spinner canvases redrew every 40 ms even on the WI-FI tab, inside the same ui_task that samples touch. Fix: `ui_spin3d_enable()` master gate, driven by a tabview VALUE_CHANGED event (`ui_home_env_active()`); env gauge refresh also early-outs when hidden. A hidden ENV tab now costs ~zero.
2. **Render storm:** the Wi-Fi list rewrote all 16 rows every second — LVGL invalidates on every setter even with identical text. Fix: per-row change-guards (`RowState` cache); only changed bars/labels are touched. Refresh cadence 1 s → 2 s (scan cycle is ~15–21 s anyway).
3. **Splash latency:** first BME680 shot waited a full 5 s period → now fires immediately on sensor_task start (first env at ~2.0 s, was ~5.8 s). Splash also gained **tap-to-skip** (any tap satisfies both gates; status line says so). Remaining gate time is the first blocking 2.4 GHz scan (~5–11 s physics).
- Design rule confirmed (KIMI.md §5.14 + this): on a 20 MHz SPI panel, responsiveness = never render what isn't visible.

### Update: 2026-08-11 — Tab gesture discipline fix
**Commit `fe75ba3`:** owner field report — tab sliding was janky and intermittently "hung", and a vertical swipe on the Wi-Fi list could flip to the ENV tab. Root cause: the tabview content slider hijacks vertical scroll gestures, and a 320×240 full-screen slide is ~153 KB of pixels per frame over 20 MHz SPI (~60 ms/frame) — smooth sliding is physically impossible on this panel. Fix: content slider disabled (`lv_tabview_get_content` scrollable flag removed); tabs switch instantly via the bar, vertical gestures belong to the list. Verified on-device (COM7, compile time `Aug 10 2026 06:00:01`, clean boot). Rule of thumb for this hardware: animate widgets, never whole screens.

### Update: 2026-08-10 (late night) — Tabbed home + Live Wi-Fi Scan List
**Commit `f53cab3`:** the app now has its navigation shell — dark tabview (`main/ui/ui_home.{h,cpp}`) with **WI-FI** and **ENV** tabs (green selection accent, swipe/tab between them), and the first scanner-facing screen:
- **`ui_wifi.{h,cpp}`** — strongest-first AP list: 16 static row widgets (no heap in refresh), animated `lv_bar` RSSI bars colored by ROADMAP §5 tier (S green / M yellow / W orange / X red), SSID in tier color, `<hidden>` for empty ESSIDs, right-aligned `band ch rssi security` info, 1 px row separators, scrollable flex container, 1 s refresh from the new `scan_engine_snapshot()` (spinlock-guarded pool copy, strongest-first insertion sort).
- **`ui_env` refactored** to render into the ENV tab (status line + 2×2 gauges, same gauges/spinners, tighter layout).
- **`scan_engine`** — pool upserts now run under `s_pool_mux` (UI reads concurrently); new snapshot accessor.
- **Fix found only through on-device testing:** tabview + list + spinners + bars pushed ui_task's render call depth past its 4 KB stack → `Stack protection fault` boot loop right at the splash fade. ui_task stack 4 KB → 6 KB (`lvgl_port.cpp`); 45 s capture clean, scan cycles + env flow steady, heap 8,332,476. KIMI.md §10.2 stack table updated.
- Verified (COM7): flash digest matched; app descriptor showed the previous build's timestamp again (incremental-build gotcha — digest is the witness). Build 0x15c1c0 (1.50 MB), 32% free.

### Update: 2026-08-10 (night) — Reactive 3D wireframe spinners on SCR_ENV
**Commit `ad97d92`:** `main/ui/ui_spin3d.{h,cpp}` — a small software 3D renderer for LVGL: shape vertices (tetra/cube/octahedron/icosahedron) rotated about Y with a slow tilt wobble, perspective-projected (`DIST/(z+DIST)`), drawn as anti-aliased 2D lines into a 40×40 ARGB8888 canvas (PSRAM-backed, static 4-instance pool, shared 40 ms lv_timer — no heap in the frame path). Edge lists are derived at init from pairwise vertex distances, so each shape is just a corner table.
- **Reactive by design:** each gauge's spinner inherits its band color and spins faster as the reading climbs its scale (`speed = 0.02 + frac·0.10 rad/frame`) — TEMP octahedron, HUMIDITY tetrahedron, PRESSURE icosahedron, AIR cube. Offline/waiting fades spinners to a dim grey drift.
- Verified on-device (COM7): compile time `Aug 10 2026 04:31:38`, zero faults across 40 s, steady heap 8,334,668 (−35 KB vs pre-spinner: 4×6.4 KB canvas + objects — as designed). Build 0x15b7b0 (1.50 MB), 32% free.
- Pushed same day (owner-authorized): `e3b3446` then `ad97d92` — origin/main fully in sync.

### Update: 2026-08-10 (evening) — Environmental Dashboard (SCR_ENV) live
**Commit `e3b3446`:** `main/ui/ui_env.{h,cpp}` replaces the scaffold home screen with the first Phase 2 screen — four gradient-scale gauges in a 2×2 grid, built to the owner's expressive-UI directive (B-06/B-07):
- **Bands (equipment-grade):** TEMP 32–122 °F (blue <59 / green 59–82 / amber 82–95 / red >95); HUMIDITY 0–100 %RH (amber <30 static risk / green 30–60 / amber 60–75 / red >75 condensation); PRESSURE 950–1050 hPa (neutral teal→blue gradient, no alarm semantics); AIR (VOC) 0–150 kΩ (red <10k poor / amber 10–50k / green >50k, higher = cleaner).
- **Motion:** value numbers tween and scale markers ease (900 ms, ease-out) on every 5 s env update; value text color follows its band.
- Reuses the spinlock snapshot handoff + 500 ms lv_timer refresh pattern; header status OFFLINE/WAITING/LIVE mirrors the old readout's three states.
- `ui_hello.*` deleted (superseded; git history preserves). Temp stays °F on screen, °C in serial log.
- **Verified on-device (COM7):** compile time `Aug 10 2026 03:49:08`, zero LVGL asserts/faults over 30 s, splash gates → fade to dashboard at ~11.4 s, live gauges (≈69.5 F / 55 % / 1017 hPa / 4.0 kΩ-red). Build 0x1591c0 (1.46 MB), 33% free.
- Fix during build: esp-gcc `-Werror=missing-field-initializers` fires even on designated initializers here — gauge config struct uses default member initializers instead.

### Update: 2026-08-10 (later) — SD card driver VERIFIED ON-DEVICE + UI design language note
**Push:** owner-authorized push 2026-08-10 — origin/main advanced `b54aa16..52d78d0` (Fahrenheit display + SD driver). Local and remote in sync.

**SD bring-up (commit `52d78d0`):** `main/storage/sd_card.{h,cpp}` — SDSPI device on the shared SPI2 bus (CS=GPIO10, 20 MHz default alongside display/touch device handles), FATFS mount at `/sdcard`, write/read-back self-test, graceful absence (missing card → warning log, logging disabled, boot continues — same pattern as BME680). First-try success on COM7: compile time `Aug 10 2026 03:07:19` (Commandment VI passed), `sdcard: SD up: SL32G, 30436 MB` (owner's 32 GB card), `self-test OK: wrote and read back /sdcard/ssp_test.txt`. BME680/env streaming unaffected; full SPI coexistence (display 20 MHz + touch 2.5 MHz + SD) proven. Build: 0x1585b0 (1.39 MB), 33% partition free.
- Note: FATFS pulled in with LFN off — filenames must stay 8.3 (`ssp_test.txt` style) until LFN is enabled for session logs (`survey_YYYYMMDD_HHMMSS.csv` will need it).

**UI design language (owner directive, 2026-08-10 — applies to all future screens):**
- Owner loves the rainbow boot splash — the whole UI should be **expressive: rich color and motion** that captivates, not a flat utilitarian grid.
- **BME680 readings get gradient scales** (thermometer-style) beside each number so relativity is visible at a glance.
- Scales are **equipment-grade, not human-comfort**: the audience is IT professionals surveying sites for computers/equipment — think GPU-temperature monitoring scales (e.g., green nominal band, amber/red thermal alarm bands appropriate to hardware), each reading with its own domain-tuned range.
- Recorded in ROADMAP §7 (B-06, B-07); implement with the Environmental Dashboard (SCR_ENV) and extend the language to other screens.

### Update: 2026-08-10 — BME680 module verdict + Fahrenheit display
- **Module verdict (owner bench):** the original BME680 module was written off — replaced with a different breakout that has a hardware 0x76/0x77 address slide switch. Set, seated, worked immediately (`device ACKs at 0x76`). Root cause of the 2026-08-08 bus silence was therefore the original module itself (dead or mis-strapped part), as the idle-HIGH line check had concluded.
- **Fahrenheit display (commit `33cae00`):** home-screen env readout now shows °F (integer `°C×100 → °F×100` conversion at render; serial env log intentionally stays in °C). Verified on-device COM7: flash digest matched `SiteSurvey-Pro.bin` 0x146d30 (1.28 MB); 24.41 °C log ↔ 75.94 F on screen. Note: the `app_init` compile-time descriptor stayed stale (`Aug 9 05:39:43`) on this incremental build — known gotcha; binary digest is the freshness witness.
- Housekeeping note: D: drive was unmounted at session start (owner re-seated it); internet outage same morning, restored. No project impact.

### Milestone: Phase 1.4 VERIFIED ON-DEVICE + Repo Hygiene Overhaul — 2026-08-09
**Status:** ✅ **BME680 and GPS both live. Phase 1.4 closed. GitHub repo sanitized to portfolio grade: AI workflow docs (KIMI/ROADMAP/PROGRESS) purged from tracking and from all commit history; force-pushed.**

**On-device verification (COM7, 2026-08-09, 25–60 s captures):**
- Compile time `Aug 9 2026 05:39:43` — fresh build flashed and confirmed (Commandment VI passed).
- `i2c lines idle HIGH (SDA=1 SCL=1)` → `i2c scan: device ACKs at 0x76` → `sensors up: BME680 present, GPS streaming`. Module answered at 0x76 after owner applied the module-side fix (final step per the 2026-08-08 bench order — exact strap change to be confirmed with owner).
- **Live BME680:** env valid every 5 s — 20.17→20.45 °C, 53.9→54.9 %RH, ~101,607 Pa, gas 5,191→6,416 Ω (heater/VOC ramp, normal). Splash env gate fired on real data (`gates satisfied (first AP + first BME680 read)`); home-screen readout live/green (owner visually confirmed).
- **GPS live:** module seated on P5 — 1 Hz NMEA streaming, `ok=192 bad=0`, UTC incrementing 094309→094329, sats=0 (indoors, no fix = acceptable pass; fix expected outdoors).
- Coexistence clean: heap flat ~8.37 MB, drops=0, hwm sensors 1556/3072, scan 2528/3072.

**Repo hygiene overhaul (owner-directed, 2026-08-09):**
- `KIMI.md`, `ROADMAP.md`, `PROGRESS.md` are now **local-only**: excluded via `.git/info/exclude`, removed from git tracking and from ALL commit history (git-filter-repo), force-pushed `d429a97...b54aa16`.
- All `KIMI.md §…` / `ROADMAP §…` references scrubbed from code comments (current tree + history); README file tree cleaned; AI-flavored commit messages rewritten. Verified zero traces across every blob and message (`git grep` over all revs).
- **Consequence:** all pre-2026-08-09 commit SHAs recorded below are stale (history rewritten). New baseline: tip `b54aa16`. Pre-clean backup bundle kept locally at `preclean-backup.bundle` (never push it).
- **New workflow rule:** docs updates (this file, ROADMAP, KIMI) no longer produce commits. Commits are code-only; GitHub stays a clean human-looking portfolio.

---

### Milestone: Phase 1.4 Sensor Integration, Boot Splash & Clean-Boot Display — 🟡 CODE COMPLETE 2026-08-08 (superseded — verified 2026-08-09, see entry above)
**Status:** 🟡 **All firmware landed and verified as far as bench hardware allows: gated boot splash, artifact-free boot sequence, sensor task + env_queue live alongside the scan engine, 3-state env readout on the home screen, and both sensor-absence paths proven. Live BME680/GPS data is blocked on the physical layer: the I2C bus is fully silent despite reportedly-correct CN1 wiring, and the GPS module is not yet seated on P5.**

**On-device verification (COM7, 2026-08-08, multiple 25–60 s captures):**
- Compile time `Aug 8 2026 08:40:38` (Commandment VI passed). Note: incremental builds keep a stale `esp_app_format` descriptor timestamp — for incremental flashes the binary size/hash are the freshness witnesses; the descriptor refreshes on any sdkconfig change.
- Measured boot timeline: app start 362 ms → display init done 837 ms (incl. full-GRAM black clear) → `backlight on: first full frame drawn` 977 ms → splash gates satisfied ~11.3 s → fade to home. Owner eyeballed and approved the sequence (2026-08-08).
- **Boot artifacts eliminated:** (1) "rainbow snow" = ST7789 GRAM power-on randomness exposed by DISPON+backlight ahead of any pixel data — fixed by writing full-screen black to GRAM while the display is still off (KIMI.md §5.13). (2) White flash = backlight self-biases during ROM/bootloader + was fired on an unrendered panel — fixed by gating BL on `lv_display_flush_is_last()` (2.5 s fallback in ui_task), moving `board_init_gpio()` ahead of NVS, and disabling `CONFIG_SPIRAM_MEMTEST` (was costing ~1.8 s pre-app: boot-to-app 2.15 s → 0.36 s). Residual ~0.4 s pre-app window is hardware-bias only; permanent cure would be a pull-down on the BL gate.
- **Splash:** 12 hue-rotated waterfall bars (staggered infinite anims), title fade-in, pulsing "Initializing Sensors & Radio..."; gates = first AP in LRU pool AND first valid env read (auto-satisfied when BME680 absent); GPS lock deliberately never gates; 100 ms gate timer; flags cross tasks as volatile bools per §10.2.
- **Task coexistence:** sensor_task (prio 3, 3072 B) + wifi_scan_task run clean together — scan cycles ~14.7–24.4 s unchanged, heap flat ~8.37 MB, hwm sensors 1592/3072 + scan 2528/3072, zero WDT/drops/evictions across captures.
- **Env readout on home screen:** `ui_hello_post_env()` spinlock handoff from main_task + 500 ms lv_timer render in ui_task context; OFFLINE (red) / waiting (amber) / live (green) states. Offline path verified on-device; live path pending sensor.
- **I2C bring-up diagnostic:** boot-time full bus scan (0x08–0x77, address-phase probes only) in `sensors.cpp`. Currently reports total silence with the module reportedly attached — live suspects: floating CSB (forces SPI mode), floating SDO (undefined address), CN1 JST contact, or missing 3.3 V at the module.
- **BME680 absent path:** `no BME680 on CN1` → env channel disabled, splash env-gate auto-satisfies — graceful degrade verified.
- **GPS absent path:** LP-UART RX provably receives (floating-line noise `FF 00 FE …` signature), parser provably rejects garbage (ok=0 bad=0, no false positives), bounded first-4-chunks hex/ASCII dump in `gps.cpp` documents the evidence. Module not yet seated on P5.

**Fixes found only through on-device testing:**
1. **ST7789 GRAM must be cleared before DISPON** — backlight-after-DISPON prevents white flash but not GRAM snow (KIMI.md §5.13).
2. **Backlight belongs on the first finished frame** (`lv_display_flush_is_last`), and `CONFIG_SPIRAM_MEMTEST` was 1.8 s of the boot budget (§5.13).
3. **LP-UART clock config is `lp_source_clk`** (not `source_clk`); GPIO 4/5 are fixed LP-IO; scaffold `SSP_GPS_UART` corrected `UART_NUM_1` → `LP_UART_NUM_0` (§5.12).
4. **newlib defines `LINE_MAX`** in `<limits.h>` — NMEA buffer constant renamed `NMEA_LINE_MAX`.
5. **strtok collapses empty fields** — hand-rolled splitter preserves `,,` in no-fix NMEA sentences (without it, no-fix reporting silently dies).

**Code landed:**
- `main/sensors/bme680.{h,cpp}` — register-level driver (new I2C master API): probe 0x76→0x77, chip-id 0xD0==0x61, 42-byte calibration, Bosch BME68x integer compensation ported 1:1, forced mode, heater 300 °C/150 ms, datasheet Table 16 gas-resistance lookup. Datasheet vendored: `docs/datasheets/bst-bme680-ds001.pdf`.
- `main/sensors/gps.{h,cpp}` — `LP_UART_NUM_0` 9600 8N1 on GPIO 4/5; talker-agnostic GGA/RMC parser with XOR checksum; rx_bytes canary + bounded hex/ASCII dump diagnostic.
- `main/sensors/sensors.{h,cpp}` — sensor_task (5 s env period, 100 ms bounded GPS poll), env_queue (depth 8), I2C bus with internal pull-ups, boot bus scan, graceful BME680 absence.
- `main/ui/ui_splash.{h,cpp}` — boot splash mask with readiness gates and 400 ms fade handoff.
- `main/ui/ui_hello.{h,cpp}` — create/load split (splash owns the display first) + 3-state live env readout.
- `main/ui/lvgl_port.cpp` — backlight first-frame gating + 2.5 s fallback.
- `main/display/display.cpp` — full-GRAM black clear before DISPON; BL no longer lit in `display_init`.
- `main/main.cpp` — `board_init_gpio()` first (earliest BL clamp); queue-set event loop posts env snapshots to the UI.
- `sdkconfig` — `CONFIG_SPIRAM_MEMTEST` disabled (−1.8 s boot).
- `KIMI.md` §5.12 (LP-UART truths), §5.13 (clean-boot trilogy).

**Build metrics:** `SiteSurvey-Pro.bin` = 0x146ba0 (1.27 MB), 36% of factory partition free. Zero errors/warnings in app code.

**Pending hardware verification (owner bench):**
- **BME680:** wiring per CN1 confirmed by owner; bus still silent. Next checks: CSB tied HIGH (I2C mode), SDO tied to GND/VCC (never floating), 3.3 V measurable at module VCC–GND, JST fully seated. Boot scan prints `i2c scan: device ACKs at 0x..` the moment the bus hears anything. **Update (commit `203a2f7`):** pre-scan line-level check splits the suspect list at boot — both lines idle HIGH + silent sweep = module-side (CSB/SDO/dead part); any line stuck LOW = CN1 wiring (short/crossed crimps). **On-device result (COM7, compile time Aug 8 10:45:15):** `i2c lines idle HIGH (SDA=1 SCL=1)` + sweep silent across 0x08–0x77 → CN1 wiring exonerated, fault is module-side. Bench order: (1) tie CSB to VCC and re-capture; (2) tie SDO to GND (address 0x76) and re-capture; (3) re-test the module on the project it came from.
- **GPS:** seat NM-ATGM336H on P5. Expect continuous 1 Hz NMEA from power-on (ok>0, sats/utc reporting) even indoors with no fix — no-fix is an acceptable pass.

---

### Milestone: Phase 1.3 Core Scanning Engine — ✅ VERIFIED ON-DEVICE 2026-08-07
**Status:** ✅ **Dual-band active scan engine live: 2.4 GHz + 5 GHz APs captured from a static LRU pool, heap flat across cycles, zero watchdogs.**

**On-device verification (COM7, 2026-08-07, 90 s capture):**
- Boot log clean: compile time `Aug 7 2026 05:10:17` (Commandment VI passed), Wi-Fi driver up on C5 (`band mode:0x3` dual-band), country `US` applied to both bands (`wifi_5g_channel_mask=0x1ffffffe`).
- Scan loop runs every ~14.7 s (2.4 G scan + 5 G scan + 5 s dwell): cycle counters `2.4G=12 5G=6` → `2.4G=11 5G=10` → `2.4G=15 5G=14`.
- **5 GHz regulatory check passed:** channels 36+ scan correctly — ch44 APs (WeaverOil, xfinitywifi, hidden ESSIDs) captured with WPA2/WPA3/Open classification.
- Static pool working: occupancy grew 13 → 22 / 64 as new BSSIDs appeared, `evict=0`, `drops=0`. Per-AP log lines carry SSID/BSSID/band/ch/RSSI/severity/security; severity chars match ROADMAP §5 boundaries (−50=S, −58=M, −85=W, −86=X).
- **Heap flat at 8,381,660 bytes across all cycles** — no leak in the scan path. Free heap at app start: 8,531,164 bytes.
- Stack: `wifi_scan_task` high-water mark 2,372 B free of 3,072 B — 3 KB budget holds (~77% headroom).

**Fix found only through on-device testing:**
1. **`esp_wifi_set_band_mode()` must come after `esp_wifi_start()`** — calling it pre-start returns `ESP_ERR_WIFI_NOT_STARTED` and boot-looped the board. Init order now: `esp_wifi_init` → `set_mode(STA)` → `set_country_code("US")` → `esp_wifi_start` → `set_band_mode(AUTO)`. Recorded as KIMI.md §5.11.

**Code landed:**
- `main/scan_engine/scan_engine.{h,cpp}` — scan-only Wi-Fi station (never connects); alternating blocking all-channel active scans per band via `esp_wifi_set_band()`; 64-entry static pool keyed on BSSID with LRU eviction; `ScanResult_t` posts to `scan_queue` (depth 16); RSSI severity tiers hardcoded per ROADMAP §5; full `wifi_auth_mode_t` → string map. Zero heap in the hot path (static scan buffer, static pool).
- `main.cpp` — wires `scan_engine_init()` / `scan_engine_start_task()` alongside `ui_task`; app_main tail is now the §10.2 main_task event loop draining `scan_queue` into per-AP log lines (1.3 acceptance evidence; Live Wi-Fi List screen consumes the queue next phase).
- `main/CMakeLists.txt` — +`scan_engine/scan_engine.cpp`, +`esp_event` `esp_netif` (IDF v6 split components).

**Build metrics:** `SiteSurvey-Pro.bin` = 0x1389f0 (1.22 MB), 39% of factory partition free. Zero errors/warnings in app code.

---

### Milestone: Phase 1.2 Display & LVGL UI Scaffold — ✅ VERIFIED ON-DEVICE 2026-08-07
**Status:** ✅ **ST7789 + XPT2046 + LVGL 9.5 pipeline verified on hardware. Display renders correctly; touch calibration pixel-perfect at all 5 test points.**

**On-device verification (COM7, 2026-08-07):**
- Boot log clean: compile time current (Commandment VI passed), 8MB PSRAM memory-test OK, custom partition table loaded, no errors/watchdogs.
- `display: ST7789 up: 320x240 landscape, SPI2 @ 20 MHz` / `touch: XPT2046 up @ 2500 kHz` / `lvgl_port: 2x15 KB PSRAM buffers`. Free heap at app start: 8,602,560 bytes.
- Hello screen renders: black bg, white title, grey hint, green touch readout.

**Fixes found only through on-device testing:**
1. **Color inversion**: `esp_lcd_panel_invert_color(panel, true)` was wrong for this panel — native polarity is correct (invert=false).
2. **RGB565 byte order**: ST7789 expects MSB-first; LVGL renders little-endian. Fixed with `lv_display_set_color_format(LV_COLOR_FORMAT_RGB565_SWAPPED)` (pre-swapped render, zero-cost flush). Symptom was rainbow fringing in anti-aliased glyph edges.
3. **LVGL root screen scrolls by default** — an overflowing label enabled horizontal panning. Screens must call `lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE)` unless scrolling is intended.
4. **Touch channels swapped**: the resistive film is rotated 90° vs XPT2046 command naming. Raw Y (0x90) tracks horizontal, raw X (0xD0) tracks vertical. The Bruce calibration array assumed unswapped channels and was unusable.

**Measured calibration (5-point raw ADC capture via `ui_calib.cpp` test bed + `touch_read_raw()`):**
- raw Y: 3450 (left) → 516 (right); raw X: 3399 (top) → 713 (bottom). Anchored at 24 px inset dots.
- Linear model validated against dead center before flashing (predicted 160,119 vs dot 160,120).
- Final on-device result: **TL(24,24) TR(296,24) BL(24,216) BR(296,216) C(160,120) — exact at all 5 points.**
- Constants live in `board_pins.h` (`SSP_TOUCH_RAW*/SSP_TOUCH_ANCHOR_*`), mapping in `touch.cpp::map_anchored()`. Bruce array retained only as a historical comment.

**Environment established (Windows host, first successful builds):**
- ESP-IDF v6.0.1 located at `C:\esp\v6.0.1\esp-idf`; Python venv `idf6.0_py3.14_env` (built from `C:\Python314`).
- Git Bash `export.sh` is broken on this machine (temp-path backslash mangling); `export.ps1` works but requires `IDF_PYTHON_ENV_PATH` because export scripts probe for a py3.12 venv that does not exist.
- Canonical activation captured in `tools/idfenv.ps1` — all builds run through it. Serial boot capture helper: `tools/capture_boot.ps1`.
- IDF v6 breaking change handled: legacy `driver` component split into `esp_driver_*`; main now requires `esp_driver_gpio/spi/uart/i2c`, `esp_lcd`, `esp_timer`, `lvgl__lvgl`.
- `CONFIG_SPIRAM_MODE_OCT` removed from `sdkconfig.defaults` — ESP32-C5 PSRAM is QUAD-only in IDF v6 (symbol was silently ignored).

**Code landed:**
- `main/display/display.{h,cpp}` — ST7789 via `esp_lcd` on SPI2 @ 20 MHz; 120 ms pre-init delay; invert off (native polarity); rotation 3 (swap_xy + mirror_y); BL (GPIO25) HIGH only after DISPON.
- `main/touch/touch.{h,cpp}` — XPT2046 as second SPI2 device @ 2.5 MHz, CS=GPIO1; Z1 press detection; 3-sample averaging; measured anchored calibration; `touch_read_raw()` diagnostic passthrough. Pure driver — no LVGL types.
- `main/ui/lvgl_port.{h,cpp}` — LVGL 9.5 glue: 2×15 KB draw buffers in PSRAM (`MALLOC_CAP_SPIRAM`), RGB565 pre-swapped render, flush → `esp_lcd_panel_draw_bitmap`, pointer indev → `touch_read`, 1 ms `esp_timer` tick, `ui_task` (4 KB) owns all LVGL calls at 10 ms cadence.
- `main/ui/ui_hello.{h,cpp}` — dark scaffold screen: centered title, hint, live touch-coordinate readout. Objects created once at init.
- `main/ui/ui_calib.{h,cpp}` — 5-point calibration test bed (retained as diagnostic for future recalibration).
- `main/idf_component.yml` — `lvgl/lvgl ^9` (resolved 9.5.0). `sdkconfig.defaults` +`CONFIG_LV_COLOR_DEPTH_16`, `CONFIG_LV_FONT_MONTSERRAT_20`.
- `main.cpp` — boots NVS → board GPIO → `lvgl_port_init()` → hello screen → `ui_task`.
- `board_pins.h` made self-contained (includes its own `driver/gpio.h`, UART/I2C types).

**Build metrics:** `SiteSurvey-Pro.bin` ≈ 569 KB, 73% of factory partition free. Zero errors/warnings in app code.

**Verification photos:** `docs/assets/phase1_2_hello_screen.jpg` (inverted-color symptom), `docs/assets/phase1_2_color_check.jpg` (byte-swap fringing symptom).

---

### Milestone: Phase 0 Product Discovery — 2026-08-06
**Status:** ✅ **Requirements interview complete. Product specification validated and recorded in KIMI.md, ROADMAP.md, and PROGRESS.md.**

**Key decisions from user interview:**
- **User:** Field engineer / wireless security auditor / pen-tester.
- **Mission:** Wardriving + RF site survey with GPS-tagged RSSI visualization.
- **Scan modes:** Passive + active, hidden SSID detection, concurrent 2.4/5 GHz Wi-Fi + BLE.
- **UI:** 7 screens, landscape 320×240, dark high-contrast theme, top/bottom tab bar navigation.
- **Screens:** Live Wi-Fi List, BLE Device List, Channel Spectrum, GPS Status, Environmental Dashboard, Alert Log, Settings/Data Manager.
- **Logging:** CSV (WiGLE-compatible) + KML (Google Earth), session-based naming `survey_YYYYMMDD_HHMMSS`, batch-write every 3–5 s, GPS UTC timestamps.
- **Auto-start:** Scanning auto-starts on boot; pause/resume toggles on touchscreen.
- **Alerts:** Target-based (BSSID/SSID match, RSSI spikes) with RGB LED trigger.

**Specification recorded:**
- `KIMI.md` §11 — Phase 0 Product Specification (all four topics)
- `ROADMAP.md` — Phase 0 marked closed; 7 screens documented; CSV/KML formats specified
- `PROGRESS.md` — This entry

---

### Milestone: ESP-IDF Project Scaffold — 2026-08-06
**Status:** ✅ **ESP-IDF project structure created; `board_pins.h` populated with exact NM-CYD-C5 pin macros; custom partition table for 16MB flash; committed and pushed to origin/main.** | 🔧 Tree code at `c771fd3`

**Scaffolding completed (2026-08-06):**
- `CMakeLists.txt` — Root project file with `IDF_TARGET=esp32c5`.
- `sdkconfig.defaults` — Mandatory flags: QIO flash @ 80MHz, 16MB flash size, 8MB Octal PSRAM, custom partition table.
- `partitions/partitions.csv` — Custom 16MB layout: factory (2MB), storage/Spiffs (4MB), OTA_0/OTA_1 (2MB each), NVS, coredump.
- `main/CMakeLists.txt` — Component registration with `driver`, `esp_wifi`, `nvs_flash` requirements.
- `main/include/board_pins.h` — Complete pin macro definitions:
  - Shared SPI: SCK=GPIO6, MISO=GPIO2, MOSI=GPIO7
  - ST7789: CS=GPIO23, DC=GPIO24, BL=GPIO25, RST=chip-level
  - XPT2046: CS=GPIO1, SPI @ 2.5 MHz, calibration constants
  - SD Card: CS=GPIO10
  - GPS: UART1, RX=GPIO4, TX=GPIO5, 9600 baud
  - BME680: I2C0, SCL=GPIO8, SDA=GPIO9, addresses 0x76/0x77
  - WS2812: GPIO27
  - Deepsleep wake: GPIO0
- `main/main.cpp` — C++ entry point with `board_init_gpio()`; drives all SPI CS lines HIGH and BL LOW before any bus transactions.
- `README.md` — Professional project README with system architecture diagram, task queue flow, pinout table, and build instructions.
- `KIMI.md` — Updated with portfolio-grade coding standards: Zero AI Signatures, LVGL/FreeRTOS architecture rules, minimal dependency tree.

**Commit:** `c771fd3` — `feat: scaffold initial ESP-IDF workspace and board pinouts`
**Remote:** `https://github.com/bigjoe420/SiteSurvey-Pro` (origin/main)

**Current tree state:**
- Code = 11 files committed. Build unverified (next step: `idf.py build`).
- Documentation = `KIMI.md`, `ROADMAP.md`, `PROGRESS.md`, `README.md` maintained.
- Git = `main` branch tracking `origin/main`.
**Status:** ✅ **ESP-IDF project structure created; `board_pins.h` populated with exact NM-CYD-C5 pin macros; custom partition table for 16MB flash; first build pending.** | 🔧 Tree code at `N/A (uncommitted)`

**Scaffolding completed (2026-08-06):**
- `CMakeLists.txt` — Root project file with `IDF_TARGET=esp32c5`.
- `sdkconfig.defaults` — Mandatory flags: QIO flash @ 80MHz, 16MB flash size, 8MB Octal PSRAM, custom partition table.
- `partitions/partitions.csv` — Custom 16MB layout: factory (2MB), storage/Spiffs (4MB), OTA_0/OTA_1 (2MB each), NVS, coredump.
- `main/CMakeLists.txt` — Component registration with `driver`, `esp_wifi`, `nvs_flash` requirements.
- `main/include/board_pins.h` — Complete pin macro definitions:
  - Shared SPI: SCK=GPIO6, MISO=GPIO2, MOSI=GPIO7
  - ST7789: CS=GPIO23, DC=GPIO24, BL=GPIO25, RST=chip-level
  - XPT2046: CS=GPIO1, SPI @ 2.5 MHz, calibration constants
  - SD Card: CS=GPIO10
  - GPS: UART1, RX=GPIO4, TX=GPIO5, 9600 baud
  - BME680: I2C0, SCL=GPIO8, SDA=GPIO9, addresses 0x76/0x77
  - WS2812: GPIO27
  - Deepsleep wake: GPIO0
- `main/main.cpp` — C++ entry point with `board_init_gpio()` that drives all SPI CS lines HIGH and BL LOW before any bus transactions.

**Current tree state:**
- Code = `main.cpp`, `board_pins.h`, `CMakeLists.txt`, `main/CMakeLists.txt` created.
- Documentation = `KIMI.md`, `ROADMAP.md`, `PROGRESS.md` maintained.
- Build status = Unbuilt. Next: `idf.py set-target esp32c5 && idf.py build`.

### Milestone: Project Initialization — 2026-08-06
**Status:** ✅ **Workspace scaffolded; templates applied; hardware corrected to NM-CYD-C5; exact pinouts extracted from manufacturer repository; Project-Isolated Docs rule established.** | 🔧 Tree code at `N/A (initial commit pending)`

**Project foundation laid (2026-08-06):**
- Created `D:\SiteSurvey Pro` workspace.
- Created `D:\SiteSurvey Pro\docs\` directory for project-isolated hardware documentation.
- Generated `KIMI.md`, `ROADMAP.md`, and `PROGRESS.md` from AI Project Template.
- **Hardware corrected:** NM-CYD-C5 (ESP32-C5-WROOM-1 RISC-V @ 240 MHz), 16MB Flash, 8MB PSRAM.
- **Display confirmed:** ST7789, 320×240 landscape, SPI @ 20 MHz.
- **Touch confirmed:** XPT2046 **resistive** (not capacitive), shared SPI @ 2.5 MHz, CS=GPIO1.
- **GPS confirmed:** UART at GPIO 4 (RX) / GPIO 5 (TX), 9600 baud, NMEA.
- **I2C confirmed:** CN1 at GPIO 8 (SCL) / GPIO 9 (SDA) for BME680.
- **SD Card confirmed:** SPI shared bus, CS=GPIO10.
- **RGB LED confirmed:** WS2812 on GPIO 27 (GRB order).
- **Touch calibration captured:** `{225, 3413, 403, 3334, 1}` from Bruce firmware `interface.cpp`.
- **Project-Isolated Docs rule added:** All hardware docs must live in `D:\SiteSurvey Pro\docs\`. All references to external hardware folders removed.
- Defined non-negotiable rules: Local-First, Project-Isolated Docs, `sdkconfig` safety, stale-build detection, append-only logs.
- Phase 1 roadmap defined: BSP Scaffold → Display/LVGL/XPT2046 → Core Scanning Engine → Sensor Integration.

**Manufacturer repository audited:**
- Repo: `https://github.com/RockBase-iot/NM-CYD-C5`
- Key files read: `README.md`, `Demos/Platformio/pinouts/nm-cyd-c5.h`, `Demos/Platformio/nm-cyd-c5/pins_arduino.h`, `Demos/Platformio/nm-cyd-c5/connections.md`, `Demos/Platformio/nm-cyd-c5/interface.cpp`, `Demos/Platformio/nm-cyd-c5/platformio.ini`
- Pinout verified across multiple files for consistency.

**Current tree state:**
- Code = `N/A` (no source files yet).
- Documentation = `KIMI.md`, `ROADMAP.md`, `PROGRESS.md` initialized.
- `docs/` directory created for hardware documentation.
- Hardware = Pinout documented. Awaiting on-device verification.

---

## 2. Bench Metrics

| Date | Build SHA | Flash Size | Free Heap | Scan Rate | Notes |
|------|-----------|------------|-----------|-----------|-------|
| 2026-09-12 | picker build | not re-measured (UI + export path only) | internal DMA 77.6 KB at app start | ~15-21 s dual-band cycle | **Session export picker** — KML/Report buttons convert the chosen session via `kml_export_path`/`report_export_path`; `csv_list_sessions()` scans SD (48 cap, newest-first, active flagged from pointer file); Settings modal follows the OTA file-list pattern. Boot VERIFIED clean (SD up + self-test OK, POWERON, no faults). Finger test of the picker pending owner. |
| 2026-09-12 | `3da7e7c` | not re-measured (palette code only) | not re-measured | ~15-21 s dual-band cycle | **Outdoor mode removed per owner decision.** Light theme had white-on-white regressions (53 hardcoded `lv_color_white()` labels theme-driven in `c5bb635` first), owner said delete the option outright. Light palette + Settings toggle + power_mgr flag excised (`3da7e7c`); dark UI permanent, orphaned NVS key never read. Boot VERIFIED clean on-device (compile stamp Sep 12 03:06:34), zero faults. |
| 2026-09-12 | `e972e4c` | 0x1d4280 (1.83 MB) | internal DMA 4008 B after all tasks; ~8.04 MB total | ~15-21 s dual-band cycle | **Scroll root cause PROVEN via 50 Hz SD touch capture.** (a) touch map clamped at anchors -> 24 px dead bands at edges (upward drags died at bottom edge); (b) XPT2046 press-bounce + SPI-contention spikes 86-160 px yanked scroll. Fix: edge extrapolation + >72 px spike rejection w/ 3-sample persistence accept. Post-fix capture clean. Also: outdoor = true light theme (was too subtle to see), power-off button = 1 s hold (scroll accident), GPS replacement module VERIFIED (q=2 3-6 sats indoors, NMEA clean). |
| 2026-09-11 | `86f6a11` | 0x1d41c0 (1.83 MB) | internal DMA 4008 B after all tasks; ~8.04 MB total | ~15-21 s dual-band cycle | **Scroll-feel tuning VERIFIED clean boot.** Indev read period 10->20 ms (matches 50 Hz sampler, kills move/0/move/0 delta bursts = jumpy) + `lv_indev_set_scroll_throw` 25% (default 10%, calmer flick glide = fast). Feel itself pending owner finger-test. Gotcha: ninja skips `esp_app_desc` on incremental builds — delete its .obj for an honest compile stamp. |
| 2026-09-10 | `e6dfb63` | 0x1d44b0 (1.83 MB) | internal DMA 8.3 KB after all tasks; ~8.04 MB total | ~15–21 s dual-band cycle | **On-device scan report export VERIFIED on-device + two deep data-path bugs fixed.** Settings → Data export → "Export Report" writes `<session>.txt` (observations, 2.4G/5G split, top-5 channels w/ avg RSSI, strongest/weakest 5 APs, security breakdown, GPS coverage) beside the KML button; shared `session_csv` module (comma-safe parser, PSRAM dedup table, integer-only deg7 formatting). Bug 1: session CSV rows were ALL empty — `session_logger_log_ap` never copied AP/GPS data into queued entries (fill restored). Bug 2: CPU_LOCKUP reboots — **NVS access (even read-only) from PSRAM-stack tasks locks the CPU**; active session now tracked via `/sdcard/current_session` pointer file written by the logger, `csv_find_active()` never touches NVS. Also: no-RTC session selection unreliable by name/mtime (FAT can't represent pre-1980 dates) → pointer file; float printf purged from session write path (mprec stack risk); SD bus 10 MHz for write margin. Bench: live session → 9 unique APs, 0 bad rows; clean boot, zero faults. |
| 2026-09-10 | `04000e6` | 0x1d1430+ (1.82 MB) | internal DMA 8.6 KB after all tasks; ~8.04 MB total | ~15–21 s dual-band cycle | **KML export for Google Earth VERIFIED on-device.** Settings → "Google Earth export" → Export KML converts newest session CSV → .kml (dedup by BSSID/strongest RSSI, no-fix rows skipped+counted, RSSI-tiered icon colors, XML-escaped, comma-safe CSV parse). Bench: synthetic torture CSV → 2 placemarks, 1 no-fix, 1 truncated row rejected. **Also fixed session durability found in benching: every survey CSV was 0 bytes** — FATFS dir entry only updates on f_sync/f_close (now fsync after header+flush), and 1970-clock name collisions truncated prior sessions (now numeric suffix bump). Removed dup overwrite block that broke cached-GPS logging. DMA headroom IMPROVED 6.6→8.6 KB. |
| 2026-09-10 | `6856aeb` | 0x1d1430 (1.82 MB) | internal DMA 6.6 KB after all tasks; ~8.04 MB total | ~15–21 s dual-band cycle | **Settings scroll momentum restored + Power Off deep-sleep instant-wake fix — both VERIFIED on-device by user.** Root causes: momentum stripped with elastic in `7a70be3` (drag went 1:1, died on release); power-off configured EXT1 wake on GPIO0 without RTC pull-up / RTC_PERIPH power → pin floated low → ANY_LOW fired instantly → reboot loop. Fix: `rtc_gpio_pullup_en` + `esp_sleep_pd_config(RTC_PERIPH, ON)` + error check + 50 ms settle; "Sleeping - press BOOT to wake" hint 1.5 s before sleep (one-shot LVGL timer). Bench-verified via temp auto-off harness: 31 s UART silence in deep sleep, no wake. Follows NVS/PSRAM-stack crash fix `f48e803` (flash_broker). Clean boot capture, zero faults. App version 0.9.0. |
| 2026-08-24 | `d019cca`+ | 0x15f420 (1.40 MB) | ~8.32 MB steady / 8,487,708 at app start | ~15–21 s dual-band cycle | **Env overlay on Wi-Fi screen:** BME680 temp/humidity/pressure readout in top-right of SCR_WIFI. Build 0x15f420, 31% free. |
|------|-----------|------------|-----------|-----------|-------|
| 2026-08-24 | `d019cca`+ | 0x15f160 (1.40 MB) | ~8.32 MB steady / 8,487,772 at app start | ~15–21 s dual-band cycle | **Touch fix + SD operational:** Spectrum bar `LV_ANIM_ON` → `LV_ANIM_OFF` + change guards. SD mount + session logger now working (was blocked by `0x101` RAM pressure). Clean 40 s boot capture, zero faults. |
| 2026-08-24 | `d019cca`+ | 0x15f050 (1.40 MB) | ~8.33 MB steady / 8,329,080 at app start | ~15–21 s dual-band cycle | **Session CSV logger:** `survey_YYYYMMDD_HHMMSS.csv` format, 16-entry RAM buffer, GPS-tagged. `CONFIG_FATFS_LONG_NAMES=y`. SD mount blocked by known `0x101` RAM pressure (2026-08-23). Build 0x15f050, 31% free. |
|------|-----------|------------|-----------|-----------|-------|
| 2026-08-24 | `d019cca`+ | 0x15f050 (1.40 MB) | ~8.33 MB steady / 8,329,080 at app start | ~15–21 s dual-band cycle | **Session CSV logger:** `survey_YYYYMMDD_HHMMSS.csv` format, 16-entry RAM buffer, GPS-tagged. `CONFIG_FATFS_LONG_NAMES=y`. SD mount blocked by known `0x101` RAM pressure (2026-08-23). Build 0x15f050, 31% free. |
| 2026-08-24 | `d019cca`+ | 0x15d560 (1.39 MB) | ~8.33 MB steady / 8,329,812 at app start | ~15–21 s dual-band cycle | **Channel Spectrum screen (SCR_SPECTRUM):** 11× 2.4G + 13× 5G channel bars with RSSI-tier colors, total/busiest footer. Three-button home (Wi-Fi / Spectrum / Environment). Clean 40 s boot capture, zero faults. |
| 2026-08-10 | `011e1d2`+ | 0x15c4c0 (1.54 MB) | ~8.33 MB steady / 8,494,988 at app start | ~15–21 s dual-band cycle | **UI polish audit round:** adaptive delay reverted → fixed 10 ms; tab button styling moved after `lv_tabview_add_tab()`; row backgrounds opaque black; list pad_top 6 px. Clean 40 s boot capture, zero faults, compile time `Aug 10 2026 12:44:17`. |
|------|-----------|------------|-----------|-----------|-------|
| 2026-08-10 | `011e1d2`+ | 0x15c4c0 (1.54 MB) | ~8.33 MB steady / 8,494,988 at app start | ~15–21 s dual-band cycle | **UI polish audit round:** adaptive delay reverted → fixed 10 ms; tab button styling moved after `lv_tabview_add_tab()`; row backgrounds opaque black; list pad_top 6 px. Clean 40 s boot capture, zero faults, compile time `Aug 10 2026 12:44:17`. |
| 2026-08-10 | `33cae00` | 0x146d30 (1.28 MB) | — | — | Env readout in Fahrenheit on home screen (log stays °C). Flash digest verified on-device; app descriptor timestamp stale on incremental build (known gotcha). |
|------|-----------|------------|-----------|-----------|-------|
| 2026-08-10 | `33cae00` | 0x146d30 (1.28 MB) | — | — | Env readout in Fahrenheit on home screen (log stays °C). Flash digest verified on-device; app descriptor timestamp stale on incremental build (known gotcha). |
| 2026-08-09 | `b54aa16` | 0x146d10 (1.28 MB) | 8,369,xxx steady / ~8,526,524 at app start | ~15–21 s dual-band cycle | **Phase 1.4 VERIFIED:** BME680 live @0x76 (20.2 °C / 54 %RH / 101.6 kPa / gas ramping 5.2k→6.4k Ω), GPS streaming 1 Hz NMEA (ok=192 bad=0, no fix indoors), splash env gate on real data, home readout green. Same day: repo sanitized (docs purged from history, force-push); **all earlier SHAs below are stale** (pre-rewrite). |
| 2026-08-08 | `c4b02b0` ⚠ stale SHA | 0x146ba0 (1.27 MB) | 8,369,9xx steady / 8,526,524 at app start | ~14.7–24.4 s dual-band cycle | Phase 1.4 sensors+splash+boot-polish; BL gated on first frame @977 ms; boot-to-app 0.36 s (memtest off); hwm scan 2528/3072, sensors 1592/3072. |
| 2026-08-07 | `f64e0c8` | 0x1389f0 (1.22 MB) | 8,381,660 steady / 8,531,164 at app start | ~14.7 s dual-band cycle | Phase 1.3 scan engine; pool 22/64, evict=0, drops=0, hwm 2372/3072, heap flat across cycles. |
| 2026-08-06 | `e395f33` | 0x8b1d0 (569 KB) | — | — | Phase 1.2 UI scaffold; builds clean on IDF v6.0.1; flash pending hardware. |
| 2026-08-06 | `N/A` | — | — | — | Project initialized; no builds yet. |

---

## 3. Audit Log

| Date | Auditor | Scope | Findings | Action |
|------|---------|-------|----------|--------|
| 2026-08-06 | Kimi | Project setup | Templates applied. Hardware spec corrected to NM-CYD-C5. Exact pinouts extracted from manufacturer repo. Project-Isolated Docs rule established. All external hardware folder references removed. | Begin Phase 1: initialize ESP-IDF project, create `board_pins.h` from extracted pinout, verify clean build. |
| 2026-08-06 | Kimi | Git commit & push | 11 files committed as `c771fd3` — `feat: scaffold initial ESP-IDF workspace and board pinouts`. Pushed to `origin/main`. README.md and KIMI.md updated with portfolio-grade standards. | Next: verify first `idf.py build` succeeds on target hardware. |

---

*This document is append-only. Last updated: 2026-09-11 — CRASH FIX VERIFIED on-device: settings stepper buttons (Auto power off +/-, and any config save) reset the device because `flash_broker_init()` had been left commented out in main.cpp since a splash bisect — every `flash_broker_exec` fell back to an inline NVS commit on the PSRAM-stack ui_task (cache-down during flash write = CPU lockup). Re-enabled the broker; software-fired the exact auto_off_plus_cb path over serial (survived, NVS persisted), reset auto_off NVS to Off, then stripped the diag hook. Final build flashed + boot-verified clean (broker init ok, DMA free 3984 B after all tasks — thin but proven). Root cause also explains the Sept 10 RSSI stepper crash — all steppers share this save path. Hard-won rule: NO flash/NVS access (reads included) from PSRAM-stack tasks — use SD or internal-stack brokers. Corollary: never leave a bisect comment/disablement un-reverted before committing.*

---

## 2026-09-11 — Phase 3: Outdoor high-contrast UI theme (commit e265bad) — VERIFIED on-device

**What:** All UI colors centralized behind `ui_theme` (new `main/ui/ui_theme.h/.cpp`) with two compile-time palettes: indoor (exact previous look) and outdoor (grays pushed toward white, cyan accent swapped for amber 0xFFD600, saturated tier colors). `main.cpp` selects the palette once at boot from the `power_mgr` outdoor flag (loaded from NVS on the main task — safe context) before any screen exists; screens read theme macros at creation. Settings → Power gains an "Outdoor mode" switch: saves via flash broker (same path as the other power settings) and restarts so every screen re-themes. Outdoor mode also changes backlight behavior in `power_mgr`: dim floor raised to 35 % and the backlight-off stage is skipped (stays glanceable in sunlight); applied immediately if already dimmed. RSSI tier tables (wifi/ble/spectrum/detail) and env gauge bands moved from static initializers to runtime fill — the palette is only known after boot. Home-screen gradient blocks, splash, and calib screen intentionally left as-is (saturated / transient / diagnostic).

**On-device verification (COM3, both toggle directions software-fired through a temp diag hook — reverted before commit, per the cd59934 corollary):**
- Enable path: broker NVS commit survived restart; second boot logged `power_mgr: init ok ... outdoor=on` + `UI theme: outdoor (high contrast)`; 95 s clean capture: `idle → dim (35%)` at 31.8 s and **no** `idle → off` at 2× timeout (never-off confirmed); zero faults.
- Disable path: `power_mgr_set_outdoor(false)` via broker → `esp_restart` (rst:0xc SW_CPU, expected) → `outdoor=off`, `UI theme: indoor`; indoor idle behavior unchanged (`dim (25%)` → `off` at 2×).
- Final clean build (diag removed) flashed; boot capture below, no panics/rst loops.

| Date | Build SHA | Flash Size | Free Heap | Scan Rate | Notes |
|------|-----------|------------|-----------|-----------|-------|
| 2026-09-11 | `e265bad` | 0x1d41b0 (1.83 MB) | internal DMA 4128 B after all tasks (largest block 4096 B — thin but above the ~4 KB floor); total 8.04 MB | ~15–21 s dual-band cycle | **Outdoor high-contrast theme VERIFIED on-device.** See entry above. Theme adds zero static RAM (palettes are const hex tables; THM_* macros convert at the call site). |

**Roadmap:** Phase 3 item 2 (outdoor theme) done. Remaining: GPS replacement module bring-up + outdoor field test (hardware — module still silent: `gps_rx=0 ok=0` on this boot, no power LED per user), OTA audit (UI exists).

---

## 2026-09-12/13 — Session: picker root cause FOUND (corrupt SD FAT) + Settings scroll regression under investigation

**Picker blank-list root cause: SD card FAT corruption, NOT firmware.**
- Owner's report "no sessions" reproduced with the `908d334` diagnostic build (distinguishes SD-absent vs no-files, logs scan count). Fresh boot always showed `SD up: SL32G` + self-test OK, so the mount path was fine.
- Added screen-visible scan breakdown (dir/named/err/empty counts via new `CsvScanStats`, `csv_last_scan_stats()`); owner reported `dir 0 named 0 err 0 empty 0` — `opendir()` succeeded but `readdir()` iterated ZERO entries, while targeted fopen/read/write of known files (self-test, session logger) still worked. Classic malformed-directory-entry signature.
- PC read the same card fine (134 entries visible) — Windows tolerates the bad entry, the device's FatFs bails on it. Read-only `chkdsk E:` confirmed: `\survey_19700101_000025_13.csv is cross-linked on allocation unit 133` + 2 lost chains (32 KB recoverable). **This is the FR_DISK_ERR write gremlin (open item 5) having left structural damage.**
- Full card backed up to `sessions-backup-20260912/` (134 files, ~1 MB) BEFORE any repair. `chkdsk E: /F` (run via `cmd //c "echo Y | chkdsk E: /F"` — Git Bash mangles `/F`): cross-link resolved by copying, 2 chains recovered into `FOUND.000`. Re-read clean.
- After re-seat + reboot: SD healthy, session flush clean (`flushed 6 entries to .../survey_19700101_000024_72.csv`), zero errors. Picker scan presumed fixed (same code path as `csv_find_latest` which the repair restored) — owner could not confirm because Settings scroll blocks reaching the picker. **Confirm with one tap in the next session.**
- Watch items seen on card afterwards: `current_session` 0 bytes (pointer write lost), one 0-byte session stub (`survey_19700101_002854.csv`) — write gremlin history; multiple power cycles tonight explain at least some.

**Settings scroll regression — INVESTIGATED, root cause NOT yet found.** Symptom (owner, both 09-12 morning and 09-13 night): on SCR_SETTINGS only, dragging scrolls a little then snaps/jumps back to top; reaching the bottom takes many swipes. Wi-Fi and BLE lists scroll perfectly (touch hardware + LVGL scroll engine exonerated). A/B test WITHOUT SD card in the device: still bad → SD write/SPI contention theory DISPROVEN.
- Ruled out by code inspection: panel scroll config identical to the Sep-11 verified build (ELASTIC off / MOMENTUM on / CLICKABLE, incl. the pre-existing duplicated init block); no periodic timers touching scroll; `refresh_ssid_list/bssid` fire only on user edit; alert engine does not poke settings UI; `ui_settings_set_visible(true)` fires once per navigation.
- **TEMP DIAG code added (UNCOMMITTED — remove before any commit, per the cd59934 corollary):** 50 Hz touch+scroll ring in `touch.cpp` (`tdbg_*`: PSRAM ring of tick/raw/mapped/pressed + mirrored `scroll_y`/`content_h`/`scrollable`), `touch_read_dbg()` refactor (single acquisition returns paired raw), scroll-state mirror timer (50 ms) + SD dump timer (30 s, dumps `/sdcard/touch_diag.csv`) in `ui_settings.cpp`, `sess_rescan`/picker-scan duration logs, picker empty-label shows scan breakdown.
- First field capture failed: 0-byte `touch_diag.csv` (created-but-empty = truncated, data never flushed — internal-heap stdio buffer suspicion or card pulled mid-write). Dump hardened: PSRAM `setvbuf`, write `.tmp` + `fsync` + `rename`, errno logging on every failure path.
- Verified on serial (card absent): dump timer fires on schedule, ring non-empty, only `fopen` fails (`No such file or directory`) — firmware side proven good.
- **Capture data waiting:** `/sdcard/touch_diag.csv` on the card (in the device; ring window = last 40 s, file persists across power cycles, refreshes every 30 s while a settings screen has been created). Next session: pull card, copy to `tools/`, correlate `scroll_y` resets against mapped-`my` jumps, tick gaps (stalls), `content_h` changes, `scrollable` flag flicker.

**Tree state at handoff:** HEAD `908d334`; working tree has UNCOMMITTED diagnostic changes in `main/touch/touch.{cpp,h}`, `main/ui/ui_settings.cpp`, `main/storage/session_csv.{cpp,h}` (picker diagnostics are valuable — consider committing a cleaned version minus the temp ring after the scroll bug is solved). Nothing pushed (per rules). Current flash on device: diag build, app stamp Sep 12 2026 23:35:02.

**Bench (this session's builds):** bin ~1.83 MB (unchanged class), boots clean, SD self-test OK, heap ~8.03 MB steady. GPS no-fix indoors (normal). Session logger flushing normally post-repair.

---

## 2026-09-13→18 — Session (recovered 2026-09-24 from archived wire log; this entry was never written at the time)

**Touch hardware bug: FIXED and verified** earlier in this conversation (tap failures; zero failures since).

**Settings scroll regression: ROOT CAUSE FOUND (2026-09-18 ~03:10 local).**
LVGL samples the finger once per screen redraw. Redraws during scrolling took ~60 ms at the old 20 MHz display SPI clock, so a fast swipe (~80 ms) was seen as 1–2 points and discarded; the "throw/snap back to top" was momentum computed from that garbage. Supporting evidence from the Sep-14 swipe captures: panel only scrolls at the *tail* of a swipe; momentum advances in ~80 ms chunks; 19 of 37 swipes ignored across the whole screen (button-absorb theory disproved).

**FIX: display SPI clock 20 MHz → 40 MHz** (halves redraw time → LVGL sees the finger 2–3× per swipe). Also repaired the build system after the project folder moved to `C:\Development\SiteSurvey Pro` (old `D:\` paths). Clean build succeeded on the new path; fix **flashed to device 2026-09-18 ~03:17**.

**STATUS AT RECOVERY (2026-09-24): fix is on the device but was NEVER owner-verified** — the session ended at the moment the owner was asked to run the finger test (Settings → swipe up/down ~20 s → back out → wait 10 s, with the two-ring capture open). Working tree still carries the uncommitted fix + temp diag code (tdbg_* ring, dump timers, touch_read_dbg) — must be stripped before any commit (cd59934 corollary).

**Recovered-from-archive lesson:** the Sep-14/18 sessions never updated this file; after the desktop app update wiped the chat list, the only record was the on-disk wire log. Write the handoff entry *during* the session, not after.

---

## 2026-09-25/26 — Session: SD session-logging FIXED + scroll performance audit (measured, physics-bound)

**Methodology:** temporary frame-time probe in `ui_task` (avg/worst `lv_timer_handler` cost, inter-call gap, flush count) + a self-driving test harness (`ui_settings_test_open`: auto-opens Settings 4 s after backlight and scrolls the panel 12 px/33 ms, reversing every 2 s) so scroll cost could be measured on the bench WITHOUT owner round-trips. All temp code stripped before commit (cd59934 corollary).

**SD session logging — was SILENTLY BROKEN, now FIXED.** Every `fopen` for a new survey session failed with `sdmmc_cmd: allocate_dma_buf: not enough mem, err=0x101`. Root cause: the app's own boot instrumentation showed internal DMA-capable RAM at **108 bytes free** after all tasks start (Wi-Fi + NimBLE eat ~90 KB of the C5's small internal RAM). The IDF v6.1 sdmmc protocol layer allocates a per-transaction DMA temp buffer from the heap for every unaligned sector read/write (i.e. all FATFS traffic). Fix in `sd_card.cpp`: pin one 2 KB `MALLOC_CAP_DMA` buffer at SD init (internal RAM is still ~68 KB free at that point) and hand it to the driver via `host.dma_aligned_buffer` with `unaligned_multi_block_rw_max_chunk_size = 4`. GOTCHA: the driver calls `heap_caps_get_allocated_size()` on that pointer and **asserts on non-heap pointers** — a static .bss buffer boot-loops with `assert failed: heap_caps_get_allocated_size`. Must be a real `heap_caps_aligned_alloc`. Verified: self-test OK, `session started`, zero errors.

**Companion SD fixes:** session fsync throttled 4 → every 16 flushes (`SYNC_EVERY_N_FLUSHES`); Settings-entry SD directory scan now cached 10 s (`SESS_SCAN_TTL_MS`) and deferred via `lv_async_call` so the screen paints before the FAT walk (the scan blocks `ui_task` for its whole duration on the display-shared SPI bus).

**Scroll audit — measured numbers (auto-scroll harness, 30 MHz display SPI):**
- Raw full-frame DMA: **46.5 ms/frame** (153.6 KB @ 30 MHz, 88% bus efficiency). This is the hard floor at 30 MHz.
- Settings continuous scroll: **92 ms/frame @ PSRAM 40 MHz, 77 ms @ PSRAM 80 MHz** (13 fps). Content-hidden test produced IDENTICAL numbers → not text/glyph cost.
- Contention model: per scroll frame the shared MSPI bus carries ~150 KB framebuffer writes + 150 KB DMA reads + XIP code fetch + glyph fetch. Frame time ≈ total bytes / effective bus bandwidth. Faster PSRAM directly helps → **shipped SPIRAM_SPEED_80M** (120M is unsupported with flash @ 80M DIO — build-time static assert).
- **LVGL RVV asm (`CONFIG_LV_DRAW_SW_ASM_RISCV_V`) is a no-op for RGB565** — LVGL 9.5 ships RVV blends for RGB888 only. Left off.
- Display SPI 40 MHz re-tested twice (queue 2, full-frame buffers, both PSRAM speeds): corruption returns (thin green bar across screen) — the shared-SPI wiring cannot hold 40 MHz. 30 MHz is the ceiling.
- `bus.max_transfer_sz` full-frame (150 KB) = boot loop + 150–260 ms frames. Keep the 24-row (15 KB) value.

**Where it landed (this commit):** PSRAM 80M, refresh period 16 ms, display 30 MHz, RVV off, SD pinned DMA buffer, sync/16, SD-scan TTL + async. Expected: home screen fluid; Settings continuous full-screen scroll ~13 fps (hardware floor ~21 fps even with perfect overlap). The 2026-09-18 "down-scroll perfect" report cannot be reproduced by any configuration tried — treat 13–21 fps as the physics of SW-rendered full-screen scroll on single-core C5 with PSRAM framebuffers over a shared MSPI bus.

**Owner-reported reset on first Settings tap (2026-09-26):** NOT reproduced on the bench (0 resets across ~10 min of captures). Prime suspect was the entry-time synchronous SD scan + first-paint storm; mitigations (TTL + async scan) shipped. Keep an eye; if it recurs, capture the backtrace immediately.

**Bench:** 1 boot / 40 s, SD self-test OK, `session started`, no asserts. Bin ~1.86 MB.

---

## 2026-09-26 (late) — Session: splash-freeze root cause + recovery + internal-DMA-RAM wall

**Splash freeze ROOT CAUSE (device was alive, UI dead):** boot ladder showed internal DMA-capable RAM at **0–8 bytes free** after init. The SPI master allocates a small internal DMA temp buffer PER TRANSACTION for every display command/params (`spicommon_dma_setup_priv_buffer`); with zero headroom every `tx_param`/`draw_bitmap` failed (`Failed to allocate priv TX buffer`) → `flush_cb` error path → splash frozen. The trigger was enabling **core-dump-to-flash** (~2–4 KB permanent internal-RAM cost). Compensating by trimming NimBLE transport buffers (ACL_FROM_LL 24→4, EVT 30→16, mSys 24→12) freed ~8 KB but **broke BLE entirely** (BLE_ERR_MEM_CAPACITY rc=519 at every scan start — same failure the original values were tuned to fix). Reverted both (commit afb0e0c); boot ladder back to ~330 B free — the config that ran stable all day.

**PSRAM 80 MHz officially exonerated-and-rejected:** faster on the bench (77 vs 92 ms/frame) but owner-verified UNSTABLE on this Rev v1.0 board (splash text flashing, ghosting, glow, Settings-tap reset). Back to 40 MHz.

**HARD WON FACTS (do not relearn):**
- ESP32-C5 internal SRAM (~95 KB usable) is fully consumed by Wi-Fi + NimBLE + stacks. ANY new feature with an internal-RAM cost (coredump, IRAM opts, new buffers) can silently kill the display SPI path. Check the `[DMA] after all tasks` ladder after ANY sdkconfig change.
- NimBLE transport buffer values are load-bearing for scanning. Do not trim.
- `esp_lcd` + `psram_dma_direct`: color data path is fine from PSRAM; the per-command internal allocs are the fragile part.
- LVGL RVV asm: RGB888 only, no-op for RGB565.
- Display SPI 40 MHz: corrupts on this wiring. 30 MHz ceiling. `max_transfer_sz` must stay 15 KB (full-frame size boot-loops).
- Settings-tap reset: STILL NOT CAUGHT. Coredump-to-flash is not viable (RAM cost). Must be caught over serial while tapping.

**Still open:** Settings-tap reset (intermittent, happened at 80M AND 40M PSRAM), scroll fps physics cap (~13–21 fps full-screen, hardware-scroll project is the only real fix).

---

## 2026-09-27 (early) — Session: Settings-tap reset ROOT CAUSE + fix (log flood blocking console TX)

**ROOT CAUSE of the "tap Settings → device reloads" saga (and very likely several freezes):** `main.cpp` app superloop logged **every BLE advertisement at INFO level** — ~8–9 `ESP_LOGI` lines/sec in busy RF environments (~2,500 lines per 5 min, observed in capture `tools/captures/settings_tap_owner_04.txt`). With no serial monitor draining the console UART (the owner's normal use), the UART TX ring buffer fills in seconds and the superloop task **blocks inside `esp_log`** — starving every queue consumer behind it (UI posts, alerts, session data). Any UI interaction after that point (e.g. a Settings tap) hits a wedged system; symptoms ranged from frozen scroll to full resets. Same blocking-console shape as the 2026-08-31 freeze incident (which fixed only the *secondary* console — the primary UART blocks identically).

**Why four serial captures "never coincided with a crash":** the capture port being open drains the TX buffer — the bug cannot fire while recording. Owner-session capture proved it: device ran 100+ minutes, ~2,500-line BLE flood, zero resets while drained.

**Why the bench never reproduced it:** the direct-call harness, 8-cycle cascade harness, and synthetic-pointer indev harness (all clean, captures `settings_tap_repro_01/02/03`) exercised the entire nav path correctly — the crash was never in the nav code at all.

**FIX (this commit):** per-advertisement `ble:` log demoted `ESP_LOGI` → `ESP_LOGD` (compiled-out at the default INFO level). Verified post-flash: `ble_flood_fix_check.txt` shows the flood gone (47 app lines/30 s, all boot + 1-per-5 s env/gps heartbeats). Also made the boot `reset reason:` line permanent — first thing needed for any future field reset report.

**STILL OPEN:** scroll fps physics cap (~13 fps full-screen @ PSRAM 40M; ST7789 VSCSAD hardware scroll is the only real fix). Owner to confirm Settings-tap reset is gone in normal (unplugged-monitor) use.

---

## 2026-09-28 — Session: house-rules audit (owner-requested) + Settings-tap panic status

**House rules refreshed from KIMI.md** (Commandments I–VIII, Corollaries 1–22, Standing Rules, §10 quality standards) before auditing.

**Audit findings & fixes (this commit):**
- `report_export.cpp`: `fopen write failed` path leaked the two PSRAM channel-histogram buffers (`ch_obs`/`ch_sum`) — freed `aptable` only. Fixed (Standing Rule: match allocators to frees).
- `main.cpp`: per-AP Wi-Fi scan-result line demoted INFO → DEBUG — same per-event flood class as the BLE one fixed in 8fac3d9.
- `main.cpp`: boot reset reason now re-announced at 30 s / 90 s / 210 s / 450 s / 930 s after boot (5 lines total, deliberately bounded — an unbounded periodic INFO log would eventually fill an undrained console TX buffer and block the superloop). The boot-time line alone was always emitted before any host was listening.
- No TEMP DIAG leftovers anywhere in `main/` (all of tonight's harness code stripped; verified by grep).
- LVGL thread-safety pattern verified clean: all `ui_*_post_*` entry points only copy into critical-section statics; rendering stays on `ui_task`.
- Hot paths verified allocation-free: scan/BLE use static pools with LRU eviction; storage allocs are init/export-time only.
- Landmines intact: PSRAM 40M, coredump-to-flash OFF, secondary console NONE, NimBLE transport buffers untrimmed, display 30 MHz, `max_transfer_sz` 15 KB.

**Observations (no action taken):**
- `sdkconfig` carries `FLASHMODE_QIO=y` while the resolved string is `"dio"` (esptool flashes DIO). Stable in practice; regenerating via fullclean risks churn — flag for the owner.
- KIMI.md §10.2 ("draw buffers live in internal RAM, 1/10th screen") is stale: current known-good is 2 × full-frame buffers in PSRAM (Corollary 22 + 2026-09-26 commit 7c12678). Internal-RAM buffers are physically impossible now (Wi-Fi+NimBLE consume it all).

**Settings-tap panic — status:** root-caused to PANIC (esp_reset_reason=4, owner-session capture `reset_caught_01.txt`) but the backtrace is still uncaught: the fault does not reproduce while a serial capture port is open (owner-verified twice, 2026-09-28 01:23/01:27). Next lever when it recurs: the 930 s re-announce window means any capture within ~15 min of a crash still classifies it; catching the live backtrace needs the owner tapping inside a coordinated capture window. Captures tonight: `settings_tap_repro_01/02/03`, `settings_tap_owner_04`, `panic_live_01/02`, `reset_caught_01`, `reason_reannounce_check`, `ble_flood_fix_check` (all under `tools/captures/`).

---

## 2026-09-28 (later) — Breakthrough: capture tool was resetting the device

**Root cause of "fault only fires when nobody is watching": `tools/serial_capture.py` itself.** pyserial asserts RTS/DTR on port open; on the ESP32 auto-reset circuit RTS drives EN, so EVERY capture start pulsed EN and hard-reset the device. Every "coordinated capture" gave the device a fresh boot — and the owner verified the panic is **uptime/state-dependent**: "I let it sit a bit after the reset and it started doing it again" (01:40). The 00:49 panic (reason 4, real CPU exception) was genuine; tonight's recurrence boot read POWERON only because the capture-open reset contaminated the evidence.

**Fixes (this commit):**
- `serial_capture.py`: `setRTS(False)` + `setDTR(False)` immediately after open. Verified: consecutive opens no longer produce a boot banner in steady state. (First open right after esptool flashing still pulses once — esptool leaves the lines asserted; harmless for diagnostics, note when interpreting captures.)
- `main.cpp`: corrected the reset-reason legend (was off-by-one: real enum is 1=POWERON, 4=PANIC, 9=BROWNOUT, 14=PWR_GLITCH). Earlier reads stand: 00:49 was PANIC.

**Implication for the hunt:** the live-backtrace capture is now possible — keep the port open without resetting, let the device age into the fault state, tap during a long capture. PWR_GLITCH (14) and BROWNOUT (9) are also now distinguishable in the reason line if the recurrence is power-related rather than a panic.

---

## 2026-09-28 (~02:00) — Settings-tap panic ROOT-CAUSED and fixed

**Caught live (`tools/captures/panic_live_04.txt`, 3 panics in one 285 s window after the serial_capture.py RTS fix removed the accidental reset).** All identical:

```
E spi_common: spicommon_dma_setup_priv_buffer(460): Failed to allocate priv TX buffer
Guru Meditation Error: Core 0 panic'ed (Load access fault), MCAUSE=5, MTVAL=0
MEPC 0x4081549a (memcpy), RA 0x40809dfe (spi_device_polling_transmit)
```

**Decoded chain:** `fread → ff_disk_read → sdmmc_read_sectors → sdspi_host_start_command → spi_device_polling_transmit → setup_priv_desc`: the SD command's 32 B priv TX buffer alloc fails (internal DMA RAM exhausted) and the IDF v6.1 cleanup path calls `uninstall_priv_desc`, which `memcpy`s into `rx_data` from the **NULL** never-assigned `buffer_to_rcv` — load fault at address 0. Upstream of it all: `sess_rescan()` on the Settings screen walking the SD FAT directory.

**Why it needed uptime:** `dma_largest` (new telemetry field on the env heartbeat) is only **~288 B from boot** and drifts downward under load; once the largest free DMA block can't fit the ~48 B priv buffer, the next SD command is fatal. Display SPI commands draw from the same pool — this is the same wall documented in the splash-freeze saga, with the SD polling path as the fatal victim.

**Fix (this commit):**
- `sd_card.{h,cpp}`: `sd_dma_headroom()` — largest free DMA block ≥ 128 B.
- Guarded every SD caller: `sess_rescan`, session-picker open (`ui_settings.cpp`), and `write_buffer` (`session_logger.cpp`, drops one batch with a warning instead of crashing mid-survey).
- `main.cpp` env heartbeat now logs `dma_largest=` — watch it trend; if it declines over long sessions, something is still eating DMA RAM and the real fix is finding that eater.

**Note:** the 2026-09-28 BLE/Wi-Fi log-flood demotions (8fac3d9, 6aa101c) were still correct hygiene but were NOT the cause — the primary UART console drops output when undrained, it never blocks.

---

## 2026-09-28 (~03:10) — Owner's 03:02 reset was HARDWARE (reason 1 POWERON), not the panic

Capture `reset_after_guard.txt` (started ~15 s after the owner's reset) shows the post-reset boot reading **reset reason 1 (POWERON)** — a power/reset-pin event, NOT the software panic. The guard build (eb6578c) was confirmed active on the device (dma_largest telemetry present), so the SD-panic fix held; the remaining symptom is a hardware-class reset. The capture also exposed: `dma_largest` stays ~288 B from boot, then collapses to **0** in a transient window ~30 s after boot (Wi-Fi+BLE coexistence scan + session-start + alert flood all land together), recovering minutes later — the DMA wall is transient, not a monotonic leak.

Also fixed this session: alert annunciation had no cooldown — a parked target in scan range re-matched every sweep (~10 ALERT lines/s + LED flash). Ring buffer still records every event; LED + log now throttle to once per target per 30 s.

**Open: hardware reset root cause.** Prime suspect: 5 V USB delivery sagging under the Settings-tap load spike (backlight 100 % + full-screen redraw + SD + Wi-Fi TX), POR'ing the chip faster than the brownout detector. Discriminator for owner: run the device from a wall-charger USB supply (not the PC) — if Settings-tap resets vanish, it's the PC USB power path; if a reset still occurs, capture within 15 min and the reason line says 4 (panic) vs 1 (power).

---

## 2026-09-28 (04:35) — Hardware reset root cause CONFIRMED: PC USB power delivery

Owner ran the discriminator: device on a wall-charger USB supply, Settings tapped repeatedly after sit time — **could not reproduce the reset**. On PC USB it reset reliably. Conclusion: the 5 V rail from the PC port/cable sags under the Settings-tap load spike (backlight 100 % + full-screen redraw + SD scan + Wi-Fi TX), POR'ing the chip faster than the brownout detector (which is why it logged POWERON, not BROWNOUT). The two-reset picture stands: reason 4 = SD panic (fixed in eb6578c), reason 1 = power sag (hardware, solved by supply).

**Battery note for future hardware:** size the battery/boost path for peak current, not average — Wi-Fi TX + backlight + SD + display together pull roughly 0.5–0.8 A at 3.3 V in bursts. A weak boost converter or high-ESR cell will reproduce this exact bug in the field. If a sag does reach the rail in battery use, the brownout detector (level 7) will catch it and log reason 9 — distinguishable from both fixed failure modes.

---

## 2026-09-28 (~05:00) — Load-spike flattening: the reset fix proper

Owner rejection of the "wall-charger workaround" was correct — the fix is to flatten the surge in software so any USB host survives it. Two changes:

- **Backlight ramp** (`power_mgr.cpp`): upward brightness transitions now use the LEDC hardware fade over 150 ms (fade driver installed at init). Instant full backlight was the device's biggest single current step; downward transitions stay instant (reducing load never browns out). Boot also gains a soft fade-in.
- **Staggered SD scan** (`ui_settings.cpp`): the settings-entry session scan moved from `lv_async_call` (immediately after first paint) to a one-shot LVGL timer at +800 ms, so the card's draw lands past the redraw surge and the backlight ramp.

Owner verification pending: back on PC USB power (the original failing source), let it sit, hammer Settings.

---

## 2026-10-01 — Session close: full push to GitHub, owner testing next

All work through `ece68ee` (load-spike flattening) pushed to `origin/main` (`bigjoe420/SiteSurvey-Pro`) as the solid backup. Working tree clean at close.

**State at handoff:**
- FIXED this cycle: SD priv-buffer NULL-deref panic (guards on all SD callers, `sd_dma_headroom()`), alert flood cooldown, `report_export` leak, per-event log floods, capture tool RTS reset bug, reset-reason + `dma_largest` telemetry.
- SHIPPED, owner-verification PENDING: load-spike flattening (`ece68ee` — 150 ms backlight ramp-up, SD scan deferred 800 ms). Test protocol: power from PC USB (the original failing source), let it sit, hammer Settings. Reason line distinguishes any residual reset: 4 = software panic, 1 = power, 9 = brownout.
- NEXT PROJECT when owner is ready: ST7789 VSCSAD hardware scroll — the only real fix for the ~13 fps full-screen settings scroll cap.
- Housekeeping debt: ROADMAP.md/KIMI.md still reference `D:\SiteSurvey Pro` and IDF v6.0.1; reality is `C:\Development\SiteSurvey Pro` + ESP-IDF v6.1 (`python idf_run.py build/flash`, device COM3).
