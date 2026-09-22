# MiniMe II — code review notes (pro pass)

What we **fixed** vs what we **left** and why. Current: **v0.7.17**.

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
| Loop stack override | **0.7.14** — `ARDUINO_LOOP_STACK_SIZE` before `Arduino.h` + strong `getArduinoLoopTaskStackSize()` (0.7.11/0.7.13 were no-ops on core 3.x); verify HWM log |
| Heap vs PSRAM | **0.7.13** — bar/`heap*` = internal; PSRAM separate; **0.7.14** LCD PSRAM row |
| `MINIME_USER_AGENT` | **0.7.13** — single `#define` |
| Members JSON on stack | **0.7.13** — heap `DynamicJsonDocument*` for guild members |
| Chunked body truncate ≠ success | **0.7.15** — trailer/size/mid-chunk/`Content-Length` incomplete → `false` |
| Large JSON in PSRAM | **0.7.17** — `gwDoc` / `statusDoc` / `deepSeekDoc` via shared `SpiRamJsonDocument` |

## Still deferred (why)

### 1 — Discord 429 / Retry-After

`sendDiscordMessage` uses status line only. Hobby-acceptable.

### 2 — ArduinoJson 6

Pinned; v7 later.

### 3 — Explicit TWDT register/reset

Rely on Arduino-ESP32 default + `delay`/`yield`. Documented.

### 4 — CI compile-only / no HIL

Honest in README.

### 5 — Delete nested `gwPumping` HB-only path

Plan: keep until a week of stable Gateway after dual-core, then prune.

### 6 — Remaining `String` on cold paths

Command handlers / HTTPS body buffers / tracked user IDs still use `String`. Acceptable until heap pressure shows up on those paths.

### 7 — MmLog / webLogFeed cross-core

Rings have no mutex. **0.7.16:** `webLogFeed` drops if `xPortGetCoreID() != 1`. Still do not call MmLog from Core 0 for intentional logs.
