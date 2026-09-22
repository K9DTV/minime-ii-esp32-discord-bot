# MiniMe II — code review notes (pro pass)

What we **fixed** vs what we **left** and why. Current: **v0.7.41**.

## Fixed (through dual-core / fetch pumps / pro hardening)

| Item | When / what |
|---|---|
| Dual-core UI vs Gateway | 0.7.0+ — Core 0 `uiTask` LCD/touch; Core 1 net/Discord |
| Command queue | 0.7.0 — `MESSAGE_CREATE` enqueue; `drainDiscordCmds` / `handleCommand` on Core 1 |
| Mid-draw `pumpGateway` | 0.7.0 — removed; QSPI no longer on HB path |
| DS18B20 stall on Core 1 | 0.7.1/0.7.2 — Core 0 `pollTemperatureNonBlocking` only |
| DashSnap sync | 0.7.1 — seqlock + 1 s publish throttle |
| Fetch body HB pumps | 0.7.6 — weather / news / APOD / ISS / arXiv use `readHttpBodyAfterHeaders` |
| `*GetOpen` returns chunked/CL | 0.7.7 — body reader no longer guesses “until close” |
| Gateway TLS verify | 0.7.8 — `beginSslWithBundle` + CA blob |
| Body truncation ≠ success | 0.7.8 — 48 KB / hitCap returns `false` |
| Presence eviction | 0.7.8 — unknown presence no longer `addOrPickUserSlot` |
| Owner gate / firmware URL / atomics / VERSION | 0.7.8 |
| Hot-path `String` / chunked web | 0.7.9 |
| LOG/Serial split | 0.7.12 — FULL LOG dump body vs normal MmLog |
| DeepSeek off stack | 0.7.11+; **0.7.13** PSRAM `BasicJsonDocument` + `nothrow` + no substring; **0.7.14** `capacity()==0` => OOM |
| Loop stack override | **0.7.14** — `ARDUINO_LOOP_STACK_SIZE` before `Arduino.h`; **0.7.27** drop sketch `getArduinoLoopTaskStackSize` (core 3.3+ already defines it) |
| Heap vs PSRAM | **0.7.13** — bar/`heap*` = internal; PSRAM separate; **0.7.14** LCD PSRAM row |
| `MINIME_USER_AGENT` | **0.7.13** — single `#define` |
| Members JSON on stack | **0.7.13** — heap `DynamicJsonDocument*` for guild members |
| Chunked body truncate ≠ success | **0.7.15** — trailer/size/mid-chunk/`Content-Length` incomplete → `false` |
| Large JSON in PSRAM | **0.7.17** — `gwDoc` / `statusDoc` / `deepSeekDoc` via shared `SpiRamJsonDocument` |
| Discord 429 / Retry-After | **0.7.18** — `sendDiscordMessage` waits + retries (header / JSON); Gateway pumped |
| UI overlay cross-core | **0.7.23** — transient/Event `char[]` + `portMUX`; DM/Mention atomic; **0.7.24** until expire under mux; temp store/snapshot |
| FULL LOG / Core0 MmLog | **0.7.23/0.7.24** — `[GW] ===` prefix; drop count after null check; counter in `web_ui.cpp` |
| Event vs transient | **0.7.25** — `showTransient` does not clobber sticky Event (`lastEventLine`) |
| Mid-TLS Wi‑Fi reconnect | **0.7.25** — `ensureWifiForGateway` no-op if `gwPumping` \|\| `httpsInUse` |
| Command use ≠ Online | **0.7.25** — `recordUserUse` leaves Discord status alone |
| CPU idle without identify | **0.7.25** — `updateBotPresenceIdle` can drop 240→160 even if never identified |
| HTTP body throughput | **0.7.25** — block `read` + `reserve`/`concat` (was per-byte `+=`) |
| NTP hot path | **0.7.25** — Pacific offset recompute ≤1/min |
| `PRESENCE_UPDATE` `d.status` | Already in filter (`gwFilter["d"]["status"]`); comment clarified 0.7.25 |
| God-file split | **0.7.26** — `command_fetch.cpp`; LCD overlay/snap/draw modules; `gwFilter` init at connect |
| Owner `!coredump` | **0.7.29** — flash partition summary / clear (`coredump_cmd.cpp`) |
| Explicit TWDT | **0.7.30 / 0.7.34** then **unrolled 0.7.33 / 0.7.35** — still deferred (not in 0.7.38) |
| `!ask` HOL | **0.7.38** — DeepSeek dedicated TLS; drain during waits only if `!httpsInUse` (no TWDT) |
| LCD version | **0.7.30** — left panel `Ver` row = `MINIME_VERSION` (kept) |
| Cmd-error ring | **0.7.31** — last 10 failure replies; `!sys` + Serial `[CMDERR]` (kept) |
| ArduinoJson 7 | **0.7.36** — migrate from AJ6; PSRAM via `Allocator` |
| TrackedUser / guild IDs | **0.7.36** — cold-path `char[]` (was `String`) |
| Discord REST header timeout retry | **0.7.37** — same attempt budget as 429; short backoff + Gateway pump |
| Core 0 MmLog bridge | **0.7.39** — enqueue → `drainCore0Logs` on Core 1; drops = ring overflow only |
| Nested `gwPumping` HB-only | **0.7.40** — pruned; re-entry no-op (flag kept for defer / Wi-Fi) |
| CI beyond compile | **0.7.41** — host `ci_sanity.py` + compile; HIL soak = local `docs/HIL_SOAK.md` |

## Known tradeoffs (not deferred bugs)

- **0.7.38 HOL:** mid-shared-fetch busy spam avoided by `!httpsInUse` gate; `!ask` unblocks Discord/`!weather`. Panic inside `handleCommand` can leave `drainCmdsBusy` stuck until reboot (RAII clears normal returns).
- **TWDT still deferred:** 0.7.30/0.7.34 panics; do not bundle with HOL again without a dump.

## Still deferred (why)

### 1 — Explicit TWDT register/reset

Unrolled 0.7.33/0.7.35. Not re-tried in 0.7.38 (HOL isolated). Retry only with dump evidence.

### 2 — Remaining `String` on cold HTTPS/command reply paths

Bodies / Discord posts / `!ask` still use `String`. Tracked users + guild IDs done in 0.7.36. More only if heap pressure shows on those paths.

### 3 — Architectural (not this release)

LCD vs web layouts are independent (no `/api/ui` sync).
