# Changelog

Older sections are append-only history (as written when that release shipped). Current firmware is **0.7.48** (see `VERSION` and README).

## 0.7.48

- Scratch TWDT proof: optional `#define MINIME_TEST_TWDT` enables owner `!hang` (infinite `delay` loop so Core 1 stops feeding the 90 s loop TWDT). Procedure in [`docs/soak-results.md`](docs/soak-results.md). Keep define **off** for normal flashes. Confirm flash via `Display · v0.7.48`.

## 0.7.47

- Rename `gwArmFastIdentify` -> `gwBeginDropEpisode` (clears session + resets fail count; interval is backoff-owned). Document reconnect state invariants above the climb constants. Confirm flash via `Display · v0.7.47`.

## 0.7.46

- Gateway reconnect climb: after a drop, **3** fast tries (200 ms), then **3 s → 7 s → 12 s**, then +8 s steps capped at **40 s** (was forever-capped at 5 s / sticky 200 ms on wifi-up). Resets on READY/RESUMED. Confirm flash via `Display · v0.7.46`.

## 0.7.45

- CI Python: `tools/test_cmd_tokenize.py` (tokenize / `CMD_CONSUMES_REST`, sync-checked against `commands.cpp`) and `tools/test_body_reader.py` (FakeClient mirror of `readHttpBodyAfterHeaders` — incomplete CL / missing 0-chunk / mid-chunk / cap => false). Host mirrors: `cmd_tokenize.py`, `http_body_reader.py`. Confirm flash via `Display · v0.7.45`.

## 0.7.44

- `commands.cpp`: dispatch table (`kCmds`) with `CMD_CONSUMES_REST` / `CMD_OWNER` / `CMD_RECORD_USE` — tokenize mid-line rest rule and owner/record gates share one table; handlers are thin `cmd*` functions. `tokenizeCommand` returns the `CmdEntry*` so dispatch does not re-scan. Behavior unchanged: owner gate still runs before `recordUserUse`; unknown commands still reply + transient without recording; usage errors inside handlers still run after `recordUserUse` for `CMD_RECORD_USE` cmds (same as pre-refactor). Confirm flash via `Display · v0.7.44`.

## 0.7.43

- TWDT try (different from 0.7.30/0.7.34): after setup, `esp_task_wdt_reconfigure` to **90 s** + `enableLoopWDT` on Core 1 `loopTask` only. No `uiTask` subscribe, no mid-HTTPS `esp_task_wdt_reset`. Soft Discord guard remains HB ack; TWDT is stuck-loop backstop. Confirm flash via `Display · v0.7.43`. If panic: lines above `ELF file SHA256` or `!coredump`.
- `!ask`: handshake 15 s / body deadline 30 s. `sendDiscordMessage`: 60 s wall budget (release + fail) so 429 stacking cannot outrun the 90 s TWDT.
- **HIL attestation (2026-09-22/23):** multi-hour lan-monitor soak + `!ask` hammer 23:10–00:16 PDT — no Discord drops, no TWDT panic, ~8 s total `GW_DOWN` (update reboot 3 s + brief 5 s). Details: [`docs/soak-results.md`](docs/soak-results.md).

## 0.7.42

- CI badges split further: **Python** (pytest + Pillow; playwright in `requirements-logo.txt`) and **HTML** (`tools/ci_html.py` for LAN CSS/JS/SVG headers). Sanity + Compile unchanged. Confirm flash via `Display · v0.7.42`.

## 0.7.41

- CI beyond compile-only: `tools/ci_sanity.py` (VERSION sync, one `.ino`, no AJ6 types, secrets not tracked) then compile. Local HIL playbook: `docs/HIL_SOAK.md` + tracked `docs/lan-monitor.ps1` (`-BaseUrl` / `MINIME_LAN`). Confirm flash via `Display · v0.7.41`.

## 0.7.40

- Prune nested `gwPumping` HB-only path: re-entry is a no-op (dual-core cmds no longer run inside `gatewayWS.loop`). Keep `gwPumping` for presence defer + Wi-Fi kick gate. Confirm flash via `Display · v0.7.40`.

## 0.7.39

- Core 0 MmLog bridge: enqueue lines to Core 1 (`drainCore0Logs`); Serial shows `[C0] …`. Ring overflow still counted as `mmLogDropCore0`.
- DS18B20 disconnect logs via bridge (≤1/min). Confirm flash via `Display · v0.7.39`.

## 0.7.38

- `!ask` HOL only (no TWDT): DeepSeek dedicated TLS; `pumpNetWait` drains only when `!httpsInUse`; `DrainBusyGuard` RAII.
- Confirm flash via `Display · v0.7.38`. If panic: lines above `ELF file SHA256` or `!coredump`.

## 0.7.37

- `sendDiscordMessage`: retry on HTTPS header timeout (same attempt budget as 429; 500 ms backoff + Gateway pump). Confirm flash via `Display · v0.7.37`.

## 0.7.36

- ArduinoJson **6 → 7**: `JsonDocument` + `SpiRamAllocator` (no `Static`/`Dynamic`/`BasicJsonDocument`); `to<>` / `add<>` instead of `createNested*`. CI pins `ArduinoJson@7.4.2`.
- Cold-path `String` cut: `TrackedUser` id/name and cached guild IDs are fixed `char[]` (no heap churn per presence/slot).
- Confirm flash via `Display · v0.7.36`. TWDT / `!ask` HOL unchanged (still unrolled).

## 0.7.35

- Unroll 0.7.34 again after crash: drop explicit TWDT, mid-TLS drain, dedicated DeepSeek TLS (same shape as 0.7.33).
- Kept: LCD `Ver`, cmd-error ring, `httpsAwaitHeaders(Client&)`. Confirm flash via `Display · v0.7.35`.

## 0.7.34

- Retry 0.7.30 features with safer gates: DeepSeek dedicated TLS again; TWDT add/reset (loop + uiTask around paint + `pumpNetWait`).
- Drain during TLS waits only when `!httpsInUse` (no mid-shared-fetch busy spam); `DrainBusyGuard` RAII for nested drain flag.
- Confirm flash via `Display · v0.7.34`. If panic: capture lines above `ELF file SHA256` or `!coredump`.

## 0.7.33

- Unroll 0.7.30 risk set after `RTC_SW_CPU_RST` / ELF SHA panic (no header captured): drop explicit TWDT add/reset, mid-TLS `drainDiscordCmds`, dedicated DeepSeek TLS (back on shared `httpsClient` + `httpsInUse` queue stall).
- Kept: LCD `Ver`, cmd-error ring, `httpsAwaitHeaders(Client&)`. Confirm flash via `Display · v0.7.33`.

## 0.7.32

- Docs: renumber deferred list; record mid-fetch **busy** tradeoff + `drainCmdsBusy` note (0.7.30 review).
- Clarify 0.7.30 HOL: queue no longer stalls; shared-client cmds drained during an in-flight fetch get a fast busy reply (see `[CMDERR]`). Confirm flash via `Display · v0.7.32`.

## 0.7.31

- Cmd-error ring (10): Discord failure replies (`busy`, fetch/sensor errors, post fail) via `sendDiscordCmdError` / `noteCmdErrorReply`.
- Visible on `!sys` (newest 5) and LCD Log→Serial as `[CMDERR] …`. Confirm flash via `Display · v0.7.31`.

## 0.7.30

- TWDT: explicit `esp_task_wdt_add`/`reset` on loop (Core 1) and `uiTask` (Core 0); resets in HTTPS wait pumps.
- `!ask` HOL: DeepSeek uses its own TLS client (not `httpsInUse`); drain cmds during TLS waits; queue no longer stalls on busy HTTPS.
- LCD: `Ver` row shows `MINIME_VERSION`. Confirm flash via `Display · v0.7.30`.

## 0.7.29

- Owner `!coredump`: read flash coredump partition (0xFD0000 / 0x30000) — task, PC, ExcCause, backtrace, panic reason; `!coredump clear` erases. Confirm flash via `Display · v0.7.29`.

## 0.7.28

- `connectWiFi` moved to `wifi_connect.cpp` (`.ino` is setup/loop only).
- README: only one `.ino` in the sketch folder (Arduino merges all; second `.ino` => redefinition). Confirm flash via `Display · v0.7.28`.

## 0.7.27

- Loop stack: drop sketch `getArduinoLoopTaskStackSize()` — ESP32 core 3.3+ already defines it from `ARDUINO_LOOP_STACK_SIZE` (was redefinition). Confirm flash via `Display · v0.7.27`.

## 0.7.26

- Org split: `command_fetch.cpp` (API/DeepSeek) vs `commands.cpp` (tokenize/dispatch).
- Org split: LCD → `display.cpp` + `display_overlay.cpp` + `dash_snap.cpp` + `display_draw.cpp` (+ `display_internal.h`).
- Gateway JSON filter: file-scope `gwFilter`, built in `connectGateway` (not on first TEXT). Confirm flash via `Display · v0.7.26`.

## 0.7.25

- Event sticky: `showTransient` no longer overwrites `lastEventLine` (Gateway/`noteLastEvent` only).
- Wi‑Fi: `ensureWifiForGateway` skips reconnect while `gwPumping` or `httpsInUse` (no mid‑TLS disconnect).
- Users: `recordUserUse` no longer forces status On; Discord presence owns Online/Idle/DND/Off.
- CPU: `updateBotPresenceIdle` drops to 160 MHz even if Gateway never identified.
- HTTP body: block reads + `String::reserve`/`concat` (chunked/CL/until‑close) instead of per‑byte `+=`.
- NTP: DST offset recompute at most once per 60 s (1 Hz dash no longer zeros offset every tick).
- Filter note: `gwFilter["d"]["status"]` already present for `PRESENCE_UPDATE`. Confirm flash via `Display · v0.7.25`.

## 0.7.24

- Temp: `dashTempStore` / `dashTempSnapshot` under one mux (C/F + timestamp together).
- Transient until: expire under same overlay mux (no clear-vs-set race).
- FULL LOG match requires `[GW] === …` prefix; Core0 drop count after null/size check; counter lives in `web_ui.cpp`.
- `/api/status`: `measureJson` vs 48 KB ceiling before send. Confirm flash via `Display · v0.7.24`.

## 0.7.23

- Cross-core UI overlay: transient/Event are fixed `char[]` + `portMUX` (no torn `String`); DM/Mention atomic.
- FULL LOG markers match via `strstr`; Core 0 `MmLog` drops counted (`!sys` / status JSON).
- `!temp` / LCD / web: sample stale if >30 s. `statusDoc` 48 KB. Confirm flash via `Display · v0.7.23`.

## 0.7.22

- LCD left window: same row order as web metrics, and each meter is one row (label | value | bar) like web `mline` — Sig, PSRAM, SRAM, Srv, then Up/T …. Confirm flash via `Display · v0.7.22`.

## 0.7.21

- Web subtitle: **MiniMe-II A Discord Bot** (was "MiniMe A Discord Server APP"). LCD left panel matches web order + `MiniMe-II` header + LCD awake/asleep sys row. Confirm flash via `Display · v0.7.21`.

## 0.7.20

- LCD/web meters under Sig: **PSRAM** bar, **SRAM** bar, then **Srv** (Up/temp below). PSRAM uses same free/total linear fill as SRAM. Confirm flash via `Display · v0.7.20`.

## 0.7.19

- 429: drop duplicate macros from `discord_rest.cpp` (config header is sole source); ignore non-numeric `Retry-After` (keep `-1` so JSON body parse can run). Confirm flash via `Display · v0.7.19`.

## 0.7.18

- Discord REST 429: `sendDiscordMessage` honors `Retry-After` (header, else JSON `retry_after`), waits with `pumpGateway`, up to 3 attempts (0.5–60 s clamp). Confirm flash via `Display · v0.7.18`.

## 0.7.17

- `gwDoc` (256 KB) and `statusDoc` (32 KB): `SpiRamJsonDocument` / `SpiRamAllocator` (same pattern as `!ask`). Was default `DynamicJsonDocument` → internal SRAM (boot heapPct ~93). Confirm flash via `Display · v0.7.17`.

## 0.7.16

- Chunked body: bound empty size-line skips (CDN bare-CRLF quirk); still require final 0-chunk.
- `webLogFeed`: drop if `xPortGetCoreID() != 1` (ring vs `handleStatus`).
- Until-close body path: documented best-effort (callers validate JSON). Confirm flash via `Display · v0.7.16`.

## 0.7.15

- `readHttpBodyAfterHeaders` chunked: trailer / size-line / mid-chunk failure returns **false** (no partial-body success). Content-Length incomplete also **false**.
- Loop stack: `static_assert(ARDUINO_LOOP_STACK_SIZE == 16384)` next to strong override.
- Document: `webLogFeed` / MmLog Core-1-only (no ring mutex vs `handleStatus`). Confirm flash via `Display · v0.7.15`.

## 0.7.14

- Loop stack (Arduino-ESP32 3.x): `ARDUINO_LOOP_STACK_SIZE 16384` before `Arduino.h` in `minime.h`, plus strong `getArduinoLoopTaskStackSize()` in the `.ino`. 0.7.13's `SET_LOOP_TASK_STACK_SIZE` after `minime.h` was a no-op on 3.x (same class of miss as 0.7.11). Confirm via `[SYS] loop stack free HWM` (16 KB => often >~2000 words remaining after setup).
- LCD: PSRAM free/total row on left panel (DashSnap + `boardPsramTotals`) to match web/`!sys`.
- `!ask`: after `SpiRamJsonDocument` alloc, treat `capacity()==0` as out of memory (not "JSON parse NoMemory").
- Confirm flash via `Display · v0.7.14`.

## 0.7.13

- Loop stack: documented `SET_LOOP_TASK_STACK_SIZE(16*1024)` after `minime.h`; one-shot `[SYS] loop stack free HWM` log. (**Superseded by 0.7.14** — that macro after Arduino.h does not raise the stack on ESP32 core 3.x.)
- `!ask`: PSRAM `BasicJsonDocument` + `nothrow`; parse from `c_str()+offset` (no substring copy).
- Heap bar / `!sys`: **internal** heap only; separate PSRAM lines / JSON fields.
- `MINIME_USER_AGENT` single source. `appendMembersFromGuild` doc off stack. Confirm flash via `Display · v0.7.13`.

## 0.7.12

- LOG vs Serial restored: Serial = normal MmLog; LOG = body between `[GW] === FULL LOG ===` / `END LOG` (60 s drop-ring dump; clear on each dump start). Confirm flash via `Display · v0.7.12`.

## 0.7.11

- `!ask`: DeepSeek parse doc is heap/PSRAM `DynamicJsonDocument` (no 24 KB stack `StaticJsonDocument`).
- Core 1 loop stack: `getArduinoLoopTaskStackSize()` **before** any `#include` (16 KB strong override).
- `boardMemTotals`: real `ESP.getPsramSize()` / `getFreePsram()` (no fake 8 MB).
- `httpGetOpen`: `User-Agent: MiniMeBot/1.0` (OWM / ISS). Confirm flash via `Display · v0.7.11`.

## 0.7.10

- LOG panel: MmLog lines feed rolling `webFullLines` again (empty after full-dump opt-out).
- `!sys`: drop IP / OTA host (use owner `!ota` for those).
- `!ota` owner-only in README (matches code/help). `!led` handler removed.
- Web `esc()`: numeric `0` no longer becomes empty (`s||''` bug).
- `ChunkPrint::write` bulk `memcpy`. Confirm flash via `Display · v0.7.10`.

## 0.7.9

- Hot-path `String` reduction: `formatLocalTimeStr`; LCD snap/draw use `char[]`; Gateway log ring is fixed `char[][]`; LAN `/` and `/api/status` stream via chunked `Print` (`serializeJson` to `ChunkPrint`, no 25 KB JSON `String`).
- Confirm flash via `Display · v0.7.9`.

## 0.7.8

- Gateway TLS: `beginSslWithBundle` + ESP32 CA blob (plain `beginSSL` was `setInsecure`).
- `readHttpBodyAfterHeaders`: truncation / 48 KB cap returns **false** (not success).
- Presence: unknown users no longer evict tracked slots; `recordUserUse` after owner check on `!led`/`!servo`/`!clear`.
- `!sysinfo` firmware URL → `K9DTV/minime-ii-esp32-discord-bot`.
- DashSnap: static buffers + Core 1 loop stack 16 KB; seqlock retry `taskYIELD`.
- Cross-core: `displayAsleep` / `lastDisplayActivityMillis` / `dashForceFull` are `std::atomic`.
- `MINIME_VERSION` single source; web header uses it. GW alive 60 s; full-log dump opt-in (`GW_DEBUG_FULL_LOG_DUMP`).
- Confirm flash via `Display · v0.7.8`.

## 0.7.7

- `httpsGetOpen` / `httpGetOpen` return `chunked` + `Content-Length` for pumped body reads (no more “until close” guess). Confirm flash via `Display · v0.7.7`.

## 0.7.6

- Weather / science news / APOD / ISS / arXiv: body read via `readHttpBodyAfterHeaders` (Gateway HB pumps); removed arXiv `readString()` stall.
- `docs/CODE_REVIEW_NOTES.md` refreshed for dual-core + fetch pumps. Confirm flash via `Display · v0.7.6`.

## 0.7.5

- Boot: check `gwDoc` alloc immediately after `new` (before `setupDisplay` / Canvas). Confirm flash via `Display · v0.7.5`.

## 0.7.4

- Boot: `statusDoc` alloc failure halts like `gwDoc` (same Fatal / power-cycle policy). Confirm flash via `Display · v0.7.4`.

## 0.7.3

- `showTransient` honors `durationMs` (`!display` stays **6 s** as documented); web flash includes `transientLine3`.
- LOG ring: drop oldest until 20 KB budget fits (no full wipe on overflow).
- Boot: halt with `Fatal` / MmLog if `gwDoc` alloc fails.
- Docs/comments: sticky DM/Mention until `!clear`; nested Wi-Fi kick note. Confirm flash via `Display · v0.7.3`.

## 0.7.2

- Removed blocking `readTemperature` (shared OneWire footgun vs Core 0 `pollTemperatureNonBlocking`). Temp is Core-0-only poll + cached `dashTempC`/`F`. Confirm flash via `Display · v0.7.2`.

## 0.7.1

- DS18B20: non-blocking poll on Core 0 (`pollTemperatureNonBlocking`); Core 1 `publishDashSnap` / `!temp` no longer block ~750 ms on conversion.
- DashSnap publish: seqlock + 1 s throttle (no `portENTER_CRITICAL` over ~4KB copy).
- README: dual-core scope clarified (LCD/QSPI fixed; long HTTPS fetches still on Core 1); backlight idle timer / Idle CPU docs corrected; LCD theme independent of web.
- `uiTask` create failure logs via MmLog. Confirm flash via `Display · v0.7.1`.

## 0.7.0

- Dual-core: Core 1 = Gateway / HTTPS / OTA / web / command drain; Core 0 `uiTask` = LCD + touch.
- `DashSnap` published under mutex; Core 0 paints only (no mid-draw `pumpGateway`).
- `MESSAGE_CREATE` enqueues; `drainDiscordCmds()` runs `handleCommand` from `loop()`.
- Confirm flash via `Display · v0.7.0`.

## 0.6.10

- Display/Log: web and LCD independent (same as Light/Dark). Removed `/api/ui` layout sync.
- LCD backlight idle **5 minutes** (`DISPLAY_IDLE_MS`).
- Discord Idle → CPU **160 MHz**; Online/activity/OTA → **240 MHz**. Confirm flash via `Display · v0.6.10`.

## 0.6.9

- Light/dark: web and LCD are **independent** (web = browser; LCD = glass chip). Layout Display/Log still syncs.
- LCD light background fixed to true gray `#dde2ea` RGB565 `0xDF1D` (was `0xDEF5`, which looked warm/brown). Confirm flash via `Display · v0.6.9`.

## 0.6.8

- LCD light theme uses bright K9DTV RGB565 logo (`K9DTV_LOGO_BRIGHT_RGB565`); regenerate via `tools/gen_k9dtv_logo_rgb565.py`.
- Web theme/layout sync with LCD: `/api/ui?theme=&layout=`; status carries `theme` / `layout` (chip on either side updates both).
- Web layout matches LCD pairing: **Display** = metrics|users; **Log** = LOG|Serial (logo + chips kept). Confirm flash via `Display · v0.6.8`.

## 0.6.7

- All Discord-reply commands: LCD shows value only if `sendDiscordMessage` succeeds (`showIfPosted`); else `Post fail` (matches fetch / !help).
- Bot:N: `recordUserUse` only inside known-command branches (no separate `isTrackedBotCommand` list). Confirm flash via `Display · v0.6.7`.

## 0.6.6

- HTTPS: `contentLength` maxBody path `stop()`s like chunked; `readHttpLineCapped` false on peer close mid-line.
- Touch: `Wire.setTimeOut(50)` so I2C stall cannot starve Gateway indefinitely.
- `sendFetchResult` / `!help`: LCD “Sent” only if Discord post succeeds; else “Post fail”.
- Bot:N: do not count `!help` / unknown commands.
- `sendDiscordMessage`: comment — status line only, body discarded.
- Secrets example: `MINIME_SECRETS_IS_EXAMPLE` + compile `#error` until removed in real `secrets.h`.
- Docs: watchdog policy, activity stamp while disconnected, `docs/CODE_REVIEW_NOTES.md` updated. Confirm flash via `Display · v0.6.6`.

## 0.6.5

- Prune hobby leftovers: empty `MmLog::flushAll`, `mmSerialCdcOnBoot` wrapper, `webUiKeepsCpuActive`, vestigial `backgroundTasks` (loop calls `runAskFromLoop` directly).
- HTTP open errors: **1**=busy, **2**=header timeout, **3**=connect/TLS/DNS (was busy+connect collapsed).
- Clarify `noteBotActivity` / `noteDisplayActivity` / `noteLastEvent` in `minime.h`. Confirm flash via `Display · v0.6.5`.

## 0.6.4

- Gateway: nested `pumpGateway()` no longer re-enters `gatewayWS.loop()` (HB keep-alive only); defer Online presence send until outer pump ends.
- OTA `onError`: restore reconnect interval (failed flash no longer parks Gateway for 1 hour).
- HTTPS: capped header/chunk lines (512); chunked body no longer returns mid-chunk without draining/stop.
- Remove dead `content` null check; drop unused `lastSysInfoMillis` boot hack.
- Docs: `docs/CODE_REVIEW_NOTES.md` (fixed vs deferred from pro review). Confirm flash via `Display · v0.6.4`.

## 0.6.3

- Left header: MiniMe | centered GW | right-justified time; Bot | right-justified date; Sig with RSSI beside label.
- Users panel: **9 px** row pitch, **MAX_TRACKED_USERS=24**.
- Dual IC chips (k9dtv menu-chip): **left** Light/Dark; **right** Display/Log.
  Display = left metrics + right users; Log = left LOG + right Serial (same windows).
- Dirty redraw: skip logo/chips and right panel when unchanged; skip flush when nothing changed.
- Confirm flash via `Display · v0.6.3`.

## 0.6.2

- Left LCD panel: RSSI number, heap free/total, Id, Users n/20, **DM + Mention** on one line, HTTPS busy/idle, sticky **Event** line. No footer strip under the panels.
- Removed **USB VBUS** and **`!set1` / `!set2`** from firmware and docs. DM/@mention are LCD alert flags; owner `!clear` clears them.
- Confirm flash via `Display · v0.6.2`.

## 0.6.1

- Display: drop **JC3248W535EN-Touch-LCD**; use **Arduino_GFX** directly (`Arduino_ESP32QSPI` + `Arduino_AXS15231B` + `Arduino_Canvas`, landscape rotation 1).
- Touch wake: AXS15231B I2C (`0x3B`, SDA 4 / SCL 8 / INT 3) in-sketch; no wrapper IRQ API.
- LCD refresh **1 s** (measured ~48 ms flush / ~62 ms total). Confirm flash via `Display · v0.6.1`.

## 0.6.0

- Hardware fork **MiniMe II**: Guition **JC3248W535EN** (AXS15231B QSPI LCD + in-cell touch).
- Display: **JC3248W535EN-Touch-LCD** (Arduino_GFX). Same dashboard fields as MiniMe I; backlight off after idle; touch IRQ wakes panel only.
- Removed SSD1327 / U8g2 and ESP32 GPIO capacitive touch + VBUS touch compensation. NeoPixel/servo pins remapped off QSPI (16 / 17).
- Confirm flash via `Display · v0.6.0`.

## 0.5.3

- Fix compile on Arduino-ESP32 core **3.3.x**: HTTPS uses `useBuiltinCACertBundle()` only on **3.3.12+** (CI). Older IDE cores call `setCACertBundle(start, end - start)` with `_binary_x509_crt_bundle_*` (not PlatformIO `_binary_data_crt_*`, which CI does not link). Confirm flash via `Display · v0.5.3`.

## 0.5.2

- HTTPS REST: CA cert bundle again (no `setInsecure`); Discord bot token and API keys verified TLS.
- OLED uptime row: `Up:xxd xxh xxm T:xxxF/xxxC` (**no** seconds on OLED; web SysInfo still shows `d h m s`). OLED refresh **4 s**; LAN web `/api/status` poll **2 s**.
- Shared `formatLocalDateStr` / `formatUptimeStr`; command tokenizer with `cmdConsumesRest`.
- DeepSeek JSON buffer **24576**; removed scrape fallback. Science news filter/doc sized for SNAPI v4 `results`.
- Removed no-op `applyCpuForIdleState` (Discord Idle after 5 min quiet unchanged). `WIFI_PS_NONE` + HB ack grace kept.
- LAN CSS + boot/app JS in `web_assets.h`; `/api/status` ArduinoJson with once-allocated status doc. Static status JSON reuse; `yield()` in `loop()`.
- Docs: README no longer claims boot `!sys`/`!help` auto-posts. Gateway: delete unused resume helpers (`sendResume` / resume host parse); identify-only after drops.
- Note vs older changelog lines: set1/set2 remain **steady HIGH** in current code (not 1 Hz / 10 Hz flash). Confirm flash via `Display · v0.5.2`.

## 0.5.1

- CPU locked at **240 MHz** (no idle downclock to 80 MHz when OLED blank + Discord Idle). Test for unexplained full-chip resets. Confirm flash via `Display · v0.5.1`.

## 0.5.00

- LAN web UI: k9dtv.com light/dark theme (local assets, `k9-theme`, OS default when unset); IC chip toggle with sun/moon + target label; sticky click focus cleared.
- LAN web UI: second IC chip for **Display** (all four panels) vs **Log** (Display + SysInfo only); preference in `mm-layout`.
- Discord: public `!sys` and `!ota` (ArduinoOTA IP / hostname / port 3232); uptime as `d h m s`; set1/set2 flash at 1 Hz 50%.
- LAN web UI on port **80** (`http://<board-ip>/`). Confirm flash via `Display · v0.5.00`.
- Gateway: skip resume (never worked here); after OP7/OP9/disconnect use 200 ms IDENTIFY reconnect (no 5 s climb). No boot channel `!sys` / `!help` posts.

## 0.4.95

- LAN web UI back on port **80** (`http://<board-ip>/`). Dropped the 8080 workaround. Confirm flash via `Display · v0.4.95`.

## 0.4.94

- LAN web UI briefly on port 8080 (Chrome HTTPS auto-upgrade workaround); reverted in 0.4.95.

## 0.4.93

- LAN web UI back to plain HTTP on port 80 (self-signed HTTPS removed; browser warning was not useful trust). Confirm flash via `Display · v0.4.93`.
- CI: compile FQBN uses `PartitionScheme=custom` so GitHub builds against sketch `partitions.csv` (not the default 1.25MB APP limit that failed on v0.4.92).

## 0.4.92

- LAN web UI: HTTPS on port 443 (self-signed); HTTP :80 redirects to HTTPS. Discord outbound TLS back to `setInsecure` / `beginSSL` (no CA verify). Confirm flash via `Display · v0.4.92`.

## 0.4.91

- HTTPS REST: verify server certificates with the ESP32 CA cert bundle (removed `setInsecure()`). Confirm flash via `Display · v0.4.91`.

## 0.4.90

- Fix: LAN **LOG** panel clears when content would exceed **20 KB** (was able to grow without a hard size wipe). SysInfo uptime spacing `Nd Nh Nm`. Confirm flash via `Display · v0.4.90`.

## 0.4.89

- Shipped breadboard firmware (no known bugs). Idle CPU **80 MHz** (ESP32-S3 has no 100 MHz step). Channel auto reports off; boot posts sysinfo + help once only. Confirm flash via `Display · v0.4.89`.

## 0.4.88

- Gateway reconnect: cap interval at 5s (no climb to 60s); reset backoff on OP9 so identify retry is not delayed. Best-effort under ~30s offline when Discord answers. Confirm flash via `Display · v0.4.88`.

## 0.4.87

- Web Serial panel: fixed 12-line ring (oldest drops off the top, new line at bottom); no scrollbar. Confirm flash via `Display · v0.4.87`.

## 0.4.86

- Gateway: save `resume_gateway_url` from READY and reconnect to that host after OP7 / resume paths (not always `gateway.discord.gg`), to cut OP9 invalid-session after Discord reconnect.

## 0.4.85

- Web Display: `.dash` gets `min-width:0;overflow:hidden` so meter `1fr` bars cannot spill past the Display panel. Confirm flash via `Display · v0.4.85`.

## 0.4.84

- Web Display meters: bar column is remaining width (`1fr`), not fixed `9rem`, so bars stop at the Display panel edge. Confirm flash via `Display · v0.4.84`.

## 0.4.83

- Web Display meters: value column `10ch` -> `7ch` so bars sit ~3 chars left. Confirm flash via `Display · v0.4.83`.

## 0.4.82

- Web Display meters: restore first-page fixed `9rem` `.bar` + original `bar()` fill. Each row is `label | 10ch value | bar` so Srv bar left edge matches Heap (and Sig). Confirm flash via `Display · v0.4.82`.

## 0.4.81

- Fix (on us): meters HTML is built on the ESP (not JS grid). Every bar track is `position:absolute;left:148px` so value length cannot shift bar starts. Confirm flash via `Display · v0.4.81`.

## 0.4.80

- Fix (on us): 0.4.78 table CSS did not lock bar columns. Sig/Heap/Srv now use one inline `display:grid` with columns `40px | 100px | 1fr` so all bar left edges match. Confirm flash via `Display · v0.4.80`.

## 0.4.79

- Web Display meters use absolute pixel layout: label at 0, value at 40px, every bar starts at 140px. Subtitle and Display header show `v0.4.79`.

## 0.4.78

- Web Display Sig/Heap/Srv use a fixed-layout HTML table so bar left edges share one column. Page subtitle shows `v0.4.78` so a successful flash is obvious.

## 0.4.77

- Web Display: each Sig/Heap/Srv row is its own identical grid (`label | 7rem value | bar`) so all three bar left edges match Sig.

## 0.4.76

- Web Display Sig/Heap/Srv values left-justified in the number column.

## 0.4.75

- Web Display meters: label | number (left) | bar (right); fixed value column keeps Sig/Heap/Srv bars aligned.

## 0.4.74

- Web UI: `sendNoCacheHeaders()` on `/` and `/api/status` plus HTML cache meta so browsers do not keep a stale page.

## 0.4.73

- Web Display meters left-justified: label | bar | value (Sig/Heap/Srv bars share left edge).

## 0.4.72

- Web Display Sig/Heap/Srv use one CSS grid (label | fixed value col | bar) so all three bars share the same left edge.

## 0.4.71

- Web Display meters: bar first (shared left edge after label), value on the right -- Heap/Srv/Sig bars align.

## 0.4.70

- Web Display: Sig/Heap/Srv meter bars share one left edge (fixed-width value column). OLED bar layout unchanged from pre-0.4.69. Idle CPU remains 100 MHz.

## 0.4.69

- Idle CPU (OLED off + Discord Idle, web not holding CPU) is 100 MHz.

## 0.4.68

- DM to bot flashes set1 at 10 Hz; @OWNER_ID mention flashes set2 at 10 Hz. Owner `!clear` stops both and forces OFF. `!set1`/`!set2` stop that pin's flash.

## 0.4.67

- USB Serial fully quiet: no `Serial.begin` / no MmLog to the port. All MmLog still goes to the web panels via `webLogFeed`. Copy `serial_log.cpp` when flashing.

## 0.4.66

- Serial panel back beside LOG; same box height; Serial has no scrollbar; Serial lines capped to LOG line count. USB Serial port still quiet.

## 0.4.65

- MmLog no longer writes to the USB Serial port; same lines still go to the web LOG via `webLogFeed`. Web UI unchanged.

## 0.4.64

- Kill Serial panel. Same MmLog stream still feeds the web LOG panel (FULL/END headers stripped). Layout: Display|SysInfo, LOG full width under both.

## 0.4.63

- Serial keeps its own live MmLog feed again (not cleared/clipped when LOG is empty). Same max depth as LOG.

## 0.4.62

- Rename system panel to SysInfo. LOG and Serial sit under Display+SysInfo. Serial line count capped to LOG line count.

## 0.4.61

- Layout: Display left, system Log right; under Display LOG then Serial. LOG = body between `[GW] === FULL LOG ===` and `END LOG` (headers omitted). Serial = all other MmLog lines.

## 0.4.60

- Web layout: centered logo (opens k9dtv.com), subtitle MiniMe A Discord Server APP; system Log left of Display; LOG (serial ring) under Display with Serial to its right (MmLog only).

## 0.4.59

- Web UI one page: K9DTV logo + Display + system Log + Serial(5). Separate `/log` removed.

## 0.4.58

- Full K9DTV logo restored; Display again includes tracked users. Two pages kept (`/` + `/log`). Use a ~4MB app partition if link overflows.

## 0.4.57

- Web UI back to two pages: `/` Display + Serial(5) with logo; `/log` system Log panel (opens in new window). Compact logo kept.

## 0.4.56

- Shrink web UI flash use: compact logo SVG + leaner CSS so the app fits the board text section.

## 0.4.55

- Web UI one page: Display (no tracked users), **Log** system panel restored, **Serial** shows only 5 live lines (RAM ring, no log file).

## 0.4.54

- Web UI: static K9DTV logo at top (`/logo.svg`, cached; not part of status refresh).

## 0.4.53

- Web UI: one page with display + live **Log** (serial/MmLog lines). Removed separate /long window.

## 0.4.52

- Keep CPU at 240 MHz while LAN web UI is up so OLED sleep / Discord Idle no longer drops Wi-Fi and kills the browser page.

## 0.4.51

- Web UI formatting: OLED-style label/value rows, meter bars, user columns; long-data page uses a system grid + user table.

## 0.4.50

- Web UI: display box only (fixed status JSON load); removed serial log panel and footer note; **Long data** opens `/long` in a new window.

## 0.4.49

- LAN web UI on port 80: OLED-style dashboard box (same fields, not pixels) plus a live 5-line serial log box (`web_ui.cpp`).

## 0.4.40

- README: status line under the photo; OLED and touch deep dives in `<details>`; **Why this is hard** bullets after How it works.

## 0.4.39

- README: hint under secrets fill-in to expand the Discord/API setup `<details>` sections.

## 0.4.38

- README: short **How it works** architecture diagram (Gateway / REST / OLED / touch) as Mermaid, placed after **What this bot can do**.

## 0.4.37

- GitHub Actions compile check for ESP32-S3 (OPI PSRAM, 16MB flash) via `arduino-cli`; README build badge. CI compiles only (does not prove board/Discord).

## 0.4.36

- README: collapse Discord/OpenWeatherMap/NASA/DeepSeek/ID howto sections into `<details>` so the top stays product + photo + features.

## 0.4.35

- LICENSE inventory updated for multi-file sketch sources; notes that local `secrets.h` is not in the repo.
- GitHub About description synced to current version.

## 0.4.34

- README: Ongoing project section (OTA updates, set1/set2 when a configured user ID — normally the owner — is mentioned or DMed, PCB + desk case).

## 0.4.33

- README: short AI-use note (same idea as Space Wars — AI helped with edits; James owned architecture and board decisions).

## 0.4.32

- Secrets are `#define` macros in `secrets.h` (not `const char*` variables) so multi-file link no longer reports multiple definition of Wi-Fi/token symbols.

## 0.4.31

- Split monolithic `MiniMe_Discord_Bot.ino` into multi-file Arduino sketch: `config.h`, `minime.h`, and `.cpp` modules (time, users, display, touch, hardware, discord REST/gateway, commands). Behavior unchanged. Thin `.ino` holds setup/loop only.


## 0.4.30

- Secrets moved out of the `.ino` into `MiniMe_Discord_Bot/secrets.h` (gitignored). Committed template: `secrets.example.h`. Sketch `#include "secrets.h"`.

## 0.4.29

- `!sysinfo` firmware link updated to `minime-esp32-discord-bot` (old repo name removed).
- README fill-in block matches sketch: `OWNER_ID_STR` / channel IDs are `const char*` (same as 0.4.27 firmware).

## 0.4.28

- Owner `!led`: keep `on` / `off`; add `!led <r> <g> <b>` (integers **0–255**) on the onboard RGB NeoPixel (GPIO 48, `NEO_GRB`). `on` is white 255 255 255. Helpers: `setLedRgb`, `parseRgbTriplet`.
- Mid-line `!led` keeps multi-word RGB args (same as `!ask` / `!display`). Discord `!help` / README use `!led on/off` wording.
- Firmware polish: `gwSendJson`, `drawDashBar`, `findFreeTrackedSlot`, `uptimeDhms`, HTTP open connect/timeout codes, dashboard user rows without `String names[]`, `sendDiscordMessage` via `httpsAwaitHeaders`, drop `botDiscordStatusSent` (status `0` = unset).
- CHANGELOG 0.4.13 mid-line note corrected for `!led`.

## 0.4.27

- Firmware cleanup: strip dead Serial debug and unused helpers; trim sketch comment noise (command list lives in Discord `!help`).
- Shared HTTPS/HTTP open helpers, fetch-command helpers, `discordDisplayName`, and tracked-user slot clear/fill.
- Owner/channel IDs as `const char*`; shared `boardMemTotals()` for OLED heap bar and `!sysinfo`.

## 0.4.26

- Remove unused `dashLastCmd`, `dashLastEvent`, `dashLastCmdMillis`, `TEMP_CHANNEL_ID_STR`, `drawTransient()`, `debugTouchSerial()`, `TOUCH_DEBUG_MS`, and `lastTouchDebugMillis`.
- README matches current firmware: poll-only OLED wake (no touch interrupt, no Serial touch debug); `!ask` Discord cap **2000** (not 3600); presence updates do not wake the OLED; `TOUCH_THRESHOLD` default **2000**; CPU 80 MHz when OLED is off and bot is Idle.

## 0.4.25

- OLED: uptime and temp on row 3; Sig / Heap / Srv shift down one row. User rows 7–14 and rows 15–16 unchanged.

## 0.4.24

- DS18B20 on GPIO 10: enable `INPUT_PULLUP` at boot and before each temperature read.
- README breadboard photo now includes the DS18B20 (`docs/minime-breadboard-v2.jpg`).

## 0.4.23

- OLED sleep: wake only when compensated touch is at or above trip. Ignore sticky hardware IRQ status, and attach the interrupt at the trip point (not 0) so idle below trip can dim/off.

## 0.4.22

- Comment out Serial (test logging off). Touch trip gap remains 2000.

## 0.4.21

- Touch trip gap default is **2000**.

## 0.4.20

- CPU 80 MHz when OLED is off and Discord bot status is Idle; 240 MHz otherwise. Serial remains test-only.

## 0.4.19

- Serial: dedicated `usb power:` line with VBUS millivolts and volts (divider on GPIO 1).

## 0.4.18

- Serial 115200 on; touch init and raw/comp/idle/trip/usb/touched lines every 500 ms.

## 0.4.17

- README touch tuning: lower `TOUCH_THRESHOLD` if the pad is hard to trigger; raise it if it false-triggers.

## 0.4.16

- Touch trip gap default is **450** (was 3500) to match a ~400–500 raw delta on this pad.

## 0.4.15

- Touch cal: 16-sample rolling idle average; trip is a constant gap above that average. USB VBUS ADC on GPIO 1 (10k/10k divider) scales touch raw so USB voltage does not walk the trigger. `!sysinfo` shows VBUS mV and idle/trip.

## 0.4.14

- Add `LICENSE` (MIT for original MiniMe files only; third-party libraries and APIs stay under their own terms). README points to it.

## 0.4.13

- Mid-sentence one-word args work for most commands (e.g. `… !weather 90210 …`). `!ask` / `!display` / `!led` keep multi-word args for the rest of the line.

## 0.4.12

- Commands may appear anywhere in a message (e.g. `Should I !ask what time is it.`); they are handled the same as if they started the line.

## 0.4.11

- Touch pad still wakes the OLED; it no longer sets Discord presence to Online.

## 0.4.10

- OLED row 14: `Bot:Online` / `Bot:Idle  ` left; fixed-slot `Www Mmm dd YYYY` right (space-padded day, DOW does not shift).

## 0.4.9

- Restore command handling that had regressed: lowercase the command word only (args like `!display` text keep their case).
- Unknown `!` commands reply `That is not a command.` (normal chat without `!` is ignored).
- `!weather` accepts only a 5-digit US ZIP; anything else returns `Invalid ZIP code.`

## 0.4.8

- OLED row 5 combines uptime and temp as fixed-width `Up:xxxxdxxhxxm T:xxxF/xxxC` (space-padded; sensor fail shows `T:--Error--`).
- User rows shift up to rows 6–13; row 14 left open.

## 0.4.7

- Fix `!ask` silent failures: Discord 2000-char post limit, larger REST JSON buffer, HTTP status check, HTTPS busy lock while DeepSeek runs, short fallback if the reply post fails.

## 0.4.6

- Boot auto `!sysinfo` waits until Gateway is connected and identified (no more false "Disconnected").

## 0.4.5

- Bot Discord presence: Online on commands, scheduled posts, and touch; Idle after 5 minutes quiet (Gateway OP 3).
- Serial logging commented out (including touch debug in `loop`) to reduce CPU load.

## 0.4.4

- Capacitive touch wake on GPIO 4: tap pad to turn the OLED back on after dim/off (1 min idle, 15 s fade).
- README: touch pad wiring, calibration, Serial debug, and threshold tuning. Display sleep timing matches firmware.
- Sketch section headers for user tracking, touch wake, Discord REST, and setup/loop.

## 0.4.3

- `!ask` is queued from the Gateway callback; DeepSeek HTTPS runs from `loop()` with heartbeats pumped so Discord stays connected.

## 0.4.2

- OLED: eight user rows; `Srv:` bar (0–90°, boot at 45°); command / `!display` text on rows 15–16 (dashboard is not wiped).
- `!display` is public; payload only, 50 characters (25 + 25), 6 seconds, overwrite restarts the timer.
- Member names load from `BOT_GUILD_ID` and both command-channel guilds (two servers).
- `!ask`: `max_tokens` 900, 12288-byte JSON parse, 3600-character Discord post.
- `!sysinfo` includes a GitHub firmware URL with Discord link embeds suppressed.
- `!help` and README command lists are alphabetical. README matches the current dashboard.

## 0.4.1

- Dashboard user stats: seven rows (was five).
- Sketch section comments for each major block (config, OLED, users, APIs, Gateway, setup/loop).
- README: 0.4.1 notes, U8g2 baseline (no setCursor), OLED sleep does not power down MCU or Wi-Fi, member load is 7.

## 0.4.0

- SSD1327 128×128 dashboard: MiniMe header, gateway, Pacific time, Sig/Heap bars, temp, uptime, five users with presence and 24h command counts.
- Startup REST member load (`BOT_GUILD_ID` / channel guild resolve) plus Presence Intent for On / Idle / DND / Off.
- OLED sleeps after 10 minutes with no real events (Sig/time/heap ticks do not count); commands, presence changes, and gateway messages wake it.
- Public `!ask`; `!status` is not in this firmware. Commands work in DMs and two allowed channels.
- US Pacific DST time, 8MB PSRAM gateway JSON, `!sysinfo` heap includes PSRAM.
- README rewrite, breadboard photo, no secrets in the GitHub sketch.

## 0.3.2

- Make `!ask` owner-only; update README and help text.

## 0.3.1

- Fix `!ask` DeepSeek parsing: handle chunked HTTP bodies, refuse gzip, clearer errors.

## 0.3.0

- Public `!ask <question>` via DeepSeek chat API (`DEEPSEEK_API_KEY`).

## 0.2.0

- Public science commands: `!news` (space/high-tech headlines), `!physics` (arXiv), `!apod` (NASA), `!iss` (ISS position).

## 0.1.1

- Minor firmware comment and formatting tweaks.

## 0.1.0

- Initial MiniMe ESP32-S3 Discord bot firmware (commands, weather, sensors, GPIO, OLED, scheduled reports).
- README for Discord token, weather API key, owner ID, and channel IDs.
