# MiniMe II -- Discord bot on Guition JC3248W535EN

[![Compile](https://github.com/K9DTV/minime-ii-esp32-discord-bot/actions/workflows/compile.yml/badge.svg)](https://github.com/K9DTV/minime-ii-esp32-discord-bot/actions/workflows/compile.yml)
[![Sanity](https://github.com/K9DTV/minime-ii-esp32-discord-bot/actions/workflows/sanity.yml/badge.svg)](https://github.com/K9DTV/minime-ii-esp32-discord-bot/actions/workflows/sanity.yml)
[![Python](https://github.com/K9DTV/minime-ii-esp32-discord-bot/actions/workflows/python.yml/badge.svg)](https://github.com/K9DTV/minime-ii-esp32-discord-bot/actions/workflows/python.yml)
[![HTML](https://github.com/K9DTV/minime-ii-esp32-discord-bot/actions/workflows/html.yml/badge.svg)](https://github.com/K9DTV/minime-ii-esp32-discord-bot/actions/workflows/html.yml)

**Project page:** https://k9dtv.com/project-minime.html  
**Sister project:** [MiniMe I (SSD1327 OLED)](https://github.com/K9DTV/minime-esp32-discord-bot) -- not this repo.

MiniMe II is firmware for the **Guition JC3248W535EN** all-in-one module (ESP32-S3-N16R8 + **AXS15231B** color LCD with in-cell touch). This is **not** a breadboard build and **not** MiniMe I's OLED board.

### What this is not

- **Not a cloud service** -- the bot runs entirely on the ESP32; Discord is remote, the app is on the module
- **Not a Discord.js / Python bot** -- native Arduino/ESP32 firmware, not a host PC or Raspberry Pi process
- **Not a general-purpose ESP32 Discord library** -- this is one application (LCD + LAN + commands), not a reusable SDK
- **No servo** -- removed in v0.7.82 (see **Hardware -> Servo removed** below)

**Display:** panel native **320x480** (portrait); firmware paints **480x320** landscape via **GFX Library for Arduino** (`Arduino_ESP32QSPI` + `Arduino_AXS15231B` + `Arduino_Canvas`). No SSD1327 / U8g2 and no capacitive GPIO wake pad.

**LCD <-> LAN web:** glass and `http://<board-ip>/` are one design -- same Display / Log / Controls pages, same fields and chrome roles (LCD fixed 480x320; web scales). Logos and Cancel/Save marks on the web are SVG twins of the LCD art. Light/Dark on the glass and in the browser stay **independent** (each has its own chip).

### LCD UI preview

480x320 landscape from the current firmware (K9DTV logo + menu chips). Dark | Light side by side.

**Display** (metrics | users)

| Dark | Light |
|:----:|:-----:|
| ![Display dark](docs/lcd-mock/display-dark.png) | ![Display light](docs/lcd-mock/display-light.png) |

**Log** (LOG | Serial)

| Dark | Light |
|:----:|:-----:|
| ![Log dark](docs/lcd-mock/log-dark.png) | ![Log light](docs/lcd-mock/log-light.png) |

Interactive HTML (all four): [`docs/lcd-mock/all-four.html`](docs/lcd-mock/all-four.html).

**Status:** Guition module firmware **v0.7.86** -- usable on the board today (Discord, LCD, LAN, Controls prefs). Still in active tuning and soak testing to harden edge cases; not a closed "final" product. See `VERSION` / `CHANGELOG.md` and [`docs/CODE_REVIEW_NOTES.md`](docs/CODE_REVIEW_NOTES.md).

### Arduino libraries

1. Library Manager -> **GFX Library for Arduino** (moononournation) -> Install (build must include `Arduino_AXS15231B` / QSPI)
2. Also: ArduinoJson 7, WebSockets, OneWire, DallasTemperature, Adafruit NeoPixel, NTPClient

No **JC3248W535EN-Touch-LCD**, **JPEGDecoder**, or **U8g2**.

Web/LCD art headers live **in the sketch** (not Library Manager): `k9dtv_logo_svg.h`, `k9dtv_logo_bright_svg.h`, `menu_chip_svg.h` (and mark-icon SVG/RGB565 headers). CI (`tools/ci_html.py`) requires those SVG headers. LCD logo/mark bitmaps: regenerate with `tools/gen_k9dtv_logo_rgb565.py` / `tools/gen_k9_mark_icon_rgb565.py` when site art changes.

**If compile fails with `LIST_HEAD` / `Arduino_ESP32QSPI` / `Arduino_AXS15231B` does not name a type:**  
the IDE is using a **second, broken** GFX install. Arduino reports something like:

`Used: ...\libraries\Arduino_GFX`  
`Not used: ...\libraries\GFX_Library_for_Arduino`

**Fix (you do this in the Arduino libraries folder -- Cursor will not touch it):**

1. Quit Arduino IDE.  
2. **Move out of `libraries\` entirely** (Desktop/trash -- not a rename inside `libraries`) any folder still named `Arduino_GFX` or `Arduino_GFX_old`.  
3. Keep `GFX_Library_for_Arduino` (Library Manager's real package).  
4. Reopen IDE. Log must say `Used: ...\GFX_Library_for_Arduino` only.

**If those same errors continue with `GFX_Library_for_Arduino`:**  
GFX version and ESP32 core mismatch. Update GFX to latest, or pin the esp32 core (e.g. **3.3.5** / **3.2.1**). Check `...\GFX_Library_for_Arduino\library.properties` `version=` if it still fails.

After Wi-Fi connects, open `http://<board-ip>/` for the LAN dashboard (same Display/Log idea as the LCD).

License: see `LICENSE` (non-commercial for original MiniMe II files only; commercial use requires express written permission).

This is my second iteration of MiniMe. The board build is meant to be useful and reliable for day-to-day Discord + LCD use. I am still hardening (soaks, edge cases, polish) -- not declaring the project closed.
AI helped with firmware edits, multi-file layout, and GitHub updates. I owned the architecture, wiring, Discord Gateway/LCD design, commands, power/idle trade-offs, and what shipped on the board.

## Project status

**Ready for daily use** on the Guition module: Discord Gateway/commands, LCD + LAN twin UI, flash Controls prefs, audio alerts, owner OTA upload path.

**Not closed out.** Work continues on hardening and a short roadmap (order matters):

1. **Further soak / edge-case hardening** -- long runtimes, reconnect storms, heap pressure. Local soak: [`docs/HIL_SOAK.md`](docs/HIL_SOAK.md), [`docs/soak-results.md`](docs/soak-results.md). Fixed vs deferred: [`docs/CODE_REVIEW_NOTES.md`](docs/CODE_REVIEW_NOTES.md).
2. **Desk case / enclosure** -- last on the list (module board already; enclosure is packaging, not a breadboard prototype).

**CI:** four badges -- **Compile** (Arduino), **Sanity** (fast host checks), **Python** (pytest + Pillow), **HTML** (LAN CSS/JS/SVG in headers).

Already in: dual-core LCD vs Gateway, Display/Log/Controls + Light/Dark chips, LCD<->web UI parity, flash Controls prefs (dual-slot CRC), DM/@mention flags + alarm/`!clear`, dirty redraw, user-initiated Wi-Fi OTA (`!ota`), ArduinoJson 7, `!ask` HOL, Core0 log bridge, **secrets from SD** (`/secrets.h`).

### Firmware updates (OTA)

There is **no cloud update service**. MiniMe II does **not** pull or receive firmware from a remote server. You (or an owner on the LAN) choose when to upload a new build -- USB or Wi-Fi ArduinoOTA (`!ota` prints IP / hostname / port **3232**).

**Security note (OTA):** ArduinoOTA is only a **hostname + password** on the local network. There is no signed firmware, certificate identity, or other strong authority behind that password. Any host that knows the password can upload a new image. Treat the LAN as trusted; do not expose port **3232** to an untrusted network. Prefer USB when the network is not trusted; keep `OTA_PASSWORD` strong and private.

**Security note (LAN web UI):** With `WEB_UI_PASSWORD` empty (default), `http://<board-ip>/` and `POST /api/controls` have **no** authentication -- any host on the same network can change brightness/volume/toggles and factory-reset Controls prefs. Set a non-empty `WEB_UI_PASSWORD` in `secrets.h` to gate `/api/status` and `/api/controls` behind a LAN login (session token). Still not HTTPS or strong remote admin -- treat the LAN as trusted; do not expose the board to an untrusted network.

---

## What this bot can do

Same list Discord shows for `!help`:

### Public commands

*(DMs, `TARGET_CHANNEL_ID`, or `TARGET_CHANNEL_ID1`)*

| Command | Description | Notes / Key requirement |
|---|---|---|
| `!apod` | NASA Astronomy Picture of the Day | Requires `NASA_API_KEY` |
| `!ask <question>` | DeepSeek text reply in chat | Capped at 2000 chars; requires `DEEPSEEK_API_KEY` |
| `!display <text>` | Transient overlay on the LCD | Max 50 characters across two lines (6 s duration) |
| `!help` | Interactive command list | -- |
| `!iss` | International Space Station position | No API key required |
| `!news` | Space / high-tech headlines | Spaceflight News API |
| `!physics` | Latest arXiv physics papers | `cat:physics` feed |
| `!sys` | System diagnostics (uptime, internal heap, PSRAM, RSSI, gateway, firmware URL) | Hardware health overview |
| `!temp` | Indoor DS18B20 temperature | Rear 4-pin header (GPIO 18); sensor not fitted yet |
| `!time` | Bot local time (US Pacific, DST aware) | NTP synchronized |
| `!weather <zip>` | US ZIP weather (OpenWeatherMap) | Requires `WEATHER_API_KEY` |

### Owner-only

*(`OWNER_ID_STR`)*

| Command | Description | Safety / operational notes |
|---|---|---|
| `!ota` | Print Wi-Fi ArduinoOTA connection info (IP / hostname / port 3232) | User-initiated only; see **Firmware updates (OTA)** |
| `!coredump` | Last panic from flash coredump (`!coredump clear` erases) | Forensic memory preservation |
| `!clear` | Clear DM / mention alert flags on the LCD (stops the repeating alarm) | Silences the I2S alert buzzer |
| `!resetprefs` | Factory-reset Controls prefs in flash (theme, bright, vol, Sound/Ticks/Notify) | Restores default configuration |

This Guition module has **no LED1 / LED2** and **no on-board RGB**. `!led` is not shipped (no handler). There is **no** `!servo` -- the servo path was removed (see Hardware).

### Channel / DM commands

Commands work in `TARGET_CHANNEL_ID`, `TARGET_CHANNEL_ID1`, and DMs. **No automatic boot posts**.

### Bot Discord presence

- Starts **Online** when the Gateway identifies
- Goes **Idle** after **5 minutes** with no activity
- Returns to **Online** on commands (touch does **not** set Online)

### `!ask` / DeepSeek

- Request `max_tokens`: **900**
- JSON parse buffer: **24576** bytes
- Discord post cap: **2000** characters
- HTTPS on the ESP32 can take several seconds (CA-verified TLS)
- Queued off the Gateway thread; heartbeats keep running while DeepSeek waits

## How it works

Everything below runs on one **ESP32-S3**. Discord stays in the cloud; MiniMe talks to it two ways, paints the LCD, serves a LAN web dashboard, and wakes the panel from in-cell touch.

![MiniMe architecture flowchart -- same layout as k9dtv.com/project-minime.html](docs/arch-flow.svg)

*Same flowchart as the project page.*

- **Gateway** -- live link for chat commands, presence, Online/Idle, heartbeats (must not stall during long HTTPS). Heartbeats start after Hello (jittered first send); a missing OP11 ACK past the Discord interval plus **15 s** grace forces disconnect (`HB_ACK_TIMEOUT`). **Identify-only** after drops (no session resume); one `beginSslWithBundle` at boot (ESP32 CA bundle -- not plain `beginSSL`/`setInsecure`), then library reconnect only.
- **REST** -- bot posts replies and loads member names; also pulls science/weather/AI over HTTPS/HTTP. Outbound TLS uses the ESP32 **CA cert bundle** (no `setInsecure`). One shared `WiFiClientSecure`; `httpsInUse` prevents overlapping HTTPS from `stop()`ing each other. Gateway WebSocket TLS uses the same CA blob via WebSockets `beginSslWithBundle`.
- **LCD** -- **480x320** landscape status board on the Guition panel (native **320x480**); idle blanks backlight only (Wi-Fi and Gateway stay up). Metrics + users (or LOG + Serial in Log layout). Redraw every **1 s** with dirty tracking. LAN API exposes the same fills as percents.
- **LAN web UI** -- browser twin of the LCD at `http://<board-ip>/` (same layout roles; independent Light/Dark); polls `/api/status` every **2 s** (CSS/JS in `web_assets.h`). **MmLog** feeds web LOG/Serial only (no USB Serial / UART0 log dump).
- **Touch** -- wakes the LCD and hits the IC chips (theme / layout). Does not change Discord Online/Idle.

### Dual-core (ESP32-S3)

| Core | Owns |
|------|------|
| **1** (`loop`) | Discord Gateway + heartbeats, HTTPS REST, ArduinoOTA, LAN web server, command queue drain, `publishDashSnap` |
| **0** (`uiTask`) | Touch I2C, DS18B20 poll, `updateDisplay` / QSPI flush, backlight idle |

Shared UI state is a published **DashSnap** (seqlock; Core 1 writes, Core 0 paints). Discord `MESSAGE_CREATE` only enqueues; `handleCommand` runs from Core 1 `loop()` so TLS never runs inside the Gateway WebSocket callback. Mid-draw `pumpGateway()` is gone -- LCD flush no longer starves heartbeats.

**What dual-core fixes:** QSPI / touch / temp sensor no longer block Gateway heartbeats. Long HTTPS fetches (weather, news, APOD, ISS, physics, DeepSeek) run on Core 1 but body reads pump the Gateway via `readHttpBodyAfterHeaders` (same as DeepSeek). Chat still enters via the command queue -> `drainDiscordCmds` -> `handleCommand` (the stall length is the fetch itself; HB stays alive during the body wait).

### Why this is hard (on one MCU)

- Discord Gateway heartbeats must keep running while long HTTPS calls use the same TLS client (dual-core removes LCD/QSPI from that fight; fetch bodies pump HB via `readHttpBodyAfterHeaders`).
- Large Gateway JSON lives in **PSRAM**; small Wi-Fi/TLS buffers must **not**.
- Backlight can turn off while Wi-Fi and the Gateway stay up (panel sleep != chip sleep).
- Up to **22** live presence rows + 24h command counts on one landscape panel.

---

## LCD dashboard

**Panel:** Guition JC3248W535EN AXS15231B, native **320x480**, firmware canvas **480x320** landscape (`Arduino_Canvas`). Layout in `display_draw.cpp` / `dash_snap.cpp` (`drawDashboard` + `DashSnap` dirty tracking).

The LAN page matches this layout (Display = metrics|users, Log = LOG|Serial, Controls = sliders|toggles). Glass and browser Light/Dark stay independent.

<details>
<summary><strong>LCD panels, chips, !display, and backlight sleep</strong></summary>

### Modes (right IC chip)

| Chip label | Left window | Right window |
|---|---|---|
| **Display** | Metrics (header, bars, Id/Users, DM/Mention, HTTPS, Event, Sys rows) | Users (name / status / Bot:N), up to **22** rows @ **9 px** pitch |
| **Log** | LOG ring | Serial ring |

Right chip is LCD-only (**independent** of the LAN Display/Log layout). Left IC chip toggles **Light / Dark** palette on the LCD only (**independent** of the LAN web theme).

Header band: K9DTV logo (`k9dtv_logo_rgb565.h` -- dark + bright RGB565) + two menu-chip style buttons. Regenerate with `python MinimeII/tools/gen_k9dtv_logo_rgb565.py` (Playwright Chromium + Pillow) from site dark/bright SVGs.

Empty user slots show `---`. Names from startup REST member fetch (nick -> global name -> username). Presence from the Gateway. Command counts reset every 24 hours.

DM to the bot and @mention of `OWNER_ID_STR` set **DM** / **Mention** flags on the left panel and start a two-note I2S **alarm every 3 s**; owner `!clear` clears the flags and stops the sound.

### Controls flash prefs (`prefs` partition)

Adapted from the VFO settings *rules* (dirty / CRC / corrupt defaults), stored in **ESP32-S3 onboard flash** via `esp_partition` -- not a 47L16. Code: `mm_prefs.cpp`.

| Item | Behavior |
|---|---|
| **Partition** | First entry in `partitions.csv`: label `prefs`, **8 KB** at `0x9000` (two **4 KB** erase sectors = slot A / slot B). Wi-Fi **NVS** follows at `0xB000` (12 KB). |
| **What is stored** | LCD Light/Dark, brightness, volume, Sound, Ticks, Notify -- plus flash overhead (signature / version / struct size / sequence / tail magic / CRC32). Browser Light/Dark stays in `localStorage` only. |
| **Save** | Controls **Save** writes only when dirty. Writes the **other** slot with a bumped sequence. Stays on Controls. |
| **Cancel** | Short tap recalls from flash (stays on Controls). If live values already match the last loaded/saved image (**not dirty**), recall early-outs (no flash read, no redraw). **Long-press ~3 s** = factory reset (defaults + write). Leaving via **Menus** without Save also recalls (same dirty early-out). |
| **Factory reset** | Long-press Cancel, or owner `!resetprefs`. Defaults = bright/vol **100%**, toggles **ON**, Dark. |
| **Boot** | `loadSettings()` after display setup (Wi-Fi -> web -> LCD uiTask order unchanged from 0.7.76). |
| **Corrupt / missing** | -> defaults (brightness/volume **100%**, toggles **ON**, Dark), then seed flash. |
| **First flash after this change** | Partition table moved -- full USB erase/upload once. |

### `!display`

- Public command.
- Only the text after `!display` is shown.
- Cap **50** characters across two transient lines.
- Stays **6 seconds**. A new `!display` overwrites and restarts the timer.

### Backlight sleep

After **5 minutes** with no real events, backlight turns **off**. That is panel power only. The microcontroller, Wi-Fi, and Discord Gateway keep running.

These **wake** the panel and restart the **5-minute** idle timer: **in-cell touch**, Discord commands, gateway connect/disconnect, `!display`, and other status overlays. Presence updates for user rows **do not** wake the panel.

Discord presence still goes Idle after **5 minutes** quiet (CPU drops to **160 MHz**; activity / OTA returns to **240 MHz**). Backlight sleep does not by itself change CPU clock.

</details>

---

## Fill in these values

Credentials are **`#define NAME "value"`** lines in a file named **`secrets.h`**.

**Preferred (runtime):** copy that file to the **root of the SD card** as `/secrets.h`. At boot the firmware mounts the card and loads Wi-Fi, Discord token, API keys, channel IDs, OTA, and `WEB_UI_PASSWORD` from it -- change credentials without reflashing.

**Build seed (still required):** keep a `MiniMe_Discord_Bot_II/secrets.h` on the build PC (gitignored; keep real secrets **outside** this workspace) so the sketch compiles. Boot always seeds from that compile-time file, then **overlays** any keys found on the SD card. If the SD file is missing, compile-time values are used as-is (LOG: `Secrets: using compile-time...`).

**Missing SD card:** Display **IP** flashes bright red **2 s on / 2 s off** (LCD + web). Put the card in and reboot (or wait for remount) so `/secrets.h` can load.

### Firmware wiring (do not break)

`minime.h` aliases `WIFI_SSID` -> `secWifiSsid` (and the other keys the same way) so call sites keep the old names while values live in runtime buffers. That only works if:

```cpp
// =============================================================================
// READ THIS BEFORE EDITING
//
// The macro aliasing below (WIFI_SSID -> secWifiSsid, etc.) only works if:
//   1. secrets_load.cpp is the ONLY .cpp that includes secrets.h directly
//   2. All other .cpp files include minime.h (never secrets.h) at the top
//   3. The #undef block in secrets_load.cpp runs after secrets.h and before minime.h
//
// If you add a new .cpp that needs credentials, include minime.h ONLY.
// =============================================================================
```

1. Copy `MiniMe_Discord_Bot_II/secrets.example.h` -> `secrets.h` (on your machine, outside the repo if that is your rule)
2. Fill in real values and **delete** the `#define MINIME_SECRETS_IS_EXAMPLE 1` line (compile errors if it remains).
3. Copy the same filled `secrets.h` to the **SD card root** (filename exactly `secrets.h`).

```cpp
#define WIFI_SSID            "ssid"
#define WIFI_PASSWORD        "password"
#define BOT_TOKEN            "bot token"
#define WEATHER_API_KEY      "WEATHER_API_KEY"
#define NASA_API_KEY         "NASA_API_KEY"
#define DEEPSEEK_API_KEY     "DEEPSEEK_API_KEY"
#define BOT_GUILD_ID         "GUILD_ID"  // startup member fetch
#define OWNER_ID_STR         "OWNER_ID_STR"         // !clear + mention alert
#define TARGET_CHANNEL_ID    "TARGET_CHANNEL_ID"    // commands
#define TARGET_CHANNEL_ID1   "TARGET_CHANNEL_ID1"   // second command channel
#define OTA_HOSTNAME         "minime2"
#define OTA_PASSWORD         "change-me-ota"
#define WEB_UI_PASSWORD      ""                 // empty = open LAN UI; set to gate /api/*
```

Use `#define NAME "value"` (quoted strings). The SD parser reads those lines; other `#` directives and `//` comments are ignored.

| Field | Used for |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | ESP32 station Wi-Fi |
| `BOT_TOKEN` | Discord Gateway + REST |
| `WEATHER_API_KEY` | OpenWeatherMap `!weather` |
| `NASA_API_KEY` | NASA APOD for `!apod` |
| `DEEPSEEK_API_KEY` | DeepSeek for `!ask` |
| `BOT_GUILD_ID` | One guild to load members from at boot |
| `OWNER_ID_STR` | Who can run `!clear` / owner cmds; mention alert target |
| `TARGET_CHANNEL_ID` | Commands (no automatic boot posts) |
| `TARGET_CHANNEL_ID1` | Second channel where commands are allowed |
| `OTA_HOSTNAME` / `OTA_PASSWORD` | ArduinoOTA on the LAN (password only -- see **Firmware updates (OTA)**) |
| `WEB_UI_PASSWORD` | Optional LAN web login (empty = off; see **Security note (LAN web UI)**) |

IDs are **digits only**. Paste them as C strings, for example `"123456789012345678"`.

Boot loads LCD names from `BOT_GUILD_ID` and from the guilds of the target channels. Duplicate users are stored once. Slots are split across those guilds, then leftover rows are filled.

<details>
<summary><strong>How to get a Discord bot token (<code>BOT_TOKEN</code>)</strong></summary>

1. Open [Discord Developer Portal](https://discord.com/developers/applications) and sign in.
2. **New Application** -> name it -> Create.
3. Left sidebar: **Bot** -> **Add Bot** if needed.
4. Under **Token**, **Reset Token** / **Copy** -> `BOT_TOKEN` in `secrets.h`.
5. Enable **Privileged Gateway Intents**: Message Content, Server Members, Presence. If any are off, Discord closes the socket right after Identify (often no `READY` / `OP9` in our log -- close can look like a bare `WS_DISCONNECTED_WIFI_UP` loop).
6. Identify intents: `INTENTS_MINIME` in `minime_config.h` (`static_assert` checks `== 37635`). Boot log: `[GW] intents=37635`.

### Invite the bot to your server

1. Developer Portal -> **OAuth2** -> **URL Generator**.
2. Scopes: `bot`. Minimum permissions: View Channels, Send Messages, Read Message History.
3. Open the URL, pick your server, authorize.
4. The bot stays offline until the ESP32 connects.

</details>

<details>
<summary><strong>How to get your owner ID (<code>OWNER_ID_STR</code>)</strong></summary>

This is **your Discord user ID**, not the bot's ID.

1. Discord: **User Settings** -> **Advanced** -> enable **Developer Mode**.
2. Right-click **your own avatar** -> **Copy User ID**.
3. Paste into `OWNER_ID_STR` in `secrets.h`.

</details>

<details>
<summary><strong>How to get channel and guild IDs</strong></summary>

Developer Mode must be on.

- **Channel:** right-click the text channel -> **Copy Channel ID**.
- **Guild / server:** right-click the server icon -> **Copy Server ID** -> `BOT_GUILD_ID`.

The bot must be able to **see and send** in those channels.

</details>

<details>
<summary><strong>How to get an OpenWeatherMap key (<code>WEATHER_API_KEY</code>)</strong></summary>

1. Free account at [OpenWeatherMap](https://home.openweathermap.org/users/sign_up).
2. [API keys](https://home.openweathermap.org/api_keys) -> paste into `WEATHER_API_KEY`.
3. New keys can take a few hours. `!weather` uses `zip={zip},US` and `units=imperial`.

</details>

---

## Science / physics commands

| Command | Source | Key? |
|---|---|---|
| `!news` | [Spaceflight News API](https://api.spaceflightnewsapi.net/) -- 3 headlines | No |
| `!physics` | [arXiv](https://arxiv.org/) `cat:physics` -- 3 newest papers | No |
| `!apod` | [NASA APOD](https://api.nasa.gov/) -- title, short explanation, image URL | Yes |
| `!iss` | [Open Notify](http://open-notify.org/) -- ISS lat / lon | No |

<details>
<summary><strong>How to get a NASA key (<code>NASA_API_KEY</code>)</strong></summary>

1. [api.nasa.gov](https://api.nasa.gov/) -> free key -> paste into `NASA_API_KEY`.
2. `DEMO_KEY` works for light testing but is shared and rate-limited.

</details>

<details>
<summary><strong>How to get a DeepSeek key (<code>DEEPSEEK_API_KEY</code>)</strong></summary>

1. [DeepSeek Platform](https://platform.deepseek.com/) -> [API Keys](https://platform.deepseek.com/api_keys).
2. Paste into `DEEPSEEK_API_KEY`. Replies capped at **2000** characters.

</details>

---

## Hardware (default pins)

Board: **Guition JC3248W535EN** module (ESP32-S3-N16R8 + AXS15231B LCD + in-cell touch). Not a breadboard; not MiniMe I.

**Display:** native **320x480**; firmware **480x320** landscape (`Arduino_ESP32QSPI` + `Arduino_AXS15231B` + Canvas).

| Device | GPIO |
|---|---|
| LCD backlight | 1 |
| LCD QSPI CS / SCK / D0-D3 | 45 / 47 / 21 / 48 / 40 / 39 |
| Touch I2C SDA / SCL / INT | 4 / 8 / 3 |
| SD SPI CS / MOSI / SCK / MISO | 10 / 11 / 12 / 13 |
| DS18B20 data (rear 4-pin; not fitted yet) | 18 |
| I2S DOUT / BCLK / LRCLK (on-module amp -> speaker) | 41 / 42 / 2 |

Change pins in `minime_config.h` if your wiring differs. **No LED1 / LED2**, **no on-module RGB**, **no** `!set1` / `!set2`, **no** USB VBUS ADC (GPIO 1 is backlight). Rear twin **4-pin** headers share **GND / 3.3 V / GPIO 17 / GPIO 18** -- temp uses **18**; **17** is free (was briefly the servo pin).

### Servo removed

On **MiniMe I** I planned an external servo to raise a small mailbox flag for DM / @mention alerts. MiniMe II kept that idea for a while (`!servo`, `Srv` bar, GPIO 17).

With this Guition panel I already have a full color Display (DM/Mention flags on the glass) plus the on-module speaker (ticks and the repeating alarm). That covers the same job without a mechanical flag -- so the servo is **gone**: no `!servo`, no `Srv` meter, no LEDC on GPIO 17. GPIO **18** is temperature; **17** is unused if you want it later for something else.

### SD card (on-module slot)

SPI: **CS 10**, **MOSI 11**, **SCK 12**, **MISO 13**. Firmware mounts at boot (retries if the card is hot-plugged). Display **SD** row = remaining free in **MB** + free/total bar (LCD and web).

Put credentials in a file named **`secrets.h`** on the card root (same `#define NAME "value"` lines as the build template). Boot loads that file after the SD mounts (see **Fill in these values**). Compile-time `secrets.h` remains the build seed / fallback.

**No card detected:** **IP** line flashes bright red **2 s on / 2 s off** on both LCD and LAN web.

### Speaker (on-module I2S + NS4168)

Guition demo pins: **DOUT 41**, **BCLK 42**, **LRCLK/WS 2**. Firmware plays short sine ticks over I2S (no external piezo).

Touch feedback:

- **Asleep:** any touch -> soft lower tick + wake backlight
- **Awake:** Light/Dark or Display/Log chip -> higher confirm tick
- **Awake:** anywhere else -> silent (still refreshes idle timer)

If the speaker is still very quiet, community reports often need a **10 kohm pull-up on NS4168 CTRL (U4 pin 1) to 3.3 V** so the amp is enabled / right-channel selected -- that is a hardware mod, not a firmware volume slider.

### DS18B20 (GPIO 18, rear 4-pin header)

**Not hooked up on this board yet.** I do not have the mating connector for the Guition rear 4-pin header. Firmware already polls **GPIO 18**; as soon as the connector arrives it will be wired in. Until then `!temp` and the LCD **T** line show error / disconnect (Serial may log `DS18B20 disconnected` about once a minute) -- that is expected, not a firmware bug.

When fitted: TO-92 on the back **4-pin** (**GND / 3.3 V / 17 / 18**), DQ on **GPIO 18**. Internal pull-up is enabled; a **4.7 kohm** DQ->3.3 V resistor is still recommended.

| TO-92 lead (flat toward you, leads down) | Connect to |
|---|---|
| Left | GND (4-pin header) |
| Middle (DQ) | GPIO 18 (4-pin header) |
| Right (VDD) | 3.3 V (4-pin header) |

`!temp` uses this sensor once it is connected. On failure: Discord `Temperature sensor error.` and LCD `T --Error--`.

---

## Touch (in-cell)

Capacitive touch on the AXS15231B wakes the LCD after backlight-off and hits the theme / layout chips (I2S speaker ticks -- see above).

<details>
<summary><strong>Touch behavior</strong></summary>

- I2C: SDA **4**, SCL **8**, INT **3**, addr **0x3B** (`minime_config.h`).
- `pollTouchWake()` in `loop()`; debounce **300 ms**.
- No USB VBUS ADC and no separate capacitive GPIO wake pad.

### What touch does *not* do

- Does not send Discord messages, set Discord Online/Idle, or drive external actuators by itself.
- The ESP32, Wi-Fi, and Gateway **never sleep** -- only the backlight turns off.

</details>

---

## Arduino IDE setup

GitHub Actions runs **Compile**, **Sanity**, **Python**, and **HTML** on push (badges above). None upload or talk to the board. Local soak: [`docs/HIL_SOAK.md`](docs/HIL_SOAK.md). Attested results: [`docs/soak-results.md`](docs/soak-results.md).

1. Install [Arduino IDE](https://www.arduino.cc/en/software) and the **esp32** board package (Espressif).
2. Open **only** `MiniMe_Discord_Bot_II/MiniMe_Discord_Bot_II.ino` from a folder that contains that **single** `.ino` plus the `.cpp` / `.h` files and `partitions.csv`. Arduino merges every `.ino` in the folder into one translation unit -- a leftover `Discord_Bot_MiniMe_II.ino` (or any second `.ino`) causes `redefinition of 'void connectWiFi()'` / `setup` / stack helpers. Delete extras; folder name should match the one `.ino` basename.
3. Provide `secrets.h` (from `secrets.example.h`) with Wi-Fi, token, keys, and IDs.
4. Set **Tools** as in the table below for this **Guition N16R8** module.
5. Libraries: GFX Library for Arduino, WebSockets, ArduinoJson, OneWire, DallasTemperature, Adafruit NeoPixel, NTPClient.
6. Upload. Confirm with Discord `!help` and the LAN page header (`Display - v` + `MINIME_VERSION` / `VERSION`). Tap the glass to wake after backlight off.

### Required Tools settings (N16R8)

| Tools menu | Setting for MiniMe II |
|---|---|
| **Board** | **ESP32S3 Dev Module** (not generic ESP32 Dev Module) |
| **USB CDC On Boot** | **Enabled** (upload / OTA; Monitor stays quiet -- logs on LAN web UI) |
| **USB Mode** | **Hardware CDC and JTAG** |
| **Flash Size** | **16MB (128Mb)** |
| **Flash Mode** | **QIO 80MHz** (typical) |
| **Partition Scheme** | Sketch **`partitions.csv`**: dual OTA apps (~7.9MB each), **no SPIFFS**. Prefer **Custom** when offered. |
| **PSRAM** | **OPI PSRAM** |
| **PSRAM frequency** (if shown) | **80MHz** (or board default) |
| **Arduino Runs On** / **Events Run On** | **Core 1** (keep default). Firmware pins LCD `uiTask` to **Core 0**; leave Arduino/Events on Core 1. |
| **USB DFU On Boot** | Disabled (unless needed) |
| **Upload Mode** | **UART0 / Hardware CDC** |
| **Upload Speed** | **921600** (or lower if uploads fail) |

### RAM / flash notes

- **RAM:** internal SRAM + **8MB OPI PSRAM**. **PSRAM -> OPI PSRAM** must be on.
- Large Discord Gateway JSON (`GW_DOC_PSRAM` soft size) and LAN `statusDoc` use `JsonDocument` with `SpiRamAllocator` (PSRAM via `heap_caps_*`). Default `JsonDocument` is internal SRAM only.
- Do **not** enable `heap_caps_malloc_extmem_enable` for small allocations (Wi-Fi / TLS in PSRAM can crash).
- **Flash:** **16MB**. `partitions.csv` is dual OTA apps + small coredump -- **no filesystem** yet (SD secrets are planned). First install over USB; later builds can use user-initiated Wi-Fi OTA (see **Firmware updates (OTA)**).

---

## Safety

- Never commit firmware that contains a live bot token, API key, password, or Discord snowflake ID.
- Keep real `secrets.h` **out of** this workspace. `secrets.example.h` only in-repo.
- If a token leaks, reset it in the Developer Portal immediately.

---

## License

Original MiniMe II source, README, changelog, and docs in this repo are under a
**non-commercial** license: personal and educational use is allowed; commercial
use requires the copyright holder's prior express written permission. See `LICENSE`.

That grant does **not** cover Arduino/ESP32 libraries, Discord, or other APIs. Install the libraries under **Arduino IDE setup** and follow each service's rules for keys and bots.
