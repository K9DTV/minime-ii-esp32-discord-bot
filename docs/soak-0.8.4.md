# MiniMe II -- soak for v0.8.4 (cold-path buffers + module splits + Log scrollbar + Src/IP)

Flash **v0.8.4** (`Display  -  v0.8.4`). Date the run in [`soak-results.md`](soak-results.md) when done.

General Gateway watch: [`HIL_SOAK.md`](HIL_SOAK.md) + `docs/lan-monitor.ps1`.
Prior SD / secrets / IP colors: [`soak-0.7.85.md`](soak-0.7.85.md) (still valid regression).
Prior Controls / `!ask` fixed buffers: [`soak-0.7.80.md`](soak-0.7.80.md).

## What this soak is proving

| Change | Pass means |
|---|---|
| Cold-path `char[]` (0.8.0) | `!weather` / `!news` / `!ask` / `!sys` post; no heap-stall mid-fetch |
| `display_draw_*` split (0.8.1) | Display / Log / Controls paint; theme + Menus chips OK |
| Gateway module split (0.8.2) | Identify + HB; drop/reconnect recovers; GW Good |
| Log/Serial scrollbar (0.8.3) | Bar only when lines overflow; thumb bottom = newest; no content drag |
| Src above IP (0.8.4) | LCD + web metrics order: ... Event, **Src**, **IP**, OTA ... |

## Setup

```powershell
$env:MINIME_LAN = "http://192.168.68.60"
powershell -File docs/lan-monitor.ps1
```

Confirm glass/web **v0.8.4** before scoring PASS. Leave monitor running.

## Timed Gateway soak (background)

- Idle **>= 30 min** with monitor up -- no `MONITOR_STOP`, no GW down >= 60 s
- Note `FETCH_FAIL` (LAN only) vs Discord drops
- Optional: pull `/api/status` into `docs/lan-status-snapshot.json`

## A -- Cold-path commands (0.8.0)

1. `!help` -- reply lands
2. `!weather <zip>` -- OK; GW stays Good
3. `!news` / `!iss` / `!apod` (any one) -- OK
4. `!sys` -- heap/PSRAM/Src-related diagnostics look sane
5. `!ask` short + one longer (~400 chars) -- one Discord message each; no hang
6. Optional: 3x `!ask` in ~1 min -- no panic; GW recovers if busy

## B -- LCD draw / Gateway splits (0.8.1 / 0.8.2)

1. Menus cycle Display / Log / Controls -- all three paint
2. Light/Dark chip -- palette swaps; brand bar OK
3. After boot or brief Wi-Fi blip: Gateway **Good**, Identify works
4. Serial/LOG: READY / reconnect lines appear after a drop (if you force one)

## C -- Log/Serial scrollbar (0.8.3)

1. Log page with **few** lines -- **no** scrollbar on LOG or Serial
2. After enough GW/LOG traffic to fill past the panel -- scrollbar **appears**
3. Drag **thumb** / tap **track** -- scroll moves; thumb at bottom = newest
4. Content area drag does **not** scroll (scrollbar only)
5. LAN Log page still uses browser overflow scroll (unchanged)

## D -- Src above IP (0.8.4)

1. LCD Display metrics: **Src** row is **above** **IP**
2. LAN Display page: same order in metrics
3. **Src** still shows `SD card` or `firmware`; Wi-Fi-down **IP** still solid red

## E -- Regression smoke

1. Controls: Brightness / Volume / toggles / Clear / Cancel / Save
2. `!clear` clears DM/Mention + Msg
3. Touch wake after backlight sleep
4. Prior SD rules still hold if you care this run: no-SD **SD** red/`0M`; see soak-0.7.85

## F -- Background health

1. Monitor >=30 min idle -- GW Good
2. Optional overnight: same monitor; record in soak-results

## Pass / Fail

| Result | Rule |
|---|---|
| **PASS** | A-F done (E optional depth OK); glass **v0.8.4**; scrollbar overflow-only; Src above IP on LCD+web; cold-path cmds OK; monitor no GW>=60 s stop |
| **FAIL** | Panic, stuck GW, scrollbar always visible or missing when full, Src below IP, fetch/`!ask` hang, blank Display after draw split |

Record date + version + PASS/FAIL in [`soak-results.md`](soak-results.md). Snapshot: `docs/lan-status-snapshot.json`.
