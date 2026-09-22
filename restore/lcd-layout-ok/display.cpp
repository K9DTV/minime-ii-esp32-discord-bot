#include "minime.h"
#include "k9dtv_logo_rgb565.h"
#include <string.h>

Arduino_DataBus* lcdBus = nullptr;
Arduino_GFX* lcdPanel = nullptr;
Arduino_Canvas* gfx = nullptr;

float dashTempC = -999.0f;
float dashTempF = -999.0f;
int lastServoDeg = 45;

String transientLine1 = "";
String transientLine2 = "";
String transientLine3 = "";
unsigned long transientUntilMs = 0;
String lastEventLine = "";
bool alertDm = false;
bool alertMention = false;
bool lcdThemeLight = false;
bool lcdLayoutLog = false; // false = left metrics + right users; true = left LOG + right Serial

unsigned long lastDashMillis = 0;
unsigned long lastDisplayActivityMillis = 0;
unsigned long lastDashDrawMs = 0;
unsigned long lastDashFlushMs = 0;
bool displayAsleep = false;

enum { LCD_BAR_MAX = 150 };
enum { LOGO_TOP_PAD = 0, LOGO_BOTTOM_GAP = 4 };
enum { MENU_CHIP_S = 44 }; // scaled site IC chip (32 -> 44)
enum { USER_PITCH = 9 };

// Hit boxes for IC chips (chip + label); landscape coords.
static int16_t themeChipHitX = 0, themeChipHitY = 0, themeChipHitW = 0, themeChipHitH = 0;
static int16_t layoutChipHitX = 0, layoutChipHitY = 0, layoutChipHitW = 0, layoutChipHitH = 0;
static bool dashForceFull = true; // boot / wake / theme / layout
static bool dashBrandValid = false;

struct DashPalette {
  uint16_t bg, panel, line, text, muted, cyan, ok, bad, barTr, barFl;
};

static DashPalette pal() {
  if (lcdThemeLight) {
    // Match k9dtv / MiniMe web light tokens
    return {
      0xDEF5, // #dde2ea space
      0xF7BE, // #f3f5f8 panel
      0x8CAB, // #8b95a5 border
      0x08C5, // #0f172a text
      0x322A, // #334155 muted
      0x02EE, // #005f73 cyan
      0x1285, // #14532d ok
      0xC200, // #c2410c bad/warn
      0xFFFF, // bar track
      0x1C68  // bar fill green
    };
  }
  return {
    0x1082, 0x18C3, 0x2965, 0xDEFB, 0xBDF7, 0x5D7F, 0x274A, 0xFD40, 0x0841, 0x25A6
  };
}

static void prtCol(uint16_t col, const char* text, int16_t x, int16_t y, uint8_t size = 1) {
  if (!gfx || !text) return;
  gfx->setTextColor(col);
  gfx->setTextSize(size);
  gfx->setCursor(x, y);
  gfx->print(text);
}

static void prtCol(uint16_t col, const String& text, int16_t x, int16_t y, uint8_t size = 1) {
  prtCol(col, text.c_str(), x, y, size);
}

static int16_t textW(const char* text, uint8_t size = 1) {
  if (!gfx || !text) return 0;
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  gfx->setTextSize(size);
  gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)w;
}

static void prtRight(uint16_t col, const char* text, int16_t rightX, int16_t y, uint8_t size = 1) {
  prtCol(col, text, rightX - textW(text, size), y, size);
}

static void prtCenter(uint16_t col, const char* text, int16_t midX, int16_t y, uint8_t size = 1) {
  prtCol(col, text, midX - textW(text, size) / 2, y, size);
}

static int16_t logoBandHeight() {
  return (int16_t)(LOGO_TOP_PAD + K9DTV_LOGO_H + LOGO_BOTTOM_GAP);
}

// Site menu-chip.svg / menu-chip-bright.svg, scaled; label under chip like web.
static void drawMenuChip(int16_t ox, int16_t oy, const DashPalette& p) {
  if (!gfx) return;
  const int16_t s = MENU_CHIP_S;
  auto S = [s](float v) -> int16_t {
    return (int16_t)(v * (float)s / 32.0f + 0.5f);
  };

  uint16_t body, stroke, die, dieIn, pad, pin;
  if (lcdThemeLight) {
    body = 0xFFFF;
    stroke = 0x0413; // #0e8499
    die = 0xEF7D;    // #f0fafb
    dieIn = 0xE79C;  // #e0f2f5
    pad = stroke;
    pin = stroke;
  } else {
    body = 0x0861;   // #0d0f14
    stroke = 0x2988; // #2a3344
    die = 0x10C3;    // #161a22
    dieIn = 0x0861;
    pad = 0x8C55;    // #8899aa
    pin = pad;
  }

  gfx->fillRoundRect(ox + S(1), oy + S(1), S(30), S(30), S(3), body);
  gfx->drawRoundRect(ox + S(1), oy + S(1), S(30), S(30), S(3), stroke);

  // Die block (approx scale 1.1 around center)
  const int16_t cx = ox + S(16);
  const int16_t cy = oy + S(16);
  const int16_t dieX = cx - S(7);
  const int16_t dieY = cy - S(7);
  const int16_t dieW = S(14);
  gfx->fillRoundRect(dieX, dieY, dieW, dieW, S(1), die);
  gfx->drawRoundRect(dieX, dieY, dieW, dieW, S(1), pad);
  gfx->fillRect(cx - S(4), cy - S(4), S(9), S(9), dieIn);
  gfx->drawRect(cx - S(4), cy - S(4), S(9), S(9), stroke);

  // Pins
  gfx->drawFastVLine(cx - S(4), oy + S(8), S(3), pin);
  gfx->drawFastVLine(cx, oy + S(8), S(3), pin);
  gfx->drawFastVLine(cx + S(4), oy + S(8), S(3), pin);
  gfx->drawFastVLine(cx - S(4), oy + S(21), S(3), pin);
  gfx->drawFastVLine(cx, oy + S(21), S(3), pin);
  gfx->drawFastVLine(cx + S(4), oy + S(21), S(3), pin);
  gfx->drawFastHLine(ox + S(8), cy - S(4), S(3), pin);
  gfx->drawFastHLine(ox + S(8), cy, S(3), pin);
  gfx->drawFastHLine(ox + S(8), cy + S(4), S(3), pin);
  gfx->drawFastHLine(ox + S(21), cy - S(4), S(3), pin);
  gfx->drawFastHLine(ox + S(21), cy, S(3), pin);
  gfx->drawFastHLine(ox + S(21), cy + S(4), S(3), pin);

  if (lcdThemeLight) {
    gfx->fillCircle(cx, cy, S(1.35f), 0xC200); // #c2410c
  }
}

static void placeChip(int16_t chipX, int16_t chipY, const char* label, const DashPalette& p,
                      int16_t& hitX, int16_t& hitY, int16_t& hitW, int16_t& hitH) {
  drawMenuChip(chipX, chipY, p);
  const int16_t labY = chipY + MENU_CHIP_S + 1;
  prtCenter(p.muted, label, chipX + MENU_CHIP_S / 2, labY, 1);
  hitX = chipX - 4;
  hitY = chipY - 2;
  hitW = MENU_CHIP_S + 8;
  hitH = (int16_t)(labY + 10 - hitY);
  const int16_t bandH = logoBandHeight();
  if (hitY + hitH > bandH) hitH = bandH - hitY;
}

static void drawBrandBar(const DashPalette& p) {
  if (!gfx) return;
  const int16_t bandH = logoBandHeight();
  gfx->fillRect(0, 0, 480, bandH, p.bg);

  const int16_t logoX = (480 - K9DTV_LOGO_W) / 2;
  gfx->draw16bitRGBBitmap(logoX, LOGO_TOP_PAD, (uint16_t*)K9DTV_LOGO_RGB565,
                          K9DTV_LOGO_W, K9DTV_LOGO_H);

  const int16_t chipY = LOGO_TOP_PAD + (K9DTV_LOGO_H - MENU_CHIP_S) / 2;

  // Left gap: Light/Dark (web theme chip — label is destination mode).
  {
    const int16_t gapW = logoX;
    const int16_t chipX = (gapW - MENU_CHIP_S) / 2;
    char lab[16];
    if (lcdThemeLight) snprintf(lab, sizeof(lab), ") Dark");
    else snprintf(lab, sizeof(lab), "* Light");
    placeChip(chipX, chipY, lab, p, themeChipHitX, themeChipHitY, themeChipHitW, themeChipHitH);
  }

  // Right gap: Display/Log (web layout chip — label is current mode).
  {
    const int16_t gapL = logoX + K9DTV_LOGO_W;
    const int16_t gapW = 480 - gapL;
    const int16_t chipX = gapL + (gapW - MENU_CHIP_S) / 2;
    placeChip(chipX, chipY, lcdLayoutLog ? "Log" : "Display", p,
              layoutChipHitX, layoutChipHitY, layoutChipHitW, layoutChipHitH);
  }
}

static void drawPanelBox(int16_t x, int16_t y, int16_t w, int16_t h, const DashPalette& p) {
  if (!gfx) return;
  gfx->fillRoundRect(x, y, w, h, 4, p.panel);
  gfx->drawRoundRect(x, y, w, h, 4, p.line);
}

static void drawDashBarAt(int16_t x, int16_t y, const char* label, int fillFull,
                          const DashPalette& p, int16_t barX = -1) {
  if (!gfx) return;
  int fillW = (fillFull * LCD_BAR_MAX) / DASH_SIG_HEAP_BAR_MAX;
  if (fillW < 0) fillW = 0;
  if (fillW > LCD_BAR_MAX) fillW = LCD_BAR_MAX;
  prtCol(p.muted, label, x, y, 1);
  if (barX < 0) barX = x + 36;
  gfx->fillRect(barX, y - 1, LCD_BAR_MAX + 2, 10, p.barTr);
  gfx->drawRect(barX, y - 1, LCD_BAR_MAX + 2, 10, p.line);
  if (fillW > 0) gfx->fillRect(barX + 1, y, fillW, 8, p.barFl);
}

static void drawSysRow(int16_t x, int16_t y, const char* k, const String& v, const DashPalette& p) {
  prtCol(p.muted, k, x, y, 1);
  prtCol(p.text, v, x + 56, y, 1);
}

// Snapshot of what the LCD shows; skip redraw of unchanged layers.
struct DashSnap {
  bool valid;
  bool themeLight;
  bool layoutLog;
  char timeStr[10];
  char dateStr[20];
  char upStr[24];
  int8_t gw; // -1 bad, 0 wait, 1 good
  int8_t bot; // 2 online else idle
  long rssi;
  uint32_t memFree, memTotal;
  int servoDeg;
  int tempC10; // -9990 = error
  bool identified;
  uint8_t nActive;
  bool dm, mention, httpsBusy;
  char event[37];
  char ip[16];
  uint32_t cpuMhz;
  uint32_t userHash;
  uint32_t logGen;
};

static DashSnap drawnSnap = {};

static uint32_t hashUsers() {
  uint32_t h = 2166136261u;
  for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) {
    h ^= trackedUsers[i].active ? 1u : 0u;
    h *= 16777619u;
    h ^= (uint32_t)trackedUsers[i].status;
    h *= 16777619u;
    h ^= trackedUsers[i].useCount24h;
    h *= 16777619u;
    const String& id = trackedUsers[i].userId;
    for (size_t c = 0; c < id.length(); c++) {
      h ^= (uint8_t)id[c];
      h *= 16777619u;
    }
    const String& nm = trackedUsers[i].userName;
    for (size_t c = 0; c < nm.length(); c++) {
      h ^= (uint8_t)nm[c];
      h *= 16777619u;
    }
  }
  return h;
}

static void captureSnap(DashSnap& s) {
  memset(&s, 0, sizeof(s));
  s.valid = true;
  s.themeLight = lcdThemeLight;
  s.layoutLog = lcdLayoutLog;
  updateLocalTime();
  String t = timeClient.getFormattedTime();
  strncpy(s.timeStr, t.c_str(), sizeof(s.timeStr) - 1);
  formatLocalDateStr(s.dateStr, sizeof(s.dateStr));
  formatUptimeStr(s.upStr, sizeof(s.upStr));
  if (!gatewayConnected) s.gw = -1;
  else if (identified) s.gw = 1;
  else s.gw = 0;
  s.bot = (int8_t)botDiscordStatus;
  s.rssi = WiFi.RSSI();
  boardMemTotals(s.memFree, s.memTotal);
  s.servoDeg = lastServoDeg;
  s.tempC10 = (dashTempC > -998.0f) ? (int)(dashTempC * 10.0f) : -9990;
  s.identified = identified;
  s.nActive = 0;
  for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) {
    if (trackedUsers[i].active) s.nActive++;
  }
  s.dm = alertDm;
  s.mention = alertMention;
  s.httpsBusy = httpsInUse;
  strncpy(s.event, lastEventLine.length() ? lastEventLine.c_str() : "-", sizeof(s.event) - 1);
  String ip = WiFi.localIP().toString();
  strncpy(s.ip, ip.c_str(), sizeof(s.ip) - 1);
  s.cpuMhz = getCpuFrequencyMhz();
  s.userHash = hashUsers();
  s.logGen = lcdLogGen();
}

static bool snapLeftEqual(const DashSnap& a, const DashSnap& b) {
  if (a.themeLight != b.themeLight || a.layoutLog != b.layoutLog) return false;
  if (a.layoutLog) return a.logGen == b.logGen; // LOG window
  // Display mode left: status / metrics
  return strcmp(a.timeStr, b.timeStr) == 0
      && strcmp(a.dateStr, b.dateStr) == 0
      && strcmp(a.upStr, b.upStr) == 0
      && a.gw == b.gw && a.bot == b.bot
      && a.rssi == b.rssi
      && a.memFree == b.memFree && a.memTotal == b.memTotal
      && a.servoDeg == b.servoDeg && a.tempC10 == b.tempC10
      && a.identified == b.identified && a.nActive == b.nActive
      && a.dm == b.dm && a.mention == b.mention && a.httpsBusy == b.httpsBusy
      && strcmp(a.event, b.event) == 0
      && strcmp(a.ip, b.ip) == 0
      && a.cpuMhz == b.cpuMhz;
}

static bool snapRightEqual(const DashSnap& a, const DashSnap& b) {
  if (a.themeLight != b.themeLight || a.layoutLog != b.layoutLog) return false;
  if (a.layoutLog) return a.logGen == b.logGen; // Serial window
  return a.userHash == b.userHash; // Display mode right: users
}

static void drawLogLines(int16_t sx, int16_t sy, int16_t bottom, const DashPalette& p,
                         bool fullLog) {
  const int16_t maxRows = (int16_t)((bottom - sy) / USER_PITCH);
  uint8_t n = fullLog ? lcdFullLogCount() : lcdSerialCount();
  if (n == 0) {
    prtCol(p.muted, "(empty)", sx, sy, 1);
    return;
  }
  if (maxRows < 1) return;
  if ((int16_t)n > maxRows) n = (uint8_t)maxRows;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t fromNewest = (uint8_t)(n - 1 - i);
    char line[40];
    bool ok = fullLog ? lcdFullLogNewest(fromNewest, line, sizeof(line))
                      : lcdSerialNewest(fromNewest, line, sizeof(line));
    if (!ok) break;
    if (strlen(line) > 36) line[36] = '\0';
    prtCol(p.text, line, sx, sy, 1);
    sy += USER_PITCH;
    if (sy + 8 > bottom) break;
  }
}

static void drawLeftPanel(const DashSnap& s, const DashPalette& p) {
  const int16_t top = logoBandHeight();
  const int16_t lx = 4, ly = top, lw = 234, lh = (int16_t)(318 - top);
  drawPanelBox(lx, ly, lw, lh, p);

  const int16_t cx = lx + 6;
  const int16_t right = lx + lw - 6;
  const int16_t mid = lx + lw / 2;
  const int16_t bottom = ly + lh - 4;
  int16_t y = ly + 5;

  if (lcdLayoutLog) {
    // Log button: LOG takes over the left window
    prtCol(p.muted, "LOG", cx, y, 1);
    y += 12;
    drawLogLines(cx, y, bottom, p, true);
    return;
  }

  // Display mode: left metrics (as set up)
  prtCol(p.text, "MiniMe", cx, y, 1);
  const char* gwLabel = (s.gw < 0) ? "GW:Bad" : (s.gw > 0 ? "GW:Good" : "GW:Wait");
  uint16_t gwCol = (s.gw > 0) ? p.ok : (s.gw == 0 ? p.cyan : p.bad);
  prtCenter(gwCol, gwLabel, mid, y, 1);
  prtRight(p.muted, s.timeStr, right, y, 1);
  y += 10;

  {
    char botBuf[16];
    snprintf(botBuf, sizeof(botBuf), "Bot %s", (s.bot == 2) ? "Online" : "Idle");
    prtCol(p.text, botBuf, cx, y, 1);
    prtRight(p.muted, s.dateStr, right, y, 1);
  }
  y += 10;

  {
    char rb[12];
    snprintf(rb, sizeof(rb), "%ld", s.rssi);
    prtCol(p.muted, "Sig", cx, y, 1);
    const int16_t numX = cx + textW("Sig", 1) + 4;
    prtCol(p.text, rb, numX, y, 1);
    const int16_t barX = numX + textW(rb, 1) + 6;
    int fillW = (dashSigBarW(s.rssi) * LCD_BAR_MAX) / DASH_SIG_HEAP_BAR_MAX;
    if (fillW < 0) fillW = 0;
    if (fillW > LCD_BAR_MAX) fillW = LCD_BAR_MAX;
    const int16_t barMax = (int16_t)(right - barX - 2);
    int useMax = LCD_BAR_MAX;
    if (barMax < useMax) useMax = barMax;
    if (useMax < 20) useMax = 20;
    if (fillW > useMax) fillW = useMax;
    gfx->fillRect(barX, y - 1, useMax + 2, 10, p.barTr);
    gfx->drawRect(barX, y - 1, useMax + 2, 10, p.line);
    if (fillW > 0) gfx->fillRect(barX + 1, y, fillW, 8, p.barFl);
  }
  y += 10;

  {
    char line[40];
    if (s.tempC10 > -9980) {
      float tc = s.tempC10 / 10.0f;
      float tf = tc * 9.0f / 5.0f + 32.0f;
      snprintf(line, sizeof(line), "Up %s  T %.0fF/%.0fC", s.upStr, tf, tc);
    } else {
      snprintf(line, sizeof(line), "Up %s  T --Error--", s.upStr);
    }
    prtCol(p.cyan, line, cx, y, 1);
  }
  y += 11;

  drawDashBarAt(cx, y, "Heap", dashHeapBarW(s.memFree, s.memTotal), p);
  y += 10;
  {
    char hb[28];
    snprintf(hb, sizeof(hb), "%lu/%lu", (unsigned long)s.memFree, (unsigned long)s.memTotal);
    prtCol(p.muted, hb, cx + 36, y, 1);
  }
  y += 10;
  drawDashBarAt(cx, y, "Srv", dashSrvBarW(s.servoDeg), p);
  y += 12;

  {
    char idBuf[28];
    snprintf(idBuf, sizeof(idBuf), "Id:%s", s.identified ? "yes" : "no");
    char uBuf[28];
    snprintf(uBuf, sizeof(uBuf), "Users:%u/%u", (unsigned)s.nActive, (unsigned)MAX_TRACKED_USERS);
    prtCol(s.identified ? p.ok : p.bad, idBuf, cx, y, 1);
    prtCol(p.text, uBuf, cx + 100, y, 1);
  }
  y += 10;
  {
    char al[40];
    snprintf(al, sizeof(al), "DM:%s  Mention:%s",
             s.dm ? "ON" : "off", s.mention ? "ON" : "off");
    prtCol((s.dm || s.mention) ? p.bad : p.muted, al, cx, y, 1);
  }
  y += 10;
  prtCol(s.httpsBusy ? p.bad : p.muted,
         s.httpsBusy ? "HTTPS:busy" : "HTTPS:idle", cx, y, 1);
  y += 10;
  {
    prtCol(p.muted, "Event:", cx, y, 1);
    prtCol(p.text, s.event, cx + 42, y, 1);
  }
  y += 12;

  String ota = String(OTA_HOSTNAME) + ".local";
  char cpuBuf[16];
  snprintf(cpuBuf, sizeof(cpuBuf), "%u MHz", (unsigned)s.cpuMhz);
  char wrBuf[28];
  snprintf(wrBuf, sizeof(wrBuf), "%lu / %lu ms",
           (unsigned long)lastDashFlushMs, (unsigned long)lastDashDrawMs);
  char periodBuf[16];
  snprintf(periodBuf, sizeof(periodBuf), "%lu ms", (unsigned long)DASH_REFRESH_MS);

  drawSysRow(cx, y, "IP", String(s.ip), p); y += 10;
  drawSysRow(cx, y, "OTA", ota, p); y += 10;
  drawSysRow(cx, y, "CPU", String(cpuBuf), p); y += 10;
  drawSysRow(cx, y, "Write", String(wrBuf), p); y += 10;
  drawSysRow(cx, y, "Period", String(periodBuf), p);
}

static void drawRightPanel(const DashSnap& s, const DashPalette& p) {
  (void)s;
  const int16_t top = logoBandHeight();
  const int16_t lh = (int16_t)(318 - top);
  const int16_t rx = 242, ry = top, rw = 234, rh = lh;
  drawPanelBox(rx, ry, rw, rh, p);
  const int16_t sx = rx + 6;
  const int16_t bottom = ry + rh - 4;
  int16_t sy = ry + 6;

  if (lcdLayoutLog) {
    // Log button: Serial takes over the right window
    prtCol(p.muted, "Serial", sx, sy, 1);
    sy += 12;
    drawLogLines(sx, sy, bottom, p, false);
    return;
  }

  // Display mode: users on the right
  prtCol(p.muted, "User", sx, sy, 1);
  prtCol(p.muted, "Status", sx + 100, sy, 1);
  prtCol(p.muted, "Bot", sx + 168, sy, 1);
  sy += 12;

  for (uint8_t row = 0; row < MAX_TRACKED_USERS; row++) {
    if (sy + 8 > bottom) break;
    char name[16];
    const char* src;
    if (!trackedUsers[row].active) src = "---";
    else if (trackedUsers[row].userName.length()) src = trackedUsers[row].userName.c_str();
    else src = trackedUsers[row].userId.c_str();
    size_t n = 0;
    while (src[n] && n + 1 < sizeof(name) && n < 12) {
      name[n] = src[n];
      n++;
    }
    name[n] = '\0';
    prtCol(p.text, name, sx, sy, 1);
    prtCol(p.cyan, statusToWord(trackedUsers[row].active ? trackedUsers[row].status : 0),
           sx + 100, sy, 1);
    uint32_t uses = trackedUsers[row].active ? trackedUsers[row].useCount24h : 0;
    char botBuf[12];
    snprintf(botBuf, sizeof(botBuf), "%lu", (unsigned long)uses);
    prtCol(p.muted, botBuf, sx + 168, sy, 1);
    sy += USER_PITCH;
  }
}

bool setupDisplay() {
  lcdBus = new Arduino_ESP32QSPI(
      LCD_CS_PIN, LCD_SCK_PIN, LCD_D0_PIN, LCD_D1_PIN, LCD_D2_PIN, LCD_D3_PIN);
  lcdPanel = new Arduino_AXS15231B(
      lcdBus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, false /* IPS */,
      LCD_NATIVE_W, LCD_NATIVE_H);
  gfx = new Arduino_Canvas(LCD_NATIVE_W, LCD_NATIVE_H, lcdPanel, 0, 0, 0);
  if (!gfx || !gfx->begin()) return false;
  gfx->setRotation(1);
  DashPalette p = pal();
  gfx->fillScreen(p.bg);
  gfx->flush();
  lastDisplayActivityMillis = millis();
  displayAsleep = false;
  dashForceFull = true;
  dashBrandValid = false;
  drawnSnap.valid = false;
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, HIGH);
  return true;
}

void noteDisplayActivity() {
  lastDisplayActivityMillis = millis();
  if (displayAsleep) {
    displayAsleep = false;
    digitalWrite(LCD_BL_PIN, HIGH);
    lastDashMillis = 0;
    dashForceFull = true;
    drawDashboard();
  }
}

bool lcdThemeChipHit(uint16_t x, uint16_t y) {
  if (themeChipHitW <= 0 || themeChipHitH <= 0) return false;
  return (int16_t)x >= themeChipHitX && (int16_t)x < themeChipHitX + themeChipHitW
      && (int16_t)y >= themeChipHitY && (int16_t)y < themeChipHitY + themeChipHitH;
}

bool lcdLayoutChipHit(uint16_t x, uint16_t y) {
  if (layoutChipHitW <= 0 || layoutChipHitH <= 0) return false;
  return (int16_t)x >= layoutChipHitX && (int16_t)x < layoutChipHitX + layoutChipHitW
      && (int16_t)y >= layoutChipHitY && (int16_t)y < layoutChipHitY + layoutChipHitH;
}

void toggleLcdTheme() {
  lcdThemeLight = !lcdThemeLight;
  dashForceFull = true;
  dashBrandValid = false;
  lastDashMillis = 0;
  noteDisplayActivity();
  if (!displayAsleep) drawDashboard();
}

void toggleLcdLayout() {
  lcdLayoutLog = !lcdLayoutLog;
  dashForceFull = true; // brand label + right panel
  dashBrandValid = false;
  lastDashMillis = 0;
  noteDisplayActivity();
  if (!displayAsleep) drawDashboard();
}

int dashSigBarW(long rssi) {
  if (rssi >= -40) return DASH_SIG_HEAP_BAR_MAX;
  if (rssi <= -100) return 0;
  return (int)((rssi + 100) * DASH_SIG_HEAP_BAR_MAX / 60);
}

int dashHeapBarW(uint32_t memFree, uint32_t memTotal) {
  if (memTotal == 0) return 0;
  int w = (int)((memFree * (uint32_t)DASH_SIG_HEAP_BAR_MAX) / memTotal);
  if (w < 0) w = 0;
  if (w > DASH_SIG_HEAP_BAR_MAX) w = DASH_SIG_HEAP_BAR_MAX;
  return w;
}

int dashSrvBarW(int servoDeg) {
  int w = (servoDeg * DASH_SRV_BAR_MAX) / 90;
  if (w < 0) w = 0;
  if (w > DASH_SRV_BAR_MAX) w = DASH_SRV_BAR_MAX;
  return w;
}

int dashBarPct(int fill, int maxFill) {
  if (maxFill <= 0) return 0;
  int p = (fill * 100) / maxFill;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

void updateDisplaySleep() {
  if (displayAsleep) return;
  unsigned long now = millis();
  if (lastDisplayActivityMillis == 0) {
    lastDisplayActivityMillis = now;
    return;
  }
  if (now - lastDisplayActivityMillis < DISPLAY_IDLE_MS) return;
  displayAsleep = true;
  digitalWrite(LCD_BL_PIN, LOW);
}

void noteLastEvent(const String& line) {
  lastEventLine = line;
  if (lastEventLine.length() > 36) lastEventLine = lastEventLine.substring(0, 36);
  noteDisplayActivity();
  lastDashMillis = 0;
}

void showTransient(const String& line1, const String& line2, const String& line3, unsigned long durationMs) {
  (void)durationMs;
  transientLine1 = line1;
  transientLine2 = line2;
  transientLine3 = line3;
  transientUntilMs = millis() + 3000UL;
  String e = line1;
  if (line2.length()) {
    if (e.length()) e += " ";
    e += line2;
  }
  if (line3.length()) {
    if (e.length()) e += " ";
    e += line3;
  }
  noteLastEvent(e);
}

void drawDashboard() {
  if (!gfx) return;
  unsigned long t0 = millis();

  DashSnap nowSnap;
  captureSnap(nowSnap);
  DashPalette p = pal();

  bool needBrand = dashForceFull || !dashBrandValid
                || !drawnSnap.valid
                || drawnSnap.themeLight != nowSnap.themeLight
                || drawnSnap.layoutLog != nowSnap.layoutLog;
  bool needLeft = needBrand || !drawnSnap.valid || !snapLeftEqual(drawnSnap, nowSnap);
  bool needRight = needBrand || !drawnSnap.valid || !snapRightEqual(drawnSnap, nowSnap);

  if (!needBrand && !needLeft && !needRight) {
    // Nothing visible changed — skip canvas work and QSPI flush.
    lastDashDrawMs = 0;
    lastDashFlushMs = 0;
    return;
  }

  if (needBrand) {
    gfx->fillScreen(p.bg);
    drawBrandBar(p);
    dashBrandValid = true;
    needLeft = true;
    needRight = true;
  } else {
    // Clear only the strip under the logo if panels redraw over stale edges.
    // Panels redraw their own boxes.
  }

  if (needLeft) drawLeftPanel(nowSnap, p);
  yield();
  if (needRight) drawRightPanel(nowSnap, p);
  yield();

  pumpGateway();
  yield();
  unsigned long tFlush = millis();
  gfx->flush();
  lastDashFlushMs = millis() - tFlush;
  yield();
  pumpGateway();
  lastDashDrawMs = millis() - t0;

  drawnSnap = nowSnap;
  dashForceFull = false;
}

void updateDisplay() {
  updateDisplaySleep();
  if (displayAsleep) return;
  unsigned long now = millis();
  if (transientUntilMs != 0 && now >= transientUntilMs) {
    transientUntilMs = 0;
    lastDashMillis = 0;
  }
  if (lastDashMillis == 0 || now - lastDashMillis >= DASH_REFRESH_MS) {
    lastDashMillis = now;
    float c, f;
    if (readTemperature(c, f)) {
      dashTempC = c;
      dashTempF = f;
    }
    drawDashboard();
  }
}
