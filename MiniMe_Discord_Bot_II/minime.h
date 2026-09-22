#ifndef MINIME_H
#define MINIME_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <time.h>
#include <string.h>
#include <stddef.h>

#include "secrets.h"
#if defined(MINIME_SECRETS_IS_EXAMPLE)
#error "Using secrets.example.h template — copy to secrets.h, fill values, remove MINIME_SECRETS_IS_EXAMPLE"
#endif
#include "minime_config.h"

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
uint8_t lcdFullLogCount();
bool lcdFullLogNewest(uint8_t fromNewest, char* buf, size_t bufLen);
uint8_t lcdSerialCount();
bool lcdSerialNewest(uint8_t fromNewest, char* buf, size_t bufLen);
uint32_t lcdLogGen(); // bumps when LOG or Serial ring changes

// ====== USER TRACKING ======
struct TrackedUser {
  String userId, userName;
  uint8_t status;  // 0 Off, 1 Idle, 2 On, 3 DND
  uint32_t useCount24h;
  bool active;
};

// ====== NTP / TIME ======
extern WiFiUDP ntpUDP;
extern NTPClient timeClient;
void updateLocalTime();
void formatLocalDateStr(char* buf, size_t bufLen);
void formatUptimeStr(char* buf, size_t bufLen);

// ====== DISCORD GATEWAY ======
extern WebSocketsClient gatewayWS;
extern DynamicJsonDocument* gwDoc;
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
                     const char* userAgent = "MiniMeBot/1.0",
                     const char* extraHeaders = nullptr);
uint8_t httpGetOpen(WiFiClient& client, const char* host, const String& path,
                    unsigned long headerTimeoutMs, bool& outChunked, int& outContentLength);
void setHttpOpenError(String& outReport, uint8_t err, const char* label);
bool httpsAwaitHeaders(unsigned long deadlineMs, bool pump, String& outStatus,
                       bool& chunked, int& contentLength);
bool readHttpBodyAfterHeaders(Client& client, bool chunked, int contentLength,
                              String& outBody, unsigned long deadlineMs);
bool discordIdLooksValid(const String& id);
bool discordRestGet(const String& path, String& outBody, String& outStatus);
String guildIdFromChannel(const String& channelId);
bool appendMembersFromGuild(const String& guildId, uint8_t maxToAdd);
bool fetchGuildMembersAtStartup();
bool sendDiscordMessage(const String& channelId, const String& content, bool suppressEmbeds = false);
String getSystemInfo();
void boardMemTotals(uint32_t& memFree, uint32_t& memTotal);
void uptimeDhms(unsigned long& days, unsigned long& hours, unsigned long& minutes, unsigned long& seconds);

// ====== DISPLAY (Arduino_GFX: ESP32QSPI + AXS15231B + Canvas) ======
extern Arduino_Canvas* gfx;
extern float dashTempC;
extern float dashTempF;
extern int lastServoDeg;
extern String transientLine1;
extern String transientLine2;
extern String transientLine3;
extern unsigned long transientUntilMs;
extern unsigned long lastDashMillis;
extern unsigned long lastDisplayActivityMillis;
extern unsigned long lastDashDrawMs;   // last full drawDashboard (incl flush)
extern unsigned long lastDashFlushMs;  // last gfx->flush() only
extern bool displayAsleep;
extern String lastEventLine;           // persistent left-panel "Event" (no footer strip)
extern bool alertDm;                   // sticky until owner !clear (no auto-expiry)
extern bool alertMention;              // sticky until owner !clear (no auto-expiry)
extern bool lcdThemeLight;             // LCD palette only (web theme is independent)
extern bool lcdLayoutLog;              // false=metrics|users; true=LOG|Serial overlay
bool setupDisplay();
void noteDisplayActivity(); // LCD backlight idle timer / wake
void noteLastEvent(const String& line); // sticky Event line (+ wakes display)
void drawDashboard();
void updateDisplay();
void showTransient(const String& line1, const String& line2 = "", const String& line3 = "",
                   unsigned long durationMs = 3000); // durationMs=0 -> 3s; !display uses 6000

bool lcdThemeChipHit(uint16_t x, uint16_t y);
bool lcdLayoutChipHit(uint16_t x, uint16_t y);
void toggleLcdTheme();
void toggleLcdLayout();
void setLcdThemeLight(bool light);
void setLcdLayoutLog(bool logMode);
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
void setServoAngle(int angleDeg);
// Core 0 uiTask only: non-blocking DS18B20 (shared OneWire — never call from Core 1).
bool pollTemperatureNonBlocking(float& tempC, float& tempF);
void setLedRgb(uint8_t r, uint8_t g, uint8_t b);
bool parseRgbTriplet(const String& args, uint8_t& r, uint8_t& g, uint8_t& b);
bool isOwner(const String& authorId);
void clearAlertFlags();

// ====== USERS / PRESENCE ======
extern TrackedUser trackedUsers[MAX_TRACKED_USERS];
extern String cachedGuildIds[MAX_CACHED_GUILDS];
extern uint8_t cachedGuildCount;
extern unsigned long usesWindowStartMillis;
const char* statusToWord(uint8_t s);
void initTrackedUsers();
void recordUserUse(const String& userId, const String& userName);
void applyPresencesArray(JsonArray presences);
void handlePresenceUpdate(JsonObject d);
String discordDisplayName(JsonVariantConst user);
int findUserIndex(const String& userId);
int findFreeTrackedSlot();
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
void handleCommand(const String& content, const String& authorId, const String& authorName,
                   const String& channelId, bool isDM);

// ====== SETUP / LOOP (MiniMe_Discord_Bot_II.ino) ======
void connectWiFi();
void connectGateway();

#endif
