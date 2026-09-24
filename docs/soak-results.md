# MiniMe II — HIL soak results

Attested local soaks (GitHub Actions cannot reach the board). Playbook: [`HIL_SOAK.md`](HIL_SOAK.md). Raw log for the run below lived in `docs/lan-monitor.log` on the soak PC (gitignored).

## 2026-09-23 — v0.7.80 split + fixed buffers + Controls

| Field | Value |
|---|---|
| Firmware | **v0.7.80** (`Display · v0.7.80`) |
| Commit | `57a9b41` — web/discord file split; README status/OTA/LAN; (CI tokenize/`ci_html` fix still local at attestation) |
| Board | Guition JC3248W535EN @ `http://192.168.68.60` |
| Playbook | [`soak-0.7.80.md`](soak-0.7.80.md) |
| Monitor | `docs/lan-monitor.ps1` (Bypass); start **21:45** PDT |
| Attestation | **21:56** PDT (operator) |
| Result | **PASS** (blocks A–D) |

### What was under test

- Fixed-buffer Discord REST / DeepSeek (`!ask` short + long + hammer)
- `web_ui` / `web_render` / `discord_http` split (LAN Display/Log/Controls)
- Web slider debounce (bright/vol)
- LCD Controls hit boxes; Save / Cancel / factory (long-press + `!resetprefs`)
- Smoke: wake, theme chip, DM/@mention + `!clear`

### Operator report

Blocks **A, B, C, D** completed — all worked as expected. No fail noted (panic, stuck GW, miss-hits, bad prefs after Save, `!ask` hang).

### Monitor note

Interactive attestation ~**11 min** after monitor start (not a full ≥30 min idle-only Gateway watch). Leave `lan-monitor` running longer if you want a separate idle GW stamp for this build.

---

## 2026-09-22 / 2026-09-23 — v0.7.43 TWDT + `!ask` hammer

| Field | Value |
|---|---|
| Firmware | **v0.7.43** (`Display · v0.7.43`) |
| Commit | `a73fa11` — 90 s loop TWDT, Discord send 60 s budget, `!ask` handshake 15 s / body 30 s |
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
| 15:55:36 → 15:55:39 | **3 s** | Software update reboot |
| 22:56:23 → 22:56:28 | **5 s** | Brief drop; recovered; `READY` ~1 min later |

- **Total GW-down:** ~**8 s**
- **No** `GW_DOWN` lasting ≥60 s
- **No** `MONITOR_STOP`

### LAN poll timeouts (`FETCH_FAIL`)

Seven LAN `/api/status` timeouts (monitor HTTP timeout). **Not** Discord Gateway outages. One fell during the `!ask` hammer (23:16) — expected when Core 1 is busy on HTTPS.

### `!ask` stress

| Field | Value |
|---|---|
| Window | **23:10 → 00:16** PDT (hammered repeatedly) |
| Discord message drops | **None** (operator report) |
| `GW_DOWN` in window | **None** |
| Reboot / TWDT panic | **None** (uptime kept increasing) |
| Heap (samples) | Stable ~**37–38%** |

### Verdict

**v0.7.43 TWDT + ask budgets are board-proven** under multi-hour soak and ~66 minutes of repeated `!ask`. Soft Discord guard (HB ack) + 90 s loop backstop held without false panic. GitHub CI green still does not replace HIL; this file is the HIL attestation for this release.

## TWDT positive proof — `!hang` (v0.7.48+)

Proves the **90 s loop TWDT fires when Core 1 hangs**, not only that it stays quiet under `!ask`.

### Scratch build steps

1. In `MiniMe_Discord_Bot_II/minime_config.h`, uncomment `#define MINIME_TEST_TWDT`.
2. Flash that build (`Display · v0.7.48` or newer with the define).
3. As owner: `!hang` — LCD may show `TWDT` / `hang...`; Discord goes quiet (loop stuck).
4. Wait **≥ 90 s** for Task WDT panic + reboot.
5. After boot: owner `!coredump` — summary should name **Task WDT** / loop task.
6. Save a screenshot of the `!coredump` reply (or Serial/LOG lines above `ELF file SHA256`) into `docs/` and link it below.
7. **Comment out** `#define MINIME_TEST_TWDT` again and reflash production (never leave `!hang` enabled).

### Result (fill after run)

| Field | Value |
|---|---|
| Firmware | scratch build with `MINIME_TEST_TWDT` (App SHA `d7804ba59…`) |
| Date | 2026-09-23 ~**01:54** PDT |
| Panic reason | **Task watchdog got triggered** — `loopTask (CPU 1)` did not reset in time |
| Evidence | Discord `!coredump` after reboot (coredump @ `0xFD0000`, PC `0x4037B59F`, IDLE1 / ExcCause as reported) |
| Screenshot | [`TWDT.jpg`](TWDT.jpg) |
| Production build | v0.7.48, `MINIME_TEST_TWDT` **not** defined; `!hang` handler compiled out (`kCmds` does not register it) |
| Result | **PASS** |

**Verdict:** 90 s loop TWDT **fires when Core 1 hangs** (`!hang`), proven via flash coredump — not only quiet under soak/`!ask`.
