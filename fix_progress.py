with open('PROGRESS.md', 'r', encoding='latin-1') as f:
    lines = f.readlines()

new_entry_lines = [
    "### Update: 2026-08-24 — Cleanup: remove timing instrumentation from lvgl_port.cpp\n",
    "\n",
    "**Context:** The spin3d audit (previous entry) added `esp_timer_get_time()` measurement in `touch_read_cb()` and `ui_task()` to diagnose touch latency. The root cause was found and fixed (bool-gate -> `lv_timer_pause/resume`). The instrumentation served its purpose and was becoming permanent log noise.\n",
    "\n",
    "**Removed:**\n",
    "- `touch_read_cb()`: `int64_t t0/elapsed` + `ESP_LOGW` when touch read >500 us\n",
    "- `ui_task()`: `int64_t t0/elapsed` + `ESP_LOGW` when `lv_timer_handler()` >5 ms\n",
    "\n",
    "**Result:** Build clean (`lvgl_port.cpp.obj` recompiled), binary `0x15f480` (31% free). Boot log verified compile time `Aug 24 2026 18:23:38` — fresh. No timing warning spam. All subsystems nominal.\n",
    "\n",
]

target_idx = None
for i, line in enumerate(lines):
    if "Cleanup: remove timing instrumentation" in line:
        target_idx = i
        break

if target_idx is None:
    print("ERROR: Could not find cleanup entry")
    exit(1)

output = lines[:target_idx] + new_entry_lines + lines[target_idx+1:]

with open('PROGRESS.md', 'w', encoding='latin-1') as f:
    f.writelines(output)

print("Fixed formatting at line", target_idx + 1)
