/*
 MiniMe II Discord bot (Guition JC3248W535EN / AXS15231B). Command list: Discord !help.

 Sketch map:
  secrets.h / secrets.example.h  -- build placeholder only (gitignored; not used at boot)
  secrets_bufs.h / secrets_load.cpp -- runtime buffers; boot requires SD /secrets.h
  minime_config.h                -- pins, buffer sizes, timing constants
  minime.h                         -- shared declarations and globals
  cores.h / cores.cpp              -- Core 0 uiTask; DashSnap publish; Discord cmd queue
  time_util.cpp                    -- NTP, Pacific DST, date/uptime format helpers
  users.cpp                        -- tracked users, guild cache, presence
  display.cpp                      -- LCD setup/sleep/theme/bars; updateDisplay
  display_overlay.cpp              -- transient/Event/temp mux helpers
  dash_snap.cpp                    -- DashSnap capture/publish seqlock
  display_draw.cpp                 -- palette, panels, drawDashboard
  display_internal.h               -- private LCD types shared by the four files above
  ui_controls.cpp                  -- Controls page: bright/vol/toggles
  mm_prefs.cpp / mm_prefs.h      -- ESP32-S3 onboard flash prefs (Save/Cancel)
  touch.cpp                        -- AXS15231B I2C touch wake
  hardware.cpp                     -- NeoPixel, DS18B20, GPIO
  sd_card.cpp                      -- SD SPI mount, free/total MB, hot-plug retry
  audio.cpp                        -- I2S speaker UI ticks (wake vs button)
  board_info.cpp                   -- boardMemTotals / getSystemInfo (!sys)
  discord_rest.cpp                 -- HTTPS client lifecycle, sendDiscordMessage, members
  discord_http.cpp                 -- HTTP header/body reader (await + readHttpBodyAfterHeaders)
  discord_gateway.cpp              -- websocket, heartbeat, identify, events (filter init at connect)
  serial_log.cpp                   -- MmLog -> web UI only (no USB Serial / UART0)
  ota.cpp                          -- Wi-Fi ArduinoOTA firmware update
  web_ui.cpp                       -- LAN routing + log rings
  web_render.cpp                   -- LAN HTML/JSON/assets (status page)
  web_ui_internal.h                -- shared WebServer + ring externs for web_ui/web_render
  web_assets.h                     -- LAN CSS + boot/app JS (PROGMEM)
  k9dtv_logo_svg.h                 -- dark K9DTV logo for /logo.svg
  k9dtv_logo_bright_svg.h          -- light K9DTV logo for /logo-bright.svg
  k9dtv_logo_rgb565.h              -- LCD bitmap logos dark+bright RGB565
  k9_mark_icon_svg.h (+ bright/right) -- Cancel/Save dog+K9 SVGs (web)
  k9_mark_icon_rgb565.h            -- LCD Cancel/Save mark-icons left/right RGB565
  menu_chip_svg.h                  -- dark/light IC chips for theme toggle
  commands.cpp                     -- tokenizer + handleCommand dispatch
  command_fetch.cpp                -- weather/news/arxiv/APOD/ISS/DeepSeek fetches
  wifi_connect.cpp                 -- connectWiFi()
  coredump_cmd.cpp                 -- owner !coredump flash panic summary
  MiniMe_Discord_Bot_II.ino        -- setup / loop only (must be the only .ino in the sketch folder)

 Dual-core: Arduino loop on Core 1 (Gateway/HTTPS/OTA/web); uiTask on Core 0 (LCD+touch).
 LCD sleep turns backlight off after 5 min idle; ESP32 and Wi-Fi stay up.
 Touch wakes the panel only; does not set Discord Online.
 Discord Idle (5 min quiet) drops CPU to 160 MHz; activity / OTA back to 240.

 Board: ESP32S3 Dev Module, Flash 16MB, OPI PSRAM, USB CDC On Boot Enabled.
 Libs: GFX Library for Arduino (AXS15231B), ArduinoJson 7, WebSockets, etc.
 Partition: sketch partitions.csv = 2x ~7.9MB OTA apps.
*/

#include "minime.h"
#include "cores.h"
#include "mm_prefs.h"
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>

// Loop stack: ARDUINO_LOOP_STACK_SIZE in minime.h (before Arduino.h) is the 3.x hook.
// Do not also define getArduinoLoopTaskStackSize() here -- core 3.3+ already provides it
// from that macro (redefinition error).
static_assert(ARDUINO_LOOP_STACK_SIZE == 16384, "keep minime.h ARDUINO_LOOP_STACK_SIZE at 16 KB");

static const char* secretsFaultLine() {
  return secretsFromSd ? "bad SD file" : "need SD secrets.h";
}

// SD /secrets.h is required. Stay on the glass fault until a usable file loads
// (hot-plug is fine). Do not continue into Wi-Fi on empty or compile-time keys.
static void ensureSdSecrets() {
  if (loadSecrets()) return;
  const char* shown = secretsFaultLine();
  showTransient("Secrets", shown);
  paintBootNotice("Secrets", shown, true);
  MmLog.println(F("Secrets: waiting for SD /secrets.h"));
  unsigned long next = millis() + 5000;
  for (;;) {
    bool wasPresent = sdCardPresent();
    pollSdCard();
    unsigned long now = millis();
    // Card just showed up -- read /secrets.h on this pass, do not wait out the 5 s.
    if (sdCardPresent() && !wasPresent) next = now;
    if ((long)(now - next) >= 0) {
      next = now + 5000;
      if (loadSecrets()) return;
      const char* why = secretsFaultLine();
      showTransient("Secrets", why);
      if (strcmp(why, shown) != 0) {
        shown = why;
        paintBootNotice("Secrets", why, true);
      }
    }
    delay(200);
  }
}

void setup() {
  mmSerialBegin();
  gwDoc = newSpiRamJsonDoc();
  if (!gwDoc) {
    MmLog.println(F("Fatal: gwDoc alloc failed (PSRAM?)"));
    while (true) {
      delay(1000);
    }
  }
  // AJ7 starts at capacity 0; prove the PSRAM allocator can hand out a block.
  {
    void* probe = mmSpiRamJsonAlloc().allocate(256);
    if (!probe) {
      MmLog.println(F("Fatal: gwDoc PSRAM probe failed"));
      while (true) {
        delay(1000);
      }
    }
    mmSpiRamJsonAlloc().deallocate(probe);
  }
  initTrackedUsers();
  if (!setupDisplay()) {
    // No panel -- still run Discord / web; transients are no-ops until begin works.
  }
  loadSettings(); // prefs partition: LCD theme + Controls (or defaults if corrupt)
  setupTouch();
  lastDisplayActivityMillis = millis();
#if MM_USB_CDC_ON_BOOT
  showTransient("Serial", "CDC ON 115200");
#else
  showTransient("Serial", "CDC OFF+USB");
#endif
  delay(800); // hold boot transient so it's visible
  showTransient("Booting...", "MiniMe II Discord");
  setupPins();
  setupSdCard();
  ensureSdSecrets();
  showTransient("Secrets", "from SD card");
  paintBootNotice("Secrets", "from SD card", false);
  delay(800); // splash stays up through Wi-Fi; uiTask replaces it with the dashboard
  sensors.begin();
  sensors.setWaitForConversion(false); // Core 0 uses pollTemperatureNonBlocking
  connectWiFi();
  setupMiniMeOta();
  setupWebUi();
  timeClient.begin();
  // LCD + LAN up before Discord HTTPS/Identify so boot/web clocks match "ready to use".
  publishDashSnap();
  startUiCore(); // Core 0 owns LCD + touch from here
  lastDashMillis = 0;

  showTransient("Discord", "Loading users...");
  if (fetchGuildMembersAtStartup()) {
    String n0 = trackedUsers[0].userName[0] ? String(trackedUsers[0].userName) : "ok";
    showTransient("Users loaded", n0);
  } else {
    showTransient("Users", "Fetch failed");
  }
  delay(400); // brief hold (was 1200) -- UI already live
  connectGateway();
  if (identified) showTransient("Ready", "GW identified");
  else showTransient("Ready", "GW waiting...");
  // Identify/resume continues in loop(); do not block setup up to 25s for Hello.
  // TWDT after long setup waits: reconfigure timeout + enable loopTask only.
  // Do not subscribe uiTask (0.7.30/0.7.34 panic path). No mid-HTTPS reset sprinkle.
  {
    esp_task_wdt_config_t twdt = {
      .timeout_ms = TWDT_TIMEOUT_MS,
      .idle_core_mask = (1U << 0), // Core 0 idle only; Core 1 loopTask is busy
      .trigger_panic = true,
    };
    esp_err_t err = esp_task_wdt_reconfigure(&twdt);
    if (err != ESP_OK) {
      MmLog.print(F("[SYS] TWDT reconfigure failed err="));
      MmLog.println((int)err);
    } else {
      enableLoopWDT();
      MmLog.print(F("[SYS] TWDT loop "));
      MmLog.print(TWDT_TIMEOUT_MS / 1000UL);
      MmLog.println(F("s (stuck-loop backstop)"));
    }
  }
}

void loop() {
  static bool loggedStackOnce = false;
  if (!loggedStackOnce) {
    loggedStackOnce = true;
    // FreeRTOS remaining high-water words (4 B each on ESP32).
    // ~16 KB stack => after setup HWM often >~2000 words; ~8 KB usually ~1500-2000.
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
    MmLog.print("[SYS] loop stack free HWM words=");
    MmLog.print((unsigned)hwm);
    MmLog.print(" (~");
    MmLog.print((unsigned)(hwm * sizeof(StackType_t)));
    MmLog.println(" B)");
  }
  pumpOta();
  pumpWebUi();
  drainCore0Logs();
  // While flashing, do not run Discord / UI publish (starves OTA -> timeouts / odd replies like '864')
  if (otaIsBusy()) {
    return;
  }
  pumpGateway();
  drainDiscordCmds();
  runAskFromLoop();
  updateBotPresenceIdle();
  publishDashSnap();
  yield();
}
