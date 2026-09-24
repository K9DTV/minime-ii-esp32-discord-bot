#include "web_ui_internal.h"
#include <string.h>
#include <new>
#include "k9dtv_logo_svg.h"
#include "k9dtv_logo_bright_svg.h"
#include "k9dtv_logo_spin_svg.h"
#include "k9dtv_logo_spin_bright_svg.h"
#include "menu_chip_svg.h"
#include "k9_mark_icon_svg.h"
#include "k9_mark_icon_bright_svg.h"
#include "k9_mark_icon_right_svg.h"
#include "k9_mark_icon_right_bright_svg.h"
#include "web_assets.h"

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
    uiOverlayCopyEvent(msg1, msg1Len);
  }
  if (msg2 && msg2Len) {
    msg2[0] = '\0';
    char t1[UI_TRANSIENT_COLS], t2[UI_TRANSIENT_COLS], t3[UI_TRANSIENT_COLS];
    unsigned long until = 0;
    uiOverlayCopyTransient(t1, sizeof(t1), t2, sizeof(t2), t3, sizeof(t3), &until);
    if (millis() < until) {
      size_t n = 0;
      const char* parts[3] = {t1, t2, t3};
      for (uint8_t p = 0; p < 3; p++) {
        if (!parts[p][0]) continue;
        if (n && n + 1 < msg2Len) msg2[n++] = ' ';
        for (size_t i = 0; parts[p][i] && n + 1 < msg2Len; i++) {
          msg2[n++] = parts[p][i];
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
  out.print(F("<img class=\"logo\" id=\"brand-logo\" src=\"/logo.svg\" alt=\"K9DTV\"></a></header>"));
  out.print(F("<button type=\"button\" id=\"layout-toggle\" class=\"theme-chip-trigger\" aria-pressed=\"false\" aria-label=\"Cycle display log controls\">"));
  out.print(F("<span class=\"menu-chip-menus\" aria-hidden=\"true\">Menus</span>"));
  out.print(F("<img class=\"menu-chip-icon\" id=\"layout-chip-img\" src=\"/chip.svg\" width=\"64\" height=\"64\" alt=\"\" aria-hidden=\"true\">"));
  out.print(F("<span class=\"menu-chip-label\" aria-hidden=\"true\">"));
  out.print(F("<span class=\"theme-toggle-text\" id=\"layout-chip-text\">Display</span></span></button>"));
  out.print(F("</div><p class=\"sub\" id=\"brand-sub\">MiniMe-II A Discord Bot</p></div>"));
}

void webUiSendNoCacheHeaders() {
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
  out.print(F("<title>MiniMe-II</title>"));
  // CSS/JS as separate PROGMEM sends (send_P) -- not inlined every / (was starving Wi-Fi).
  out.print(F("<link rel=\"stylesheet\" href=\"/ui.css?v="));
  out.print(MINIME_VERSION);
  out.print(F("\">"));
  out.print(F("</head><body><main>"));
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
  out.print(F("<div id=\"serial\" class=\"serial\"><div class=\"empty\">Waiting...</div></div></section>"));
  // Controls = scaled LCD twin; Cancel/Save art = mark SVGs only.
  out.print(F("<section class=\"box box-lcd-ctrl\" id=\"box-ctrl-sliders\">"));
  out.print(F("<div class=\"ctrl-panel\">"));
  out.print(F("<div class=\"ctrl-title\">Controls</div>"));
  out.print(F("<div class=\"ctrl-row\"><div class=\"ctrl-lab\" id=\"ctrl-bright-lab\">Brightness 100%</div>"));
  out.print(F("<div class=\"ctrl-track\" id=\"ctrl-bright-track\" style=\"--pct:100\">"));
  out.print(F("<div class=\"ctrl-fill\"></div><div class=\"ctrl-knob\"></div>"));
  out.print(F("<input id=\"ctrl-bright\" type=\"range\" min=\"0\" max=\"100\" value=\"100\" aria-label=\"Brightness\">"));
  out.print(F("</div></div>"));
  out.print(F("<div class=\"ctrl-row\"><div class=\"ctrl-lab\" id=\"ctrl-vol-lab\">Volume 100%</div>"));
  out.print(F("<div class=\"ctrl-track\" id=\"ctrl-vol-track\" style=\"--pct:100\">"));
  out.print(F("<div class=\"ctrl-fill\"></div><div class=\"ctrl-knob\"></div>"));
  out.print(F("<input id=\"ctrl-vol\" type=\"range\" min=\"0\" max=\"100\" value=\"100\" aria-label=\"Volume\">"));
  out.print(F("</div></div>"));
  out.print(F("<button type=\"button\" class=\"dog-btn dog-cancel\" id=\"ctrl-cancel\" aria-label=\"Cancel\">"));
  out.print(F("<img id=\"dog-left-img\" width=\"48\" height=\"32\" alt=\"\">"));
  out.print(F("<span class=\"dog-lab\">Cancel</span></button>"));
  out.print(F("</div></section>"));
  out.print(F("<section class=\"box box-lcd-ctrl\" id=\"box-ctrl-toggles\">"));
  out.print(F("<div class=\"ctrl-panel\">"));
  out.print(F("<div class=\"ctrl-title\">Toggles</div>"));
  out.print(F("<button type=\"button\" class=\"tog\" id=\"ctrl-sound\" aria-pressed=\"true\"><span class=\"lab\">Sound</span><span class=\"st\">ON</span></button>"));
  out.print(F("<button type=\"button\" class=\"tog\" id=\"ctrl-ticks\" aria-pressed=\"true\"><span class=\"lab\">Ticks</span><span class=\"st\">ON</span></button>"));
  out.print(F("<button type=\"button\" class=\"tog\" id=\"ctrl-notify\" aria-pressed=\"true\"><span class=\"lab\">Notify</span><span class=\"st\">ON</span></button>"));
  out.print(F("<button type=\"button\" class=\"dog-btn dog-save\" id=\"ctrl-save\" aria-label=\"Save\">"));
  out.print(F("<img id=\"dog-right-img\" width=\"48\" height=\"32\" alt=\"\">"));
  out.print(F("<span class=\"dog-lab\">Save</span></button>"));
  out.print(F("</div></section>"));
  out.print(F("<div id=\"err\" class=\"err\" hidden></div>"));
  out.print(F("</div></main><script>var POLL_MS="));
  char pollBuf[16];
  snprintf(pollBuf, sizeof(pollBuf), "%lu", (unsigned long)WEB_STATUS_POLL_MS);
  out.print(pollBuf);
  out.print(F(";</script><script src=\"/ui.js?v="));
  out.print(MINIME_VERSION);
  out.print(F("\"></script></body></html>"));
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
JsonDocument* statusDoc = nullptr;
static const size_t STATUS_DOC_BYTES = 49152; // soft cap; measureJson checked before send

void webUiHandleRoot() {
  webUiSendNoCacheHeaders();
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html; charset=utf-8", "");
  ChunkPrint out(webServer);
  streamRootHtml(out);
  out.finish();
}

void webUiHandleStatus() {
  webUiSendNoCacheHeaders();
  if (!statusDoc) {
    webServer.send(500, "application/json", "{\"err\":\"statusDoc\"}");
    return;
  }

  // Stack buffers must stay live until serializeJson finishes (AJ7 may store const char* by ptr).
  char timeStr[12], dateStr[24], upStr[28], msg1[40], msg2[128], ipBuf[16], eventBuf[UI_EVENT_COLS];
  int sigPct = 0, heapPct = 0, srvPct = 0;
  long rssi = 0;
  uint32_t memFree = 0, memTotal = 0;
  dashFields(timeStr, sizeof(timeStr), dateStr, sizeof(dateStr), upStr, sizeof(upStr),
             sigPct, heapPct, srvPct, rssi, memFree, memTotal,
             msg1, sizeof(msg1), msg2, sizeof(msg2));
  uiOverlayCopyEvent(eventBuf, sizeof(eventBuf));

  JsonDocument& doc = *statusDoc;
  doc.clear();
  // Same three-state as LCD DashSnap: -1 Bad, 0 Wait, 1 Good
  doc["gw"] = !gatewayConnected ? -1 : (identified ? 1 : 0);
  doc["identified"] = identified;
  doc["botOnline"] = (botDiscordStatus == 2);
  doc["time"] = timeStr;
  doc["date"] = dateStr;
  doc["uptime"] = upStr;
  float tc = 0, tf = 0;
  bool hadSample = false, tempOk = false;
  dashTempSnapshot(tc, tf, hadSample, tempOk);
  doc["tempOk"] = tempOk;
  doc["tempF"] = tempOk ? roundTempHalfAway(tf) : 0;
  doc["tempC"] = tempOk ? roundTempHalfAway(tc) : 0;
  doc["tempStale"] = hadSample && !tempOk;
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
    doc["psramPct"] = (psTotal > 0)
      ? dashBarPct(dashHeapBarW(psFree, psTotal), DASH_SIG_HEAP_BAR_MAX)
      : 0;
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
  doc["ver"] = MINIME_VERSION;
  doc["lcd"] = displayAsleep.load() ? "asleep" : "awake";
  doc["dashFlushMs"] = (unsigned long)lastDashFlushMs;
  doc["dashDrawMs"] = (unsigned long)lastDashDrawMs;
  doc["dashRefreshMs"] = DASH_REFRESH_MS;
  doc["dm"] = alertDm.load();
  doc["mention"] = alertMention.load();
  doc["httpsBusy"] = httpsInUse;
  doc["lastEvent"] = eventBuf;
  doc["mmLogDropCore0"] = mmLogDropCore0.load();
  doc["msg1"] = msg1;
  doc["msg2"] = msg2;
  doc["bright"] = uiBrightPct.load();
  doc["vol"] = uiVolPct.load();
  doc["notify"] = uiNotifyOn.load();
  doc["ticks"] = uiTicksOn.load();
  doc["sound"] = uiSoundOn.load();

  JsonArray users = doc["users"].to<JsonArray>();
  uint8_t nActive = 0;
  for (uint8_t row = 0; row < MAX_TRACKED_USERS; row++) {
    JsonObject u = users.add<JsonObject>();
    const char* name = "---";
    if (trackedUsers[row].active) {
      nActive++;
      if (trackedUsers[row].userName[0]) name = trackedUsers[row].userName;
      else name = trackedUsers[row].userId;
    }
    u["name"] = name;
    u["status"] = statusToWord(trackedUsers[row].active ? trackedUsers[row].status : 0);
    // Number avoids a per-row temp char[] dangling-pointer under AJ zero-copy.
    u["bot"] = (unsigned long)(trackedUsers[row].active ? trackedUsers[row].useCount24h : 0);
  }
  doc["usersActive"] = nActive;
  doc["usersMax"] = MAX_TRACKED_USERS;

  JsonArray fulllog = doc["fulllog"].to<JsonArray>();
  appendRingToJsonArray(fulllog, webFullLines, webFullHead, webFullCount);
  JsonArray serialArr = doc["serial"].to<JsonArray>();
  appendRingToJsonArray(serialArr, webSerialLines, webSerialHead, webSerialCount);

  size_t need = measureJson(doc);
  if (need == 0 || need >= STATUS_DOC_BYTES) {
    webServer.send(500, "application/json", "{\"err\":\"statusJsonOverflow\"}");
    return;
  }
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "application/json", "");
  ChunkPrint out(webServer);
  serializeJson(doc, out);
  out.finish();
}

void webUiHandleLogo() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", K9DTV_LOGO_SVG);
}

void webUiHandleUiCss() {
  // send_P = one PROGMEM blast; was chunked-inline on every / and choked Wi-Fi.
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "text/css; charset=utf-8", WEB_UI_CSS);
}

void webUiHandleUiJs() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "application/javascript; charset=utf-8", WEB_UI_JS);
}

void webUiHandleLogoBright() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", K9DTV_LOGO_BRIGHT_SVG);
}

void webUiHandleChip() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", MENU_CHIP_SVG);
}

void webUiHandleChipBright() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", MENU_CHIP_BRIGHT_SVG);
}

void webUiHandleLogoSpin() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", K9DTV_LOGO_SPIN_SVG);
}

void webUiHandleLogoSpinBright() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", K9DTV_LOGO_SPIN_BRIGHT_SVG);
}

void webUiHandleMarkLeft() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", K9_MARK_ICON_SVG);
}

void webUiHandleMarkLeftBright() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", K9_MARK_ICON_BRIGHT_SVG);
}

void webUiHandleMarkRight() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", K9_MARK_ICON_RIGHT_SVG);
}

void webUiHandleMarkRightBright() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send_P(200, "image/svg+xml", K9_MARK_ICON_RIGHT_BRIGHT_SVG);
}
