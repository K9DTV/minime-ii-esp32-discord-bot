# MiniMe II — code review notes (pro pass)

What we **fixed** vs what we **left** and why. Current: **v0.7.7**.

## Fixed (through dual-core / fetch pumps)

| Item | When / what |
|---|---|
| Dual-core UI vs Gateway | 0.7.0+ — Core 0 `uiTask` LCD/touch; Core 1 net/Discord |
| Command queue | 0.7.0 — `MESSAGE_CREATE` enqueue; `drainDiscordCmds` / `handleCommand` on Core 1 |
| Mid-draw `pumpGateway` | 0.7.0 — removed; QSPI no longer on HB path |
| DS18B20 stall on Core 1 | 0.7.1/0.7.2 — Core 0 `pollTemperatureNonBlocking` only |
| DashSnap sync | 0.7.1 — seqlock + 1 s publish throttle |
| Fetch body HB pumps | 0.7.6 — weather / news / APOD / ISS / arXiv use `readHttpBodyAfterHeaders` |
| `*GetOpen` returns chunked/CL | **0.7.7** — body reader no longer guesses “until close” |
| `showTransient` duration | 0.7.3 — honors `durationMs`; `!display` 6 s |
| Boot `gwDoc` / `statusDoc` | 0.7.4–0.7.5 — alloc fail → halt; `gwDoc` checked before Canvas |
| Nested `pumpGateway` HB-only | 0.6.4 — still kept for HTTPS-from-wait paths |
| OTA / HTTPS caps / touch timeout / Bot:N | 0.6.4–0.6.7 |

## Still deferred (why)

### 1 — `String` on hot paths

**Can be fixed** (fixed `char[]` / `serializeJson` to `Print` / chunked web sends). Not done: multi-file rewrite. Watch `heapFree` before investing.

### 2 — Discord 429 / Retry-After

`sendDiscordMessage` uses status line only. Hobby-acceptable.

### 3 — ArduinoJson 6

Pinned; v7 later.

### 4 — Explicit TWDT register/reset

Rely on Arduino-ESP32 default + `delay`/`yield`. Documented.

### 5 — CI compile-only / no HIL

Honest in README.

### 6 — Delete nested `gwPumping` HB-only path

Plan: keep until a week of stable Gateway after dual-core, then prune.
