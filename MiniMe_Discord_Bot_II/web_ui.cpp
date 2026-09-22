#include "minime.h"
#include <WebServer.h>
#include <string.h>
#include <new>
#include "k9dtv_logo_svg.h"
#include "k9dtv_logo_bright_svg.h"
#include "menu_chip_svg.h"
#include "web_assets.h"

// Display | SysInfo; under both LOG | Serial.
// Serial: all MmLog lines except FULL LOG dump body.
// LOG: body between [GW] === FULL LOG === and === END LOG === (drop/reconnect ring snapshot).
// Headers themselves are stripped; each FULL LOG start clears the LOG panel.

static WebServer webServer(WEB_UI_PORT);
static bool webUiReady = false;

static const unsigned WEB_FULL_N = 200;           // ring size; oldest dropped when full
static const unsigned WEB_SERIAL_N = 12;  // fits Serial panel; oldest dropped
static const uint8_t WEB_LOG_COLS = 96;
static const size_t WEB_FULL_MAX_BYTES = 20480UL; // logical text bytes (not RAM); slots are fixed 97B each
// RAM: webFullLines[200][97] ~19.4KB + webSerialLines[12][97] ~1.2KB in internal SRAM on purpose
// (PSRAM ring indexing is slower; tradeoff is intentional).
static_assert(WEB_FULL_N >= 1 && WEB_FULL_N <= 255, "WEB_FULL_N must fit uint8_t head/count");
static_assert(WEB_SERIAL_N >= 1 && WEB_SERIAL_N <= 255, "WEB_SERIAL_N must fit uint8_t head/count");

static char webFullLines[WEB_FULL_N][WEB_LOG_COLS + 1];
static uint8_t webFullHead = 0;
static uint8_t webFullCount = 0;
static size_t webFullBytes = 0;
static bool webInFullLog = false;
static uint32_t webLogGenCounter = 0;

static char webSerialLines[WEB_SERIAL_N][WEB_LOG_COLS + 1];
static uint8_t webSerialHead = 0;
static uint8_t webSerialCount = 0;

static char webLogAcc[WEB_LOG_COLS + 1];
static uint8_t webLogAccLen = 0;

static void ringPush(char lines[][WEB_LOG_COLS + 1], uint8_t n,
                     uint8_t& head, uint8_t& count, const char* text) {
  strncpy(lines[head], text, WEB_LOG_COLS);
  lines[head][WEB_LOG_COLS] = '\0';
  head = (uint8_t)((head + 1) % n);
  if (count < n) count++;
}

static void webFullClear() {
  webFullHead = 0;
  webFullCount = 0;
  webFullBytes = 0;
  for (uint8_t i = 0; i < WEB_FULL_N; i++) webFullLines[i][0] = '\0';
}

static void webFullDropOldest() {
  if (webFullCount == 0) return;
  size_t drop = 0;
  while (drop < WEB_LOG_COLS && webFullLines[webFullHead][drop]) drop++;
  if (webFullBytes >= drop) webFullBytes -= drop;
  else webFullBytes = 0;
  webFullLines[webFullHead][0] = '\0';
  webFullHead = (uint8_t)((webFullHead + 1) % WEB_FULL_N);
  webFullCount--;
}

static void webFullPush(const char* text) {
  if (!text) return;
  size_t add = 0;
  while (add < WEB_LOG_COLS && text[add]) add++;

  // Slot full: drop oldest before overwrite push.
  while (webFullCount >= WEB_FULL_N) webFullDropOldest();

  // Logical text budget (~20KB): drop oldest until the new line fits (not a full wipe).
  while (webFullBytes + add > WEB_FULL_MAX_BYTES && webFullCount > 0) {
    webFullDropOldest();
  }
  // Single line longer than budget: clear then push one line.
  if (webFullBytes + add > WEB_FULL_MAX_BYTES) {
    webFullClear();
  }
  ringPush(webFullLines, WEB_FULL_N, webFullHead, webFullCount, text);
  webFullBytes += add;
  webLogGenCounter++;
}

uint8_t lcdFullLogCount() {
  return webFullCount;
}

bool lcdFullLogNewest(uint8_t fromNewest, char* buf, size_t bufLen) {
  if (!buf || bufLen == 0 || fromNewest >= webFullCount) return false;
  uint8_t idx = (uint8_t)((webFullHead + WEB_FULL_N - 1 - fromNewest) % WEB_FULL_N);
  strncpy(buf, webFullLines[idx], bufLen - 1);
  buf[bufLen - 1] = '\0';
  return true;
}

uint8_t lcdSerialCount() {
  return webSerialCount;
}

bool lcdSerialNewest(uint8_t fromNewest, char* buf, size_t bufLen) {
  if (!buf || bufLen == 0 || fromNewest >= webSerialCount) return false;
  uint8_t idx = (uint8_t)((webSerialHead + WEB_SERIAL_N - 1 - fromNewest) % WEB_SERIAL_N);
  strncpy(buf, webSerialLines[idx], bufLen - 1);
  buf[bufLen - 1] = '\0';
  return true;
}

uint32_t lcdLogGen() {
  return webLogGenCounter;
}

static bool lineIsFullStart(const char* s) {
  return s && strcmp(s, "[GW] === FULL LOG ===") == 0;
}

static bool lineIsFullEnd(const char* s) {
  return s && strcmp(s, "[GW] === END LOG ===") == 0;
}

static void webLogCommitLine() {
  webLogAcc[webLogAccLen] = '\0';
  if (lineIsFullStart(webLogAcc)) {
    webInFullLog = true;
    // New dump replaces prior drop snapshot (headers themselves are omitted).
    webFullClear();
    webLogAccLen = 0;
    return;
  }
  if (lineIsFullEnd(webLogAcc)) {
    webInFullLog = false;
    webLogAccLen = 0;
    return;
  }
  if (webInFullLog) {
    // LOG panel: gateway drop ring body between FULL LOG / END LOG only.
    webFullPush(webLogAcc);
  } else {
    // Serial panel: normal MmLog (alive, boot, DROP still, etc.).
    ringPush(webSerialLines, WEB_SERIAL_N, webSerialHead, webSerialCount, webLogAcc);
    webLogGenCounter++;
  }
  webLogAccLen = 0;
}

void webLogFeed(const uint8_t* buffer, size_t size) {
  // Rings have no mutex vs handleStatus. Drop writes from Core 0 (uiTask must not MmLog).
  if (xPortGetCoreID() != 1) return;
  if (!buffer || size == 0) return;
  for (size_t i = 0; i < size; i++) {
    char c = (char)buffer[i];
    if (c == '\r') continue;
    if (c == '\n') {
      webLogCommitLine();
      continue;
    }
    if (webLogAccLen < WEB_LOG_COLS) {
      webLogAcc[webLogAccLen++] = c;
    } else {
      webLogCommitLine();
      webLogAcc[webLogAccLen++] = c;
    }
  }
}

static void dashFields(char* timeStr, size_t timeLen,
                       char* dateStr, size_t dateLen,
                       char* upStr, size_t upLen,
                       int& sigPct, int& heapPct, int& srvPct,
                       long& rssi, uint32_t& memFree, uint32_t& memTotal,
                       char* msg1, size_t msg1Len,
                       char* msg2, size_t msg2Len) {
  updateLocalTime();
  formatLocalTimeStr(timeStr, timeLen);
  formatLocalDateStr(dateStr, dateLen);
  formatUptimeStr(upStr, upLen);

  // Percents from LCD bar fills (dashSigBarW / dashHeapBarW / dashSrvBarW).
  rssi = WiFi.RSSI();
  sigPct = dashBarPct(dashSigBarW(rssi), DASH_SIG_HEAP_BAR_MAX);

  memFree = 0;
  memTotal = 0;
  boardMemTotals(memFree, memTotal);
  heapPct = dashBarPct(dashHeapBarW(memFree, memTotal), DASH_SIG_HEAP_BAR_MAX);
  srvPct = dashBarPct(dashSrvBarW(lastServoDeg), DASH_SRV_BAR_MAX);

  if (msg1 && msg1Len) {
    strncpy(msg1, lastEventLine.c_str(), msg1Len - 1);
    msg1[msg1Len - 1] = '\0';
  }
  if (msg2 && msg2Len) {
    msg2[0] = '\0';
    if (millis() < transientUntilMs) {
      size_t n = 0;
      const String* parts[3] = {&transientLine1, &transientLine2, &transientLine3};
      for (uint8_t p = 0; p < 3; p++) {
        if (!parts[p]->length()) continue;
        if (n && n + 1 < msg2Len) msg2[n++] = ' ';
        for (size_t i = 0; i < parts[p]->length() && n + 1 < msg2Len; i++) {
          msg2[n++] = (*parts[p])[i];
        }
        msg2[n] = '\0';
      }
    }
  }
}

// Buffer Print writes and flush as WebServer chunked body (no giant String).
class ChunkPrint : public Print {
 public:
  explicit ChunkPrint(WebServer& s) : srv(s), len(0) {}
  size_t write(uint8_t c) override {
    buf[len++] = (char)c;
    if (len >= sizeof(buf)) flushBuf();
    return 1;
  }
  size_t write(const uint8_t* data, size_t size) override {
    if (!data || size == 0) return 0;
    size_t done = 0;
    while (done < size) {
      size_t room = sizeof(buf) - len;
      if (room == 0) {
        flushBuf();
        room = sizeof(buf);
      }
      size_t n = size - done;
      if (n > room) n = room;
      memcpy(buf + len, data + done, n);
      len += n;
      done += n;
      if (len >= sizeof(buf)) flushBuf();
    }
    return done;
  }
  void finish() {
    flushBuf();
    srv.sendContent(""); // empty chunk ends Transfer-Encoding: chunked
  }
 private:
  WebServer& srv;
  char buf[512];
  size_t len;
  void flushBuf() {
    if (len == 0) return;
    srv.sendContent(buf, len);
    len = 0;
  }
};

static void printBrand(Print& out) {
  out.print(F("<div class=\"top\"><div class=\"top-row\">"));
  out.print(F("<button type=\"button\" id=\"theme-toggle\" class=\"theme-chip-trigger\" aria-pressed=\"false\" aria-label=\"Switch to light mode\">"));
  out.print(F("<img class=\"menu-chip-icon\" id=\"theme-chip-img\" src=\"/chip.svg\" width=\"64\" height=\"64\" alt=\"\" aria-hidden=\"true\">"));
  out.print(F("<span class=\"menu-chip-label\" aria-hidden=\"true\">"));
  out.print(F("<span class=\"theme-toggle-glyph\" id=\"theme-chip-glyph\">&#9728;</span>"));
  out.print(F("<span class=\"theme-toggle-text\" id=\"theme-chip-text\">Light</span></span></button>"));
  out.print(F("<header class=\"brand\">"));
  out.print(F("<a class=\"logo-link\" href=\"https://k9dtv.com\" target=\"_blank\" rel=\"noopener\">"));
  out.print(F("<img class=\"logo\" id=\"brand-logo\" src=\"/logo.svg\" width=\"343\" height=\"107\" alt=\"K9DTV\"></a></header>"));
  out.print(F("<button type=\"button\" id=\"layout-toggle\" class=\"theme-chip-trigger\" aria-pressed=\"false\" aria-label=\"Switch to log view\">"));
  out.print(F("<img class=\"menu-chip-icon\" id=\"layout-chip-img\" src=\"/chip.svg\" width=\"64\" height=\"64\" alt=\"\" aria-hidden=\"true\">"));
  out.print(F("<span class=\"menu-chip-label\" aria-hidden=\"true\">"));
  out.print(F("<span class=\"theme-toggle-text\" id=\"layout-chip-text\">Display</span></span></button>"));
  out.print(F("</div><p class=\"sub\">MiniMe A Discord Server APP</p></div>"));
}

static void sendNoCacheHeaders() {
  webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "0");
}

static void streamRootHtml(Print& out) {
  out.print(F("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"));
  out.print(F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"));
  out.print(F("<meta http-equiv=\"Cache-Control\" content=\"no-store, no-cache, must-revalidate, max-age=0\">"));
  out.print(F("<meta http-equiv=\"Pragma\" content=\"no-cache\">"));
  out.print(F("<meta http-equiv=\"Expires\" content=\"0\">"));
  out.print(F("<script>"));
  out.print(FPSTR(WEB_UI_BOOT_JS));
  out.print(F("</script>"));
  out.print(F("<title>MiniMe</title><style>"));
  out.print(FPSTR(WEB_UI_CSS));
  out.print(F("</style></head><body><main>"));
  printBrand(out);

  out.print(F("<div class=\"layout\">"));
  out.print(F("<section class=\"box\" id=\"box-metrics\"><h2>Display · v"));
  out.print(MINIME_VERSION);
  out.print(F("</h2>"));
  out.print(F("<div id=\"metrics\" class=\"dash muted\">Loading...</div></section>"));
  out.print(F("<section class=\"box\" id=\"box-users\"><h2>Users</h2>"));
  out.print(F("<div id=\"users\" class=\"users muted\">Loading...</div></section>"));
  out.print(F("<section class=\"box\" id=\"box-logfile\"><h2>LOG</h2>"));
  out.print(F("<div id=\"logfile\" class=\"serial\"><div class=\"empty\">Waiting...</div></div></section>"));
  out.print(F("<section class=\"box\" id=\"box-serial\"><h2>Serial</h2>"));
  out.print(F("<div id=\"serial\" class=\"serial noscroll\"><div class=\"empty\">Waiting...</div></div></section>"));
  out.print(F("<div id=\"err\" class=\"err\" hidden></div>"));
  out.print(F("</div></main><script>var POLL_MS="));
  char pollBuf[16];
  snprintf(pollBuf, sizeof(pollBuf), "%lu", (unsigned long)WEB_STATUS_POLL_MS);
  out.print(pollBuf);
  out.print(F(";</script><script>"));
  out.print(FPSTR(WEB_UI_JS));
  out.print(F("</script></body></html>"));
}

template <size_t N>
static void appendRingToJsonArray(JsonArray arr, const char (&lines)[N][WEB_LOG_COLS + 1],
                                  uint8_t head, uint8_t count) {
  uint8_t start = (uint8_t)((head + N - count) % N);
  for (uint8_t i = 0; i < count; i++) {
    uint8_t idx = (uint8_t)((start + i) % N);
    arr.add(lines[idx]);
  }
}

static int roundTempHalfAway(float v) {
  return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

// Allocated once in setupWebUi() (after PSRAM is up), then reused with clear().
static SpiRamJsonDocument* statusDoc = nullptr;
static const size_t STATUS_DOC_BYTES = 32768;

static void handleRoot() {
  sendNoCacheHeaders();
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html; charset=utf-8", "");
  ChunkPrint out(webServer);
  streamRootHtml(out);
  out.finish();
}

static void handleStatus() {
  sendNoCacheHeaders();
  if (!statusDoc) {
    webServer.send(500, "application/json", "{\"err\":\"statusDoc\"}");
    return;
  }

  // Stack buffers must stay live until serializeJson finishes (AJ6 stores const char* by ptr).
  char timeStr[12], dateStr[24], upStr[28], msg1[40], msg2[128], ipBuf[16];
  int sigPct = 0, heapPct = 0, srvPct = 0;
  long rssi = 0;
  uint32_t memFree = 0, memTotal = 0;
  dashFields(timeStr, sizeof(timeStr), dateStr, sizeof(dateStr), upStr, sizeof(upStr),
             sigPct, heapPct, srvPct, rssi, memFree, memTotal,
             msg1, sizeof(msg1), msg2, sizeof(msg2));

  SpiRamJsonDocument& doc = *statusDoc;
  doc.clear();
  doc["gw"] = gatewayConnected;
  doc["identified"] = identified;
  doc["botOnline"] = (botDiscordStatus == 2);
  doc["time"] = timeStr;
  doc["date"] = dateStr;
  doc["uptime"] = upStr;
  bool tempOk = (dashTempC > -998.0f);
  doc["tempOk"] = tempOk;
  doc["tempF"] = tempOk ? roundTempHalfAway(dashTempF) : 0;
  doc["tempC"] = tempOk ? roundTempHalfAway(dashTempC) : 0;
  doc["rssi"] = (int)rssi;
  doc["sigPct"] = sigPct;
  doc["heapFree"] = memFree;
  doc["heapTotal"] = memTotal;
  doc["heapPct"] = heapPct;
  {
    uint32_t psFree = 0, psTotal = 0;
    boardPsramTotals(psFree, psTotal);
    doc["psramFree"] = psFree;
    doc["psramTotal"] = psTotal;
  }
  doc["cpuMhz"] = (unsigned)getCpuFrequencyMhz();
  doc["servo"] = lastServoDeg;
  doc["srvPct"] = srvPct;
  {
    IPAddress ip = WiFi.localIP();
    snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u",
             (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
  }
  doc["ip"] = ipBuf;
  static char otaHost[40];
  static bool otaHostReady = false;
  if (!otaHostReady) {
    snprintf(otaHost, sizeof(otaHost), "%s.local", OTA_HOSTNAME);
    otaHostReady = true;
  }
  doc["ota"] = otaHost;
  doc["lcd"] = displayAsleep.load() ? "asleep" : "awake";
  doc["dashFlushMs"] = (unsigned long)lastDashFlushMs;
  doc["dashDrawMs"] = (unsigned long)lastDashDrawMs;
  doc["dashRefreshMs"] = DASH_REFRESH_MS;
  doc["dm"] = alertDm;
  doc["mention"] = alertMention;
  doc["httpsBusy"] = httpsInUse;
  doc["lastEvent"] = lastEventLine.c_str();
  doc["msg1"] = msg1;
  doc["msg2"] = msg2;

  JsonArray users = doc.createNestedArray("users");
  uint8_t nActive = 0;
  for (uint8_t row = 0; row < MAX_TRACKED_USERS; row++) {
    JsonObject u = users.createNestedObject();
    const char* name = "---";
    if (trackedUsers[row].active) {
      nActive++;
      if (trackedUsers[row].userName.length()) name = trackedUsers[row].userName.c_str();
      else name = trackedUsers[row].userId.c_str();
    }
    u["name"] = name;
    u["status"] = statusToWord(trackedUsers[row].active ? trackedUsers[row].status : 0);
    // Number avoids a per-row temp char[] dangling-pointer under AJ6 zero-copy.
    u["bot"] = (unsigned long)(trackedUsers[row].active ? trackedUsers[row].useCount24h : 0);
  }
  doc["usersActive"] = nActive;
  doc["usersMax"] = MAX_TRACKED_USERS;

  JsonArray fulllog = doc.createNestedArray("fulllog");
  appendRingToJsonArray(fulllog, webFullLines, webFullHead, webFullCount);
  JsonArray serialArr = doc.createNestedArray("serial");
  appendRingToJsonArray(serialArr, webSerialLines, webSerialHead, webSerialCount);

  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "application/json", "");
  ChunkPrint out(webServer);
  serializeJson(doc, out);
  out.finish();
}

static void handleLogo() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", K9DTV_LOGO_SVG);
}

static void handleLogoBright() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", K9DTV_LOGO_BRIGHT_SVG);
}

static void handleChip() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", MENU_CHIP_SVG);
}

static void handleChipBright() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", MENU_CHIP_BRIGHT_SVG);
}

void setupWebUi() {
  webFullClear();
  webInFullLog = false;
  for (uint8_t i = 0; i < WEB_SERIAL_N; i++) webSerialLines[i][0] = '\0';
  webSerialHead = 0;
  webSerialCount = 0;
  webLogAccLen = 0;
  if (!statusDoc) {
    statusDoc = new (std::nothrow) SpiRamJsonDocument(STATUS_DOC_BYTES);
  }
  if (!statusDoc || statusDoc->capacity() == 0) {
    MmLog.println(F("Fatal: statusDoc alloc failed (PSRAM?)"));
    showTransient("Fatal", "No statusDoc");
    // Same policy as gwDoc: delay() feeds TWDT; halt until power cycle.
    while (true) {
      delay(1000);
    }
  }
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/logo.svg", HTTP_GET, handleLogo);
  webServer.on("/logo-bright.svg", HTTP_GET, handleLogoBright);
  webServer.on("/chip.svg", HTTP_GET, handleChip);
  webServer.on("/chip-bright.svg", HTTP_GET, handleChipBright);
  webServer.on("/api/status", HTTP_GET, handleStatus);
  webServer.begin();
  webUiReady = true;
  MmLog.print("[WEB] http://");
  MmLog.print(WiFi.localIP().toString());
  MmLog.print(":");
  MmLog.println(WEB_UI_PORT);
}

void pumpWebUi() {
  if (!webUiReady) return;
  webServer.handleClient();
}
