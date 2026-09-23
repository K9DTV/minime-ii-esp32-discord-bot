#ifndef MINIME_CONFIG_H
#define MINIME_CONFIG_H

// Single firmware version string (keep VERSION file in sync).
#define MINIME_VERSION "0.7.69"
#define MINIME_USER_AGENT "MiniMeBot/1.0"

// Scratch TWDT proof only — enables owner !hang (infinite loop). Never ship with this enabled.
// Uncomment for one flash, run !hang, wait ~90s, reboot, !coredump, then comment out again.
// #define MINIME_TEST_TWDT

// Discord content max is 2000. !ask max_tokens / JSON buffer sized to fit one message.
const int DISCORD_CONTENT_MAX = 2000;
const int DEEPSEEK_MAX_TOKENS = 900;
const size_t DEEPSEEK_JSON_DOC = 24576; // soft size hint for !ask (AJ7 grows; was AJ6 pool)
// Discord REST: retry budget for 429 and header timeouts; keep Gateway alive while waiting.
#define DISCORD_REST_MAX_ATTEMPTS 3
#define DISCORD_429_MAX_ATTEMPTS DISCORD_REST_MAX_ATTEMPTS // alias (older name)
#define DISCORD_429_WAIT_MIN_MS 500UL
#define DISCORD_429_WAIT_MAX_MS 60000UL
#define DISCORD_HEADER_RETRY_WAIT_MS 500UL

// ====== GPIO CONFIG (Guition JC3248W535EN / AXS15231B) ======
// Display QSPI (Arduino_ESP32QSPI + Arduino_AXS15231B): CS 45, SCK 47, D0 21, D1 48, D2 40, D3 39
// Touch I2C (same AXS15231B): SDA 4, SCL 8, INT 3. Backlight: GPIO 1.
// NeoPixel/servo remapped off QSPI pins (48/47). Change if you rewire.
// I2S speaker (NS4168): DOUT 41, BCLK 42, LRCLK 2.
const int LCD_BL_PIN  = 1;
const int LCD_CS_PIN  = 45;
const int LCD_SCK_PIN = 47;
const int LCD_D0_PIN  = 21;
const int LCD_D1_PIN  = 48;
const int LCD_D2_PIN  = 40;
const int LCD_D3_PIN  = 39;
const int LCD_NATIVE_W = 320; // panel native (portrait)
const int LCD_NATIVE_H = 480;
const int TOUCH_SDA_PIN = 4;
const int TOUCH_SCL_PIN = 8;
const int TOUCH_INT_PIN = 3;
const uint8_t TOUCH_I2C_ADDR = 0x3B;
const int RGB_LED_PIN = 16;
const int PIN_SERVO   = 17;
const int PIN_DS18B20 = 10;
const int I2S_DOUT_PIN  = 41;
const int I2S_BCLK_PIN  = 42;
const int I2S_LRCLK_PIN = 2; // WS / LRC

// ====== TIME CONFIG (NTP) -- US Pacific DST ======
const long PST_OFFSET_SEC = -28800; // UTC-8
const long PDT_OFFSET_SEC = -25200; // UTC-7

// ====== DISCORD GATEWAY ======
const size_t GW_DOC_PSRAM = 262144;   // 256KB
// Guition N16R8 has 8 MB PSRAM; boardMemTotals reports ESP.getPsramSize()/getFreePsram() (no clamp).
const unsigned long BOT_PRESENCE_IDLE_MS = 300000UL; // 5 minutes quiet -> Idle
// Extra wait past Discord heartbeat_interval before HB_ACK_TIMEOUT kills the socket.
// Stops false zombies when OP11 is late (ESP32 TLS / Wi-Fi jitter).
const unsigned long GW_HB_ACK_GRACE_MS = 15000UL;
const uint32_t CPU_MHZ_ACTIVE = 240; // Online / OTA / commands
const uint32_t CPU_MHZ_IDLE = 160;   // Discord Idle presence (not 80 — that correlated with resets)
// Task WDT: Arduino feeds between loop() calls only. Timeout must fit the longest
// single loop iteration (!ask ~15s handshake + 30s body; Discord 429 wait up to 60s).
// Soft Discord guard remains GW HB ack; TWDT is a stuck-loop backstop (not primary).
const uint32_t TWDT_TIMEOUT_MS = 90000UL;
// !ask HOL: dedicated DeepSeek TLS + drain during waits only if !httpsInUse (0.7.38).

// Gateway Identify intents (single source; used by sendIdentify + boot log).
constexpr uint32_t INTENT_GUILDS          = 1u << 0;
constexpr uint32_t INTENT_GUILD_MEMBERS   = 1u << 1;
constexpr uint32_t INTENT_GUILD_PRESENCES = 1u << 8;
constexpr uint32_t INTENT_GUILD_MESSAGES  = 1u << 9;
constexpr uint32_t INTENT_DIRECT_MESSAGES = 1u << 12;
constexpr uint32_t INTENT_MESSAGE_CONTENT = 1u << 15;
constexpr uint32_t INTENTS_MINIME =
    INTENT_GUILDS | INTENT_GUILD_MEMBERS | INTENT_GUILD_PRESENCES |
    INTENT_GUILD_MESSAGES | INTENT_DIRECT_MESSAGES | INTENT_MESSAGE_CONTENT;
static_assert(INTENTS_MINIME == 37635u, "intents value drifted from Discord docs");

// ====== DISPLAY STATE (480x320 landscape via Arduino_GFX AXS15231B canvas) ======
const unsigned long DASH_REFRESH_MS = 1000UL; // ~60ms draw+flush measured; 1s ok for Gateway HB

const unsigned long DISPLAY_IDLE_MS = 300000UL; // 5 minutes after last display activity -> backlight off
const unsigned long TOUCH_DEBOUNCE_MS = 300;
// Backlight PWM (GPIO 1): UI 0..100 maps to duty 10..100%; sleep uses 0.
const uint32_t LCD_BL_PWM_HZ = 5000;
const uint8_t LCD_BL_PWM_BITS = 8;
const uint8_t LCD_BL_PCT_MIN = 10; // floor duty when UI shows 0%
const uint8_t LCD_BL_PCT_DEFAULT = 80; // UI percent (maps to ~82% duty)
const uint32_t LCD_BL_DUTY_AWAKE = 204; // legacy default (= 80% of 255)
// DM / @mention sticky alert: I2S alarm chirp while flags set (owner !clear stops).
const unsigned long ALERT_SOUND_PERIOD_MS = 3000UL;

// ====== USER TRACKING (LCD right panel @ USER_PITCH; 22 rows) ======
const uint8_t MAX_TRACKED_USERS = 22;
const unsigned long USES_WINDOW_MS = 86400000UL;  // 24h
const uint8_t MAX_CACHED_GUILDS = 3;

// ====== LAN WEB UI (dashboard + log; plain HTTP) ======
const uint16_t WEB_UI_PORT = 80;
const unsigned long WEB_STATUS_POLL_MS = 2000UL; // browser /api/status poll; edit here to override

#endif
