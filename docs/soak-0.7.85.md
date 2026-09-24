# MiniMe II -- soak for v0.7.85 (SD / secrets / temp / IP flash / LOG accessors)

Flash **v0.7.86** (`Display  -  v0.7.86`). Date the run in [`soak-results.md`](soak-results.md) when done.

**v0.7.87 and newer:** do not score the compile-time fallback row below. SD `/secrets.h` is required; a missing file waits on the glass instead of connecting with build secrets. See [`HIL_SOAK.md`](HIL_SOAK.md).

General Gateway watch: [`HIL_SOAK.md`](HIL_SOAK.md) + `docs/lan-monitor.ps1`.
Prior Controls / `!ask` smoke: [`soak-0.7.80.md`](soak-0.7.80.md).
Prior overnight Gateway on **v0.7.80**: **2026-09-24** in [`soak-results.md`](soak-results.md).

## What this soak is proving

| Change (through 0.7.86) | Pass means |
|---|---|
| SD on **HSPI only** (0.7.86) | Boot can read SD; LCD colors stay normal; no reboot after SD/IP |
| Servo removed (0.7.82+) | No `!servo` in `!help`; no `Srv` meter on LCD/web |
| DS18B20 on GPIO **18** | `!temp` / Up T OK when probe fitted; Serial not stuck disconnect-spam |
| SD SPI 10/11/12/13 + meter | **SD** free **MB** + bar on LCD/web when card mounted |
| SD `/secrets.h` load (0.7.84) | Boot LOG `Secrets: loaded N keys from SD`; Wi-Fi/Discord use those values |
| Compile-time fallback | No SD file -> `Secrets: using compile-time...` still connects |
| No-SD IP flash (0.7.83) | IP bright red **2 s on / 2 s off**; stops when card mounts |
| LOG ring accessors (0.7.85) | `/api/status` `fulllog` / `serial` still populate; LAN Log page paints |

## Setup

```powershell
$env:MINIME_LAN = "http://192.168.68.60"
powershell -File docs/lan-monitor.ps1
```

Confirm glass/web **v0.7.86** before scoring PASS. Leave monitor running.

## Timed Gateway soak (background)

- Idle **>= 30 min** with monitor up -- no `MONITOR_STOP`, no GW down >= 60 s
- Note `FETCH_FAIL` (LAN only) vs Discord drops
- Optional: pull `/api/status` into `docs/lan-status-snapshot.json`

## A -- Pins / meters / IP flash

1. LCD + web: **PSRAM** / **SRAM** / **SD** (not Srv). SD like `512M` or `--` if no card
2. `/api/status`: `sdOk`, `sdFreeMb`, `sdTotalMb`, `sdPct`; no `servo` / `srvPct`
3. With card: SD bar OK; IP normal color
4. No card: SD `--`; IP flashes **2 s red / 2 s off** (time it)
5. Reinsert: flash stops; SD MB returns (hot-plug ~2 s)

## B -- Temperature (GPIO 18)

1. Probe on rear 4-pin **GPIO 18** (+ GND / 3.3 V) -- skip if not fitted; note skip
2. When fitted: `!temp` OK; Up T not `--Error--`
3. Serial: no endless `[C0] DS18B20 disconnected` once probe is solid

## C -- Secrets from SD

1. Card with `/secrets.h` -- boot LOG shows load count; Discord/Wi-Fi work
2. Optional: remove file, reboot -- compile-time fallback still connects

## D -- Discord / owner smoke

1. `!help` -- no `!servo`
2. `!sys` -- heap/PSRAM sane
3. `!clear` / Controls Save-Cancel (regression)
4. Optional short `!ask` -- GW stays Good

## E -- LOG accessors (0.7.85)

1. Open LAN **Log** page -- Full + Serial lines appear (not empty forever)
2. `/api/status` `fulllog` and `serial` arrays non-empty after boot / GW activity
3. Force a short Discord drop/recover if needed -- new READY lines show up in fulllog

## F -- Background health

1. Monitor >=30 min idle -- GW Good; note reconnects
2. Touch wake after backlight sleep
3. Light/Dark + Menus pages still paint

## Pass / Fail

| Result | Rule |
|---|---|
| **PASS** | A-F done (B skip OK if no probe); version on glass = **0.7.86**; SD secrets or compile-time confirmed; IP timing by eye; Log JSON/UI OK; monitor no GW>=60 s stop; **no color scramble / reboot after SD** |
| **FAIL** | Panic, stuck GW, empty Log after accessors, SD never mounts with known-good card, `/secrets.h` ignored when present, IP flash wrong rate |

Record date + version + PASS/FAIL in [`soak-results.md`](soak-results.md). Snapshot: `docs/lan-status-snapshot.json`.
