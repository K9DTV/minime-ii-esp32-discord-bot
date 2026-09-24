# MiniMe II -- soak for v0.7.84 (SD secrets load + SD meter / temp / no servo)

**Superseded for new flashes:** use [`soak-0.7.85.md`](soak-0.7.85.md) (same hardware checks + LOG accessors).

Flash **v0.7.84** (`Display  -  v0.7.84`) only if you are scoring that build specifically.
General Gateway watch: [`HIL_SOAK.md`](HIL_SOAK.md) + `docs/lan-monitor.ps1`.
Prior Controls/`!ask` batch: [`soak-0.7.80.md`](soak-0.7.80.md).

## What this soak is proving

| Change | Pass means |
|---|---|
| Servo removed | No `!servo` in `!help`; no `Srv` meter on LCD/web |
| DS18B20 on GPIO **18** (rear 4-pin) | `!temp` / Up T line OK when sensor wired; Serial not stuck in disconnect spam if probe present |
| SD SPI 10/11/12/13 | **SD** row shows free **MB** + free/total bar (LCD + web) when card mounted |
| Card file `/secrets.h` | Boot LOG shows `Secrets: loaded N keys from SD`; Wi-Fi/Discord use those values |
| Compile-time fallback | No SD file -> `Secrets: using compile-time...` still connects if build secrets are real |
| No SD -> IP flash | IP bright red **2 s on / 2 s off** on LCD and web; stops when card mounts |

## Setup

```powershell
$env:MINIME_LAN = "http://192.168.x.x"   # your board
powershell -File docs/lan-monitor.ps1
```

Leave the monitor running. Confirm glass/web header **v0.7.84** before scoring PASS.

## Timed Gateway soak (background)

- Idle **>= 30 min** with monitor up -- no `MONITOR_STOP`, no GW down >= 60 s
- Note any `FETCH_FAIL` (LAN only) vs real Discord drops
- Optional: pull `/api/status` once and paste fulllog / serial tails into soak notes

## A -- Pins / meters

1. LCD + web: **PSRAM** / **SRAM** / **SD** (not Srv). SD value like `512M` or `--` if no card
2. `/api/status`: `sdOk`, `sdFreeMb`, `sdTotalMb`, `sdPct` present; no `servo` / `srvPct`
3. With card: SD bar fills with free/total; IP normal color
4. Eject / no card: SD `--`, IP flashes **2 s red / 2 s off** (time it on glass or web)
5. Reinsert card: flash stops; SD MB returns (hot-plug retry ~2 s)

## B -- Temperature (GPIO 18)

1. Probe on rear 4-pin **GPIO 18** (+ GND / 3.3 V)
2. `!temp` -- Discord reply with deg F/C; LCD Up T line not `--Error--`
3. Serial: no endless `[C0] DS18B20 disconnected` once probe is solid (<=1/min when truly missing is OK)

## C -- Discord / owner smoke

1. `!help` -- no `!servo` line
2. `!sys` -- heap/PSRAM sane
3. `!clear` / Controls Save-Cancel still work (regression)
4. Optional short `!ask` once -- GW stays Good

## D -- Background health

1. Monitor >=30 min idle -- GW Good; note reconnects
2. Touch wake after backlight sleep
3. Light/Dark + Menus pages still paint

## Pass / Fail

| Result | Rule |
|---|---|
| **PASS** | A-D done; IP timing verified by eye; SD secrets load confirmed in LOG; version on glass = 0.7.84; monitor no GW>=60 s stop |
| **FAIL** | Panic, stuck GW, SD never mounts with known-good card, `/secrets.h` ignored when present, IP flash wrong rate, temp still on old pin behavior after rewire |

Record the run in [`soak-results.md`](soak-results.md) (date, commit, PASS/FAIL, notes). Snapshot helper: `docs/lan-status-snapshot.json` after a pull.
