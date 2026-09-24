#ifndef MINIME_H
#define MINIME_H

// Arduino-ESP32 3.x: must be defined before Arduino.h / esp32-hal.h (SET_LOOP_TASK_STACK_SIZE
// after Arduino.h is a no-op once CONFIG_ARDUINO_LOOP_STACK_SIZE is fixed at the 8 KB default).
#ifndef ARDUINO_LOOP_STACK_SIZE
#define ARDUINO_LOOP_STACK_SIZE 16384
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <atomic>
#include <time.h>
#include <string.h>
#include <stddef.h>

#include "secrets.h"
#if defined(MINIME_SECRETS_IS_EXAMPLE)
#error "Using secrets.example.h template — copy to secrets.h, fill values, remove MINIME_SECRETS_IS_EXAMPLE"
#endif
#include "minime_config.h"

// Large JSON arenas prefer PSRAM (ArduinoJson 7). Do not use default JsonDocument for
// Gateway / status / DeepSeek — default allocator is internal SRAM only.
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
  }
  void deallocate(void* pointer) override { heap_caps_free(pointer); }
  void* reallocate(void* pointer, size_t new_size) override {
    void* p = heap_caps_realloc(pointer, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_realloc(pointer, new_size, MALLOC_CAP_8BIT);
    return p;
  }
};
inline SpiRamAllocator& mmSpiRamJsonAlloc() {
  static SpiRamAllocator alloc;
  return alloc;
}
// Soft size hints (AJ7 grows elastically; used for overflow checks / comments).
inline JsonDocument* newSpiRamJsonDoc() {
  return new (std::nothrow) JsonDocument(&mmSpiRamJsonAlloc());
}

// MmLog -> web UI LOG/Serial only (no USB Serial / UART0).
class MmLogClass : public Print {
 public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
extern MmLogClass MmLog;
void mmSerialBegin();
#if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT == 1)
#define MM_USB_CDC_ON_BOOT 1
#else
#define MM_USB_CDC_ON_BOOT 0
#endif
void webLogFeed(const uint8_t* buffer, size_t size);
void drainCore0Logs(); // Core 1: flush Core 0 enqueued MmLog lines into Serial panel
uint8_t lcdFullLogCount();
bool lcdFullLogNewest(uint8_t fromNewest, char* buf, size_t bufLen);
uint8_t lcdSerialCount();
bool lcdSerialNewest(uint8_t fromNewest, char* buf, size_t bufLen);
uint32_t lcdLogGen(); // bumps when LOG or Serial ring changes
// Command-failure Discord replies (not usage/unknown). Last N for !sys + Serial via MmLog.
enum { CMD_ERR_RING_N = 10 };
void noteCmdErrorReply(const char* msg);
uint8_t cmdErrorReplyCount();
bool cmdErrorReplyNewest(uint8_t fromNewest, char* buf, size_t bufLen);
// ====== USER TRACKING ======
struct TrackedUser {
  char userId[24];   // Discord snowflake
  char userName[32]; // display / username (LCD truncates further)
  uint8_t status;  // 0 Off, 1 Idle, 2 On, 3 DND
  uint32_t useCount24h;
  bool active;
};

// ====== NTP / TIME ======
extern WiFiUDP ntpUDP;
extern NTPClient timeClient;
void updateLocalTime();
void formatLocalDateStr(char* buf, size_t bufLen);
void formatLocalTimeStr(char* buf, size_t bufLen);
void formatUptimeStr(char* buf, size_t bufLen);

// ====== DISCORD GATEWAY ======
extern WebSocketsClient gatewayWS;
extern JsonDocument* gwDoc;
extern bool gatewayConnected;
extern bool identified;
extern int heartbeatIntervalMs;
extern unsigned long lastHeartbeatMillis;
extern int lastSeq;
extern unsigned long lastBotActivityMillis;
extern uint8_t botDiscordStatus;
void noteBotActivity();       // Discord presence Online + activity timer
void updateBotPresenceIdle();
void sendBotPresence(const char* status, bool afk);
void gwLogEvent(const String& ev);
void sendIdentify();
void sendHeartbeat();
void pumpGateway();
void gwParkReconnectForOta();      // 1h reconnect during flash
void gwRestoreReconnectAfterOta(); // restore after OTA error (success reboots)
void gwSerialService();
void gatewayEvent(WStype_t type, uint8_t* payload, size_t length);
void requestTrackedUserPresences();
void gwSendJson(JsonDocument& doc);

// ====== WIFI OTA ======
void setupMiniMeOta();
void pumpOta();
bool otaIsBusy();
String otaStatusText();

// ====== LAN WEB UI ======
void setupWebUi();
void pumpWebUi();

// ====== DISCORD REST / HTTPS ======
// httpsGetOpen / httpGetOpen: 0=ok, 1=busy, 2=header timeout, 3=connect/TLS/DNS fail
// On 0, outChunked / outContentLength reflect response headers for readHttpBodyAfterHeaders.
extern WiFiClientSecure httpsClient;
extern bool httpsInUse;
bool httpsConnect(const char* host, uint32_t timeoutMs = 15000);
bool httpsAcquire(const char* host, uint32_t timeoutMs = 15000); // claim + connect, or false if busy/fail
void httpsRelease(); // stop shared client + clear httpsInUse
uint8_t httpsGetOpen(const char* host, const String& path, unsigned long headerTimeoutMs,
                     bool& outChunked, int& outContentLength,
                     const char* userAgent = MINIME_USER_AGENT,
                     const char* extraHeaders = nullptr);
uint8_t httpGetOpen(WiFiClient& client, const char* host, const String& path,
                    unsigned long headerTimeoutMs, bool& outChunked, int& outContentLength);
void setHttpOpenError(String& outReport, uint8_t err, const char* label);
bool httpsAwaitHeaders(Client& client, unsigned long deadlineMs, bool pump, String& outStatus,
                       bool& chunked, int& contentLength,
                       float* outRetryAfterSec = nullptr);
bool readHttpBodyAfterHeaders(Client& client, bool chunked, int contentLength,
                              String& outBody, unsigned long deadlineMs);
bool discordIdLooksValid(const String& id);
bool discordRestGet(const String& path, String& outBody, String& outStatus);
String guildIdFromChannel(const String& channelId);
bool appendMembersFromGuild(const String& guildId, uint8_t maxToAdd);
bool fetchGuildMembersAtStartup();
bool sendDiscordMessage(const String& channelId, const String& content, bool suppressEmbeds = false);
// Same as sendDiscordMessage, but records into the cmd-error ring (operator diagnostics).
bool sendDiscordCmdError(const String& channelId, const String& content, bool suppressEmbeds = false);
String getSystemInfo();
void boardMemTotals(uint32_t& memFree, uint32_t& memTotal);   // internal SRAM
void boardPsramTotals(uint32_t& psFree, uint32_t& psTotal);    // 0/0 if no PSRAM
void uptimeDhms(unsigned long& days, unsigned long& hours, unsigned long& minutes, unsigned long& seconds);

// ====== DISPLAY (Arduino_GFX: ESP32QSPI + AXS15231B + Canvas) ======
extern Arduino_Canvas* gfx;
extern int lastServoDeg;
enum { UI_TRANSIENT_COLS = 40, UI_EVENT_COLS = 37 };
extern char transientLine1[UI_TRANSIENT_COLS];
extern char transientLine2[UI_TRANSIENT_COLS];
extern char transientLine3[UI_TRANSIENT_COLS];
extern char lastEventLine[UI_EVENT_COLS]; // sticky Event; copy under uiOverlay helpers
extern std::atomic<bool> alertDm;         // sticky until owner !clear
extern std::atomic<bool> alertMention;    // sticky until owner !clear
extern bool lcdThemeLight;             // LCD palette only (web theme is independent)
extern bool lcdLayoutLog;              // false=metrics|users; true=LOG|Serial overlay
extern bool lcdLayoutControls;         // Controls page (right chip cycle)
extern std::atomic<uint8_t> uiBrightPct; // UI 0..100 (duty maps to 10..100%)
extern std::atomic<uint8_t> uiVolPct;    // 0..100 -> I2S peak scale
extern std::atomic<bool> uiNotifyOn;     // DM/@mention alarm
extern std::atomic<bool> uiTicksOn;      // touch ticks
extern std::atomic<bool> uiSoundOn;      // master mute
extern std::atomic<uint32_t> uiControlsGen;
extern std::atomic<uint32_t> mmLogDropCore0; // Core0 log ring overflow (def in web_ui.cpp)
extern unsigned long lastDashMillis;
extern std::atomic<unsigned long> lastDisplayActivityMillis;
extern unsigned long lastDashDrawMs;   // last full drawDashboard (incl flush)
extern unsigned long lastDashFlushMs;  // last gfx->flush() only
extern std::atomic<bool> displayAsleep; // Core 0 sleep + Core 1 wake via noteDisplayActivity
// Temp sample: Core 0 stores, others snapshot under mux (C/F + timestamp together).
void dashTempStore(float c, float f);
bool dashTempSnapshot(float& c, float& f, bool& hadSample, bool& fresh); // fresh = hadSample && age<30s
void uiOverlayCopyEvent(char* buf, size_t bufLen);
void uiOverlayCopyTransient(char* l1, size_t l1Len, char* l2, size_t l2Len, char* l3, size_t l3Len,
                            unsigned long* untilMs);
bool uiOverlayExpireIfDue(unsigned long now); // clear until under mux; true if expired
bool setupDisplay();
void noteDisplayActivity(); // LCD backlight idle timer / wake
void noteLastEvent(const String& line); // sticky Event line (+ wakes display)
void drawDashboard();
void updateDisplay();
void showTransient(const String& line1, const String& line2 = "", const String& line3 = "",
                   unsigned long durationMs = 3000); // durationMs=0 -> 3s; !display uses 6000

bool lcdThemeChipHit(uint16_t x, uint16_t y);
bool lcdLayoutChipHit(uint16_t x, uint16_t y);
bool lcdLogoHit(uint16_t x, uint16_t y);
bool handleControlsTouch(uint16_t x, uint16_t y, bool rising);
void toggleLcdTheme();
void toggleLcdLayout();
void toggleLcdControls();
void setLcdThemeLight(bool light);
void setLcdLayoutLog(bool logMode);
void setLcdControls(bool on);
void applyLcdLayoutMode(uint8_t mode); // 0=Display, 1=Log, 2=Controls
void cycleLcdLayout(int dir);          // +1 / -1 through Display|Log|Controls
bool lcdDogLeftHit(uint16_t x, uint16_t y);
bool lcdDogRightHit(uint16_t x, uint16_t y);
void applyBacklightFromSettings();
void setUiBrightPct(uint8_t pct);
void setUiVolPct(uint8_t pct);
void setUiNotifyOn(bool on);
void setUiTicksOn(bool on);
void setUiSoundOn(bool on);
void controlsSnapshotEnter(uint8_t returnMode);
void controlsRestoreSnapshot(); // flash recall (same as Cancel)
void controlsCancel();
void controlsSave();
bool controlsLeavingIsCommit();
void loadSettings();   // boot prefs load (mm_prefs.cpp -- ESP32-S3 onboard flash)
bool saveSettings();
bool recallSettings();
bool isSettingsDirty();
// Panel bar fills (single source for LCD + web API percents)
enum { DASH_SIG_HEAP_BAR_MAX = 280, DASH_SRV_BAR_MAX = 280 };
int dashSigBarW(long rssi);
int dashHeapBarW(uint32_t memFree, uint32_t memTotal);
int dashSrvBarW(int servoDeg);
int dashBarPct(int fill, int maxFill);

// ====== TOUCH (AXS15231B I2C wake only; no Discord Online) ======
extern unsigned long lastTouchWakeMillis;
extern bool touchWasActive;
void setupTouch();
void pollTouchWake();
bool lcdTouchPoint(uint16_t& x, uint16_t& y);
// ====== HARDWARE ======
extern OneWire oneWire;
extern DallasTemperature sensors;
extern Adafruit_NeoPixel pixels;
void setupPins();
void setupServo();
void setupAudio();
void audioTickWake();    // asleep: any touch wakes + short tick
void audioTickButton();  // awake: theme/layout chip only (short tick)
void audioAlertBeep();   // single sustained beep (saved tone; available for other uses)
void audioAlarmBeep();   // repeating DM/@mention alarm chirp (two-note)
void pollAudioAlerts();  // Core 0: while alertDm|alertMention, alarm every ALERT_SOUND_PERIOD_MS
void setServoAngle(int angleDeg);
// Core 0 uiTask only: non-blocking DS18B20 (shared OneWire — never call from Core 1).
bool pollTemperatureNonBlocking(float& tempC, float& tempF);
void setLedRgb(uint8_t r, uint8_t g, uint8_t b);
bool parseRgbTriplet(const String& args, uint8_t& r, uint8_t& g, uint8_t& b);
bool isOwner(const String& authorId);
void clearAlertFlags();

// ====== USERS / PRESENCE ======
extern TrackedUser trackedUsers[MAX_TRACKED_USERS];
extern char cachedGuildIds[MAX_CACHED_GUILDS][24];
extern uint8_t cachedGuildCount;
extern unsigned long usesWindowStartMillis;
const char* statusToWord(uint8_t s);
void initTrackedUsers();
void recordUserUse(const String& userId, const String& userName);
void applyPresencesArray(JsonArray presences);
void handlePresenceUpdate(JsonObject d);
String discordDisplayName(JsonVariantConst user);
int findUserIndex(const char* userId);
int findUserIndex(const String& userId);
int findFreeTrackedSlot();
void fillTrackedSlot(uint8_t i, const char* userId, const char* userName);
void fillTrackedSlot(uint8_t i, const String& userId, const String& userName);
void rememberGuildId(const String& gid);

// ====== COMMANDS / BACKGROUND ======
extern bool askNeedPost;
extern String askPendingQuestion;
extern String askPendingChannelId;
String collapseWhitespace(String s);
String truncateText(const String& s, int maxLen);
bool getWeather(const String& zip, String& outReport);
bool getScienceNews(String& outReport);
bool getPhysicsPapers(String& outReport);
bool getApod(String& outReport);
bool getIssPosition(String& outReport);
bool askDeepSeek(const String& question, String& outReport);
void runAskFromLoop();
bool formatCoreDumpReport(String& outReport);
bool clearCoreDumpImage(String& outReport);
void handleCommand(const String& content, const String& authorId, const String& authorName,
                   const String& channelId, bool isDM);

// ====== SETUP / LOOP (MiniMe_Discord_Bot_II.ino) ======
void connectWiFi();
void connectGateway();

#endif
