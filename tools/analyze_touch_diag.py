#!/usr/bin/env python3
"""Analyze touch_diag.csv (9-col, 50 Hz) from the settings-scroll diag build.

Columns: idx,tick,rawx,rawy,mapx,mapy,pressed,scroll_y,content_h,scrollable

Discriminates the scroll-regression hypotheses:
  A) scrollable flag flicker        -> scrollable column toggles mid-drag
  B) content_h collapse             -> content_h column dips
  C) sampler stalls / stale bursts  -> tick gaps > 40 ms
  D) momentum reverse-throw         -> mapy velocity flips near release
  E) button scroll-absorb (new)     -> drag samples present, panel scroll_y
                                       stays FLAT (panel never got the drag)
  F) LVGL-level reset               -> scroll_y rises then snaps to 0/smaller
"""
import csv, sys, statistics

path = sys.argv[1] if len(sys.argv) > 1 else "touch_diag.csv"
rows = []
with open(path, newline="") as f:
    for r in csv.DictReader(f):
        try:
            rows.append({k: int(r[k]) for k in
                         ("tick", "mapx", "mapy", "pressed", "scroll_y",
                          "content_h", "scrollable")})
        except (ValueError, KeyError, TypeError):
            pass  # skip header/partial lines

if not rows:
    print("no data rows")
    sys.exit(1)

print(f"rows={len(rows)}  span={rows[-1]['tick']-rows[0]['tick']} ms")

# --- tick gaps ------------------------------------------------------------
gaps = [(b["tick"] - a["tick"], b["tick"]) for a, b in zip(rows, rows[1:])]
big = [g for g in gaps if g[0] > 40]
print(f"tick gaps >40ms: {len(big)}", big[:10])

# --- segment into drags (pressed runs) ------------------------------------
drags, cur = [], []
for r in rows:
    if r["pressed"]:
        cur.append(r)
    elif cur:
        drags.append(cur); cur = []
if cur:
    drags.append(cur)
print(f"drags: {len(drags)}")

reset_events = 0
flat_drags = 0
for i, d in enumerate(drags):
    ys = [r["mapy"] for r in d]
    sc = [r["scroll_y"] for r in d]
    ch = {r["content_h"] for r in d}
    sf = {r["scrollable"] for r in d}
    dy = ys[-1] - ys[0]
    dsc = sc[-1] - sc[0]
    # scroll_y rise-then-reset inside one drag
    peak = max(sc)
    reset = peak > 20 and sc[-1] < peak - 20
    if reset:
        reset_events += 1
    # finger moved meaningfully but panel never scrolled
    if abs(dy) > 30 and max(sc) - min(sc) <= 4:
        flat_drags += 1
    flag = ""
    if reset: flag += " RESET!"
    if len(ch) > 1: flag += f" CONTENT_H_VARIES{sorted(ch)}"
    if len(sf) > 1: flag += f" SCROLLABLE_VARIES{sorted(sf)}"
    if abs(dy) > 30 or abs(dsc) > 10 or flag:
        print(f"drag {i:2d}: n={len(d):3d} mapy {ys[0]:3d}->{ys[-1]:3d} (d{dy:+4d}) "
              f"scroll_y {sc[0]:4d}->{sc[-1]:4d} (d{dsc:+4d}) peak={peak:4d}{flag}")

print(f"\nscroll_y rise-then-reset events: {reset_events}")
print(f"drags with real finger travel but FLAT panel scroll_y: {flat_drags}")
print("\nverdict guide:")
print("  resets>0                -> LVGL-level reset (content_h/flag/momentum)")
print("  flat_drags>0, resets=0  -> button scroll-absorb (panel never got drag)")
print("  CONTENT_H_VARIES        -> hypothesis B (layout collapse)")
print("  SCROLLABLE_VARIES       -> hypothesis A (flag flicker)")
print("  many tick gaps          -> hypothesis C (sampler stall)")
