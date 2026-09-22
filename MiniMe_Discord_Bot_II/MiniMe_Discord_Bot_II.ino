/*
 MiniMe II Discord bot (Guition JC3248W535EN / AXS15231B). Command list: Discord !help.

 Sketch map:
  secrets.h / secrets.example.h  -- Wi-Fi, tokens, IDs (gitignored)
  minime_config.h                -- pins, buffer sizes, timing constants
  minime.h                         -- shared declarations and globals
  cores.h / cores.cpp              -- Core 0 uiTask; DashSnap publish; Discord cmd queue
  time_util.cpp                    -- NTP, Pacific DST, date/uptime format helpers
  users.cpp                        -- tracked users, guild cache, presence
  display.cpp                      -- LCD dashboard (Arduino_GFX AXS15231B), sleep
  touch.cpp                        -- AXS15231B I2C touch wake
  hardware.cpp                     -- servo, NeoPixel, DS18B20, GPIO
  discord_rest.cpp                 -- HTTPS REST (CA bundle), sendDiscordMessage, members
  discord_gateway.cpp              -- websocket, heartbeat, identify, events
  serial_log.cpp                   -- MmLog -> web UI only (no USB Serial / UART0)
  ota.cpp                          -- Wi-Fi ArduinoOTA firmware update
  web_ui.cpp                       -- LAN page + status JSON (ArduinoJson)
  web_assets.h                     -- LAN CSS + boot/app JS (PROGMEM)
  k9dtv_logo_svg.h                 -- dark K9DTV logo for /logo.svg
  k9dtv_logo_bright_svg.h          -- light K9DTV logo for /logo-bright.svg
  k9dtv_logo_rgb565.h              -- LCD bitmap logos dark+bright RGB565
  menu_chip_svg.h                  -- dark/light IC chips for theme toggle
  commands.cpp                     -- handleCommand tokenizer, APIs, DeepSeek, scheduled
  MiniMe_Discord_Bot_II.ino        -- setup / loop + Wi-Fi / gateway connect

 Dual-core: Arduino loop on Core 1 (Gateway/HTTPS/OTA/web); uiTask on Core 0 (LCD+touch).
 LCD sleep turns backlight off after 5 min idle; ESP32 and Wi-Fi stay up.
 Touch wakes the panel only; does not set Discord Online.
 Discord Idle (5 min quiet) drops CPU to 160 MHz; activity / OTA back to 240.

 Board: ESP32S3 Dev Module, Flash 16MB, OPI PSRAM, USB CDC On Boot Enabled.
 Libs: GFX Library for Arduino (AXS15231B), ArduinoJson 6, WebSockets, etc.
 Partition: sketch partitions.csv = 2x ~7.9MB OTA apps.
*/
#include "minime.h"
#include "cores.h"
#include "esp_wifi.h"

// Core 1 loop stack: DashSnap publish + HTTPS String bodies need headroom beyond default 8 KB.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // modem sleep breaks ArduinoOTA (port 3232)
  esp_wifi_set_ps(WIFI_PS_NONE); // IDF: no Wi-Fi power save (fewer WS blips)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  showTransient("WiFi", "Connecting...");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000UL) {
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED) {
    showTransient("WiFi", "Timeout");
    // fall through; ensureWifiForGateway() retries in loop()
    return;
  }
  // Re-assert after associate (some stacks re-enable sleep on connect).
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  showTransient("WiFi", "Connected");
}

void setup() {
  mmSerialBegin();
  gwDoc = new DynamicJsonDocument(GW_DOC_PSRAM);
  if (!gwDoc) {
    MmLog.println(F("Fatal: gwDoc alloc failed (PSRAM?)"));
    // delay() feeds TWDT; board stays here until power cycle (no OTA/web/Gateway).
    while (true) {
      delay(1000);
    }
  }
  initTrackedUsers();
  if (!setupDisplay()) {
    // No panel -- still run Discord / web; transients are no-ops until begin works.
  }
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
  sensors.begin();
  sensors.setWaitForConversion(false); // Core 0 uses pollTemperatureNonBlocking
  connectWiFi();
  setupMiniMeOta();
  setupWebUi();
  timeClient.begin();
  showTransient("Discord", "Loading users...");
  if (fetchGuildMembersAtStartup()) {
    String n0 = trackedUsers[0].userName.length() ? trackedUsers[0].userName : "ok";
    showTransient("Users loaded", n0);
  } else {
    showTransient("Users", "Fetch failed");
  }
  delay(1200); // hold user-loaded transient so it's visible
  connectGateway();
  // Let Hello+Identify finish before first full LCD flush (QSPI can block TLS).
  {
    unsigned long t0 = millis();
    while (!identified && (millis() - t0) < 25000UL) {
      pumpGateway();
      pumpOta();
      pumpWebUi();
      yield();
      delay(10);
    }
  }
  setServoAngle(45);
  lastDashMillis = 0;
  if (identified) showTransient("Ready", "GW identified");
  else showTransient("Ready", "GW waiting...");
  publishDashSnap();
  startUiCore(); // Core 0 owns LCD + touch from here
}

void loop() {
  pumpOta();
  pumpWebUi();
  // While flashing, do not run Discord / UI publish (starves OTA → timeouts / odd replies like '864')
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
