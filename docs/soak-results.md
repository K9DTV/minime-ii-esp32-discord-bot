# MiniMe II -- HIL soak results

Attested local soaks (GitHub Actions cannot reach the board). Playbook: [`HIL_SOAK.md`](HIL_SOAK.md). Raw log for the run below lived in `docs/lan-monitor.log` on the soak PC (gitignored). Live JSON pulls: [`lan-status-snapshot.json`](lan-status-snapshot.json).

## 2026-09-24 -- v0.7.80 overnight Gateway soak (monitor stopped)

| Field | Value |
|---|---|
| Firmware on board | **v0.7.80** (`Display  -  v0.7.80`) |
| Workspace tree | **v0.7.85** local (SD/secrets/rings) -- **not flashed** this soak |
| Board | Guition JC3248W535EN @ `http://192.168.68.60` |
| Monitor | `docs/lan-monitor.ps1` (Bypass) |
| Start | **2026-09-23T21:45:20** PDT |
| Stop | **2026-09-24T01:57:57** PDT (operator) |
| End snapshot | **2026-09-24T01:57:58** PDT -- [`lan-status-snapshot.json`](lan-status-snapshot.json) |
| End uptime | **0d 4h 57m 57s** |
| Gateway | **Good** throughout (`gw=1`); no `gw=false` >=60 s stop |
| Bot | Idle/Online churn (cpu 160/240); end **Idle** cpu **160** |
| Heap / PSRAM | heapPct **33**; psramPct **96** |
| LCD / lastEvent | awake; lastEvent **GW drop** (recovered) |
| Temp / SD | `tempOk=false` (probe not on GPIO 18 yet); no `sdOk` on this build |
| Mid pull | 00:39:52 PDT @ up **3h 40m 32s** (same ver) |
| Result | **PASS** (Gateway soak) -- not a v0.7.83/0.7.84/0.7.85 feature PASS |
| Next | Flash `Display  -  v0.7.86` then [`soak-0.7.85.md`](soak-0.7.85.md) |

### lan-monitor.log (excerpt)

```
=== MiniMe II monitor start 2026-09-23T21:45:20 ... base=http://192.168.68.60 ===
[21:45:20] gw=True botOnline=True cpu=240 up=0d 0h 46m 0s heapPct=34
[21:45:20] LOG READY session=yes
[21:59:05] gw=True botOnline=False cpu=160 up=0d 0h 59m 45s heapPct=32
=== SNAPSHOT 2026-09-24T00:39:52-07:00 ver=0.7.80 up=0d 3h 40m 32s gw=1 idle ===
[01:12:52] gw=True botOnline=True cpu=240 up=0d 4h 13m 32s
[01:13:24] LOG READY session=yes
[01:17:50] gw=True botOnline=False cpu=160 up=0d 4h 18m 30s
[01:20:40] gw=True botOnline=True cpu=240 up=0d 4h 21m 21s
[01:25:41] gw=True botOnline=False cpu=160 up=0d 4h 26m 21s
=== MiniMe II monitor STOP 2026-09-24T01:57:57-07:00 ... ver=0.7.80 up~4h58m gw=1 idle ===
```

### MiniMe LOG (fulllog)

- Boot Identify -> READY
- **OP7_RECONNECT** ~44.6 min: gap **1646 ms** -> `RECOVERED` / READY
- Second **OP7_RECONNECT** ~4h 13m (`DROP_START` @ 15207402): gap **2134 ms** -> READY @ 15209974
- No stuck disconnect; GW stayed Good after each recover

### MiniMe Serial

- Steady `[GW] alive ... gw=1 id=1 drop=0` (~60 s)
- Repeated `[C0] DS18B20 disconnected` (sensor not fitted / not on GPIO 18 -- expected)

---

## 2026-09-23 -- v0.7.80 split + fixed buffers + Controls

| Field | Value |
|---|---|
| Firmware | **v0.7.80** (`Display  -  v0.7.80`) |
| Commit | `57a9b41` -- web/discord file split; README status/OTA/LAN; (CI tokenize/`ci_html` fix still local at attestation) |
| Board | Guition JC3248W535EN @ `http://192.168.68.60` |
| Playbook | [`soak-0.7.80.md`](soak-0.7.80.md) |
| Monitor | `docs/lan-monitor.ps1` (Bypass); start **21:45** PDT |
| Attestation | **21:56** PDT (operator) |
| Result | **PASS** (blocks A-D) |

### What was under test

- Fixed-buffer Discord REST / DeepSeek (`!ask` short + long + hammer)
- `web_ui` / `web_render` / `discord_http` split (LAN Display/Log/Controls)
- Web slider debounce (bright/vol)
- LCD Controls hit boxes; Save / Cancel / factory (long-press + `!resetprefs`)
- Smoke: wake, theme chip, DM/@mention + `!clear`

### Operator report

Blocks **A, B, C, D** completed -- all worked as expected. No fail noted (panic, stuck GW, miss-hits, bad prefs after Save, `!ask` hang).

### Monitor note

Interactive attestation ~**11 min** after monitor start (not a full >=30 min idle-only Gateway watch). Leave `lan-monitor` running longer if you want a separate idle GW stamp for this build.

---

## 2026-09-22 / 2026-09-23 -- v0.7.43 TWDT + `!ask` hammer

| Field | Value |
|---|---|
| Firmware | **v0.7.43** (`Display  -  v0.7.43`) |
| Commit | `a73fa11` -- 90 s loop TWDT, Discord send 60 s budget, `!ask` handshake 15 s / body 30 s |
| Board | Guition JC3248W535EN @ `http://192.168.68.60` |
| Monitor | `docs/lan-monitor.ps1` (default base URL) |
| Monitor wall start | 2026-09-22 **15:15** PDT |
| Attestation time | 2026-09-23 ~**00:19** PDT |
| Result | **PASS** |

### What was under test

- Loop-task TWDT at **90 s** (`enableLoopWDT` only; no `uiTask` subscribe; no mid-HTTPS TWDT reset sprinkle)
- `sendDiscordMessage` **60 s** wall budget
- DeepSeek `!ask` timeouts: handshake **15 s**, body deadline **30 s**
- Dual-core HOL path (dedicated DeepSeek TLS)

### Board continuous uptime

- Intentional **software-update reboot** ~**15:55** PDT (not a panic). Uptime reset; Gateway down **3 s**, then `READY`.
- Continuous post-update uptime reached **~7h 45m+** by **23:42** (still climbing; no further uptime reset in the log through attestation).

### Gateway outages (`GW_DOWN` via lan-monitor)

| When (PDT) | Duration | Cause |
|---|---:|---|
| 15:55:36 -> 15:55:39 | **3 s** | Software update reboot |
| 22:56:23 -> 22:56:28 | **5 s** | Brief drop; recovered; `READY` ~1 min later |

- **Total GW-down:** ~**8 s**
- **No** `GW_DOWN` lasting >=60 s
- **No** `MONITOR_STOP`

### LAN poll timeouts (`FETCH_FAIL`)

Seven LAN `/api/status` timeouts (monitor HTTP timeout). **Not** Discord Gateway outages. One fell during the `!ask` hammer (23:16) -- expected when Core 1 is busy on HTTPS.

### `!ask` stress

| Field | Value |
|---|---|
| Window | **23:10 -> 00:16** PDT (hammered repeatedly) |
| Discord message drops | **None** (operator report) |
| `GW_DOWN` in window | **None** |
| Reboot / TWDT panic | **None** (uptime kept increasing) |
| Heap (samples) | Stable ~**37-38%** |

### Verdict

**v0.7.43 TWDT + ask budgets are board-proven** under multi-hour soak and ~66 minutes of repeated `!ask`. Soft Discord guard (HB ack) + 90 s loop backstop held without false panic. GitHub CI green still does not replace HIL; this file is the HIL attestation for this release.

## TWDT positive proof -- `!hang` (v0.7.48+)

Proves the **90 s loop TWDT fires when Core 1 hangs**, not only that it stays quiet under `!ask`.

### Scratch build steps

1. In `MiniMe_Discord_Bot_II/minime_config.h`, uncomment `#define MINIME_TEST_TWDT`.
2. Flash that build (`Display  -  v0.7.48` or newer with the define).
3. As owner: `!hang` -- LCD may show `TWDT` / `hang...`; Discord goes quiet (loop stuck).
4. Wait **>= 90 s** for Task WDT panic + reboot.
5. After boot: owner `!coredump` -- summary should name **Task WDT** / loop task.
6. Save a screenshot of the `!coredump` reply (or Serial/LOG lines above `ELF file SHA256`) into `docs/` and link it below.
7. **Comment out** `#define MINIME_TEST_TWDT` again and reflash production (never leave `!hang` enabled).

### Result (fill after run)

| Field | Value |
|---|---|
| Firmware | scratch build with `MINIME_TEST_TWDT` (App SHA `d7804ba59...`) |
| Date | 2026-09-23 ~**01:54** PDT |
| Panic reason | **Task watchdog got triggered** -- `loopTask (CPU 1)` did not reset in time |
| Evidence | Discord `!coredump` after reboot (coredump @ `0xFD0000`, PC `0x4037B59F`, IDLE1 / ExcCause as reported) |
| Screenshot | [`TWDT.jpg`](TWDT.jpg) |
| Production build | v0.7.48, `MINIME_TEST_TWDT` **not** defined; `!hang` handler compiled out (`kCmds` does not register it) |
| Result | **PASS** |

**Verdict:** 90 s loop TWDT **fires when Core 1 hangs** (`!hang`), proven via flash coredump -- not only quiet under soak/`!ask`.
