#include "web_ui_internal.h"
#include <string.h>

// Display = metrics | users; Log = LOG | Serial (LOG left, Serial right -- matches LCD).
// Serial: all MmLog lines except FULL LOG dump body.
// LOG: body between [GW] === FULL LOG === and === END LOG === (drop/reconnect ring snapshot).
// Headers themselves are stripped; each FULL LOG start clears the LOG panel.
// Rings are WEB_*_N == DASH_LOG_ROWS (55).

WebServer webServer(WEB_UI_PORT);
static bool webUiReady = false;

// Session token when WEB_UI_PASSWORD is set (hex of 16 random bytes + NUL).
static char webSessionToken[33] = {0};

bool webUiAuthEnabled() {
  return WEB_UI_PASSWORD[0] != '\0';
}

static void webAuthMintToken() {
  for (int i = 0; i < 16; i++) {
    uint8_t b = (uint8_t)(esp_random() & 0xFF);
    static const char* hex = "0123456789abcdef";
    webSessionToken[i * 2] = hex[b >> 4];
    webSessionToken[i * 2 + 1] = hex[b & 0x0F];
  }
  webSessionToken[32] = '\0';
}

static bool webAuthPassEq(const char* got) {
  const char* exp = WEB_UI_PASSWORD;
  if (!got) got = "";
  size_t n = strlen(exp);
  size_t m = strlen(got);
  size_t lim = (n > m) ? n : m;
  if (lim == 0) return n == m;
  uint8_t diff = (uint8_t)(n ^ m);
  for (size_t i = 0; i < lim; i++) {
    char a = (i < n) ? exp[i] : 0;
    char b = (i < m) ? got[i] : 0;
    diff |= (uint8_t)(a ^ b);
  }
  return diff == 0;
}

static bool webAuthTokenMatches(const String& tok) {
  if (!webSessionToken[0] || tok.length() != 32) return false;
  uint8_t diff = 0;
  for (int i = 0; i < 32; i++) {
    diff |= (uint8_t)((uint8_t)tok.charAt(i) ^ (uint8_t)webSessionToken[i]);
  }
  return diff == 0;
}

bool webUiAuthOk() {
  if (!webUiAuthEnabled()) return true;
  if (webServer.hasHeader("X-MiniMe-Token") && webAuthTokenMatches(webServer.header("X-MiniMe-Token"))) {
    return true;
  }
  if (webServer.hasHeader("Cookie")) {
    String c = webServer.header("Cookie");
    int idx = c.indexOf("mm_tok=");
    if (idx >= 0) {
      String t = c.substring(idx + 7);
      int semi = t.indexOf(';');
      if (semi >= 0) t = t.substring(0, semi);
      t.trim();
      if (webAuthTokenMatches(t)) return true;
    }
  }
  return false;
}

void webUiSendUnauthorized() {
  webUiSendNoCacheHeaders();
  webServer.send(401, "application/json", "{\"err\":\"auth\",\"needAuth\":true}");
}

void webUiHandleLogin() {
  webUiSendNoCacheHeaders();
  if (!webUiAuthEnabled()) {
    webServer.send(200, "application/json", "{\"ok\":1,\"auth\":false,\"token\":\"\"}");
    return;
  }
  String pass;
  if (webServer.hasArg("pass")) pass = webServer.arg("pass");
  else if (webServer.hasArg("password")) pass = webServer.arg("password");
  if (!webAuthPassEq(pass.c_str())) {
    delay(150); // mild brute-force slowdown
    webServer.send(401, "application/json", "{\"err\":\"badpass\",\"needAuth\":true}");
    return;
  }
  webAuthMintToken();
  String cookie = String("mm_tok=") + webSessionToken + "; Path=/; SameSite=Strict";
  webServer.sendHeader("Set-Cookie", cookie);
  char buf[80];
  snprintf(buf, sizeof(buf), "{\"ok\":1,\"auth\":true,\"token\":\"%s\"}", webSessionToken);
  webServer.send(200, "application/json", buf);
}

static const size_t WEB_FULL_MAX_BYTES = 20480UL; // logical text bytes (not RAM); slots are fixed 97B each
// RAM: webFullLines[55][97] + webSerialLines[55][97] ~10.7KB internal SRAM.
static_assert(WEB_FULL_N >= 1 && WEB_FULL_N <= 255, "WEB_FULL_N must fit uint8_t head/count");
static_assert(WEB_SERIAL_N >= 1 && WEB_SERIAL_N <= 255, "WEB_SERIAL_N must fit uint8_t head/count");

// LOG + Serial rings live only in this file. Callers use lcdFullLogCount / lcdFullLogNewest
// (and Serial twins) -- do not expose head/count/arrays via web_ui_internal.h.
static char webFullLines[WEB_FULL_N][WEB_LOG_COLS + 1];
static uint8_t webFullHead = 0;
static uint8_t webFullCount = 0;
static size_t webFullBytes = 0;
static bool webInFullLog = false;
static uint32_t webLogGenCounter = 0;

static char webSerialLines[WEB_SERIAL_N][WEB_LOG_COLS + 1];
static uint8_t webSerialHead = 0;
std::atomic<uint32_t> mmLogDropCore0{0};

static uint8_t webSerialCount = 0;

// Core 0 -> Core 1 log bridge: webLogFeed cannot touch Serial rings from Core 0 (races handleStatus).
// Lines land in this ring; drainCore0Logs() on Core 1 prints via MmLog.
enum { C0_LOG_N = 8 };
static char c0Pending[C0_LOG_N][WEB_LOG_COLS + 1];
static uint8_t c0LogHead = 0;
static uint8_t c0LogCount = 0;
static portMUX_TYPE c0LogMux = portMUX_INITIALIZER_UNLOCKED;
static char c0Acc[WEB_LOG_COLS + 1];
static uint8_t c0AccLen = 0;

static void c0EnqueueLine(const char* text) {
  if (!text || !text[0]) return;
  portENTER_CRITICAL(&c0LogMux);
  if (c0LogCount >= C0_LOG_N) {
    portEXIT_CRITICAL(&c0LogMux);
    mmLogDropCore0.fetch_add(1);
    return;
  }
  uint8_t slot = (uint8_t)((c0LogHead + c0LogCount) % C0_LOG_N);
  strncpy(c0Pending[slot], text, WEB_LOG_COLS);
  c0Pending[slot][WEB_LOG_COLS] = '\0';
  c0LogCount++;
  portEXIT_CRITICAL(&c0LogMux);
}

void drainCore0Logs() {
  if (xPortGetCoreID() != 1) return;
  for (;;) {
    char line[WEB_LOG_COLS + 1];
    bool have = false;
    portENTER_CRITICAL(&c0LogMux);
    if (c0LogCount > 0) {
      strncpy(line, c0Pending[c0LogHead], WEB_LOG_COLS);
      line[WEB_LOG_COLS] = '\0';
      c0LogHead = (uint8_t)((c0LogHead + 1) % C0_LOG_N);
      c0LogCount--;
      have = true;
    }
    portEXIT_CRITICAL(&c0LogMux);
    if (!have) break;
    MmLog.print(F("[C0] "));
    MmLog.println(line);
  }
}

// Last N Discord replies posted because a command failed (busy / fetch / sensor / post fail).
static char cmdErrLines[CMD_ERR_RING_N][WEB_LOG_COLS + 1];
static uint8_t cmdErrHead = 0;
static uint8_t cmdErrCount = 0;

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

void noteCmdErrorReply(const char* msg) {
  if (!msg || !msg[0]) return;
  // Core 1 only (same rule as webLogFeed); callers are command/REST paths on Core 1.
  if (xPortGetCoreID() != 1) return;

  char line[WEB_LOG_COLS + 1];
  size_t n = 0;
  for (size_t i = 0; msg[i] && n + 1 < sizeof(line); i++) {
    char c = msg[i];
    if (c == '\r') continue;
    if (c == '\n' || c == '\t') c = ' ';
    // Skip markdown noise for LCD/Serial one-liners.
    if (c == '*' || c == '`' || c == '_') continue;
    if (c == ' ' && (n == 0 || line[n - 1] == ' ')) continue;
    line[n++] = c;
  }
  while (n > 0 && line[n - 1] == ' ') n--;
  line[n] = '\0';
  if (n == 0) return;

  ringPush(cmdErrLines, CMD_ERR_RING_N, cmdErrHead, cmdErrCount, line);
  // Serial panel (Log layout right): same one-liner operators can see without !sys.
  MmLog.print("[CMDERR] ");
  MmLog.println(line);
}

uint8_t cmdErrorReplyCount() {
  return cmdErrCount;
}

bool cmdErrorReplyNewest(uint8_t fromNewest, char* buf, size_t bufLen) {
  if (!buf || bufLen == 0 || fromNewest >= cmdErrCount) return false;
  uint8_t idx = (uint8_t)((cmdErrHead + CMD_ERR_RING_N - 1 - fromNewest) % CMD_ERR_RING_N);
  strncpy(buf, cmdErrLines[idx], bufLen - 1);
  buf[bufLen - 1] = '\0';
  return true;
}

static bool lineIsFullStart(const char* s) {
  // Require gateway prefix so !display / !ask text cannot flip LOG routing.
  return s && strstr(s, "[GW] === FULL LOG ===") != nullptr;
}

static bool lineIsFullEnd(const char* s) {
  return s && strstr(s, "[GW] === END LOG ===") != nullptr;
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
  if (!buffer || size == 0) return;
  // Rings have no mutex vs handleStatus. Core 0 enqueues lines for Core 1 drain.
  if (xPortGetCoreID() != 1) {
    for (size_t i = 0; i < size; i++) {
      char c = (char)buffer[i];
      if (c == '\r') continue;
      if (c == '\n') {
        c0Acc[c0AccLen] = '\0';
        if (c0AccLen) c0EnqueueLine(c0Acc);
        c0AccLen = 0;
        continue;
      }
      if (c0AccLen < WEB_LOG_COLS) {
        c0Acc[c0AccLen++] = c;
      } else {
        c0Acc[WEB_LOG_COLS] = '\0';
        c0EnqueueLine(c0Acc);
        c0AccLen = 0;
        c0Acc[c0AccLen++] = c;
      }
    }
    return;
  }
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

static void handleControlsPost() {
  webUiSendNoCacheHeaders();
  if (!webUiAuthOk()) {
    webUiSendUnauthorized();
    return;
  }
  if (webServer.hasArg("action")) {
    const String act = webServer.arg("action");
    if (act == "cancel") {
      controlsCancel();
    } else if (act == "save") {
      controlsSave();
    } else if (act == "resetprefs") {
      factoryResetSettings();
    } else if (act == "enter") {
      // Web opened Controls -- snapshot current values.
      controlsSnapshotEnter(0);
    }
  }
  if (webServer.hasArg("bright")) {
    setUiBrightPct((uint8_t)constrain(webServer.arg("bright").toInt(), 0, 100));
  }
  if (webServer.hasArg("vol")) {
    setUiVolPct((uint8_t)constrain(webServer.arg("vol").toInt(), 0, 100));
  }
  if (webServer.hasArg("notify")) {
    setUiNotifyOn(webServer.arg("notify") == "1" || webServer.arg("notify") == "true");
  }
  if (webServer.hasArg("ticks")) {
    setUiTicksOn(webServer.arg("ticks") == "1" || webServer.arg("ticks") == "true");
  }
  if (webServer.hasArg("sound")) {
    setUiSoundOn(webServer.arg("sound") == "1" || webServer.arg("sound") == "true");
  }
  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"ok\":1,\"bright\":%u,\"vol\":%u,\"notify\":%s,\"ticks\":%s,\"sound\":%s}",
           (unsigned)uiBrightPct.load(), (unsigned)uiVolPct.load(),
           uiNotifyOn.load() ? "true" : "false",
           uiTicksOn.load() ? "true" : "false",
           uiSoundOn.load() ? "true" : "false");
  webServer.send(200, "application/json", buf);
}

void setupWebUi() {
  webFullClear();
  webInFullLog = false;
  for (uint8_t i = 0; i < WEB_SERIAL_N; i++) webSerialLines[i][0] = '\0';
  webSerialHead = 0;
  webSerialCount = 0;
  webLogAccLen = 0;
  if (!statusDoc) {
    statusDoc = newSpiRamJsonDoc();
  }
  if (!statusDoc) {
    MmLog.println(F("Fatal: statusDoc alloc failed (PSRAM?)"));
    showTransient("Fatal", "No statusDoc");
    while (true) {
      delay(1000);
    }
  }
  {
    void* probe = mmSpiRamJsonAlloc().allocate(256);
    if (!probe) {
      MmLog.println(F("Fatal: statusDoc PSRAM probe failed"));
      showTransient("Fatal", "No statusDoc");
      while (true) {
        delay(1000);
      }
    }
    mmSpiRamJsonAlloc().deallocate(probe);
  }
  static const char* collectHdrs[] = {"X-MiniMe-Token", "Cookie"};
  webServer.collectHeaders(collectHdrs, 2);
  webServer.on("/", HTTP_GET, webUiHandleRoot);
  webServer.on("/ui.css", HTTP_GET, webUiHandleUiCss);
  webServer.on("/ui.js", HTTP_GET, webUiHandleUiJs);
  webServer.on("/logo.svg", HTTP_GET, webUiHandleLogo);
  webServer.on("/logo-bright.svg", HTTP_GET, webUiHandleLogoBright);
  webServer.on("/logo-spin.svg", HTTP_GET, webUiHandleLogoSpin);
  webServer.on("/logo-spin-bright.svg", HTTP_GET, webUiHandleLogoSpinBright);
  webServer.on("/chip.svg", HTTP_GET, webUiHandleChip);
  webServer.on("/chip-bright.svg", HTTP_GET, webUiHandleChipBright);
  webServer.on("/mark-left.svg", HTTP_GET, webUiHandleMarkLeft);
  webServer.on("/mark-left-bright.svg", HTTP_GET, webUiHandleMarkLeftBright);
  webServer.on("/mark-right.svg", HTTP_GET, webUiHandleMarkRight);
  webServer.on("/mark-right-bright.svg", HTTP_GET, webUiHandleMarkRightBright);
  webServer.on("/api/status", HTTP_GET, webUiHandleStatus);
  webServer.on("/api/controls", HTTP_POST, handleControlsPost);
  webServer.on("/api/login", HTTP_POST, webUiHandleLogin);
  webServer.begin();
  webUiReady = true;
  MmLog.print("[WEB] http://");
  MmLog.print(WiFi.localIP().toString());
  MmLog.print(":");
  MmLog.print(WEB_UI_PORT);
  if (webUiAuthEnabled()) MmLog.println(F(" (auth on)"));
  else MmLog.println(F(" (auth off)"));
}

void pumpWebUi() {
  if (!webUiReady) return;
  webServer.handleClient();
}
