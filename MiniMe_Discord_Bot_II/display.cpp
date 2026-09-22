#include "minime.h"
#include "k9dtv_logo_rgb565.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
std::atomic<unsigned long> lastDisplayActivityMillis{0};
unsigned long lastDashDrawMs = 0;
unsigned long lastDashFlushMs = 0;
std::atomic<bool> displayAsleep{false};

enum { LCD_BAR_MAX = 150 };
enum { LOGO_TOP_PAD = 0, LOGO_BOTTOM_GAP = 4 };
enum { MENU_CHIP_S = 44 }; // scaled site IC chip (32 -> 44)
enum { USER_PITCH = 9 };

// Hit boxes for IC chips (chip + label); landscape coords.
static int16_t themeChipHitX = 0, themeChipHitY = 0, themeChipHitW = 0, themeChipHitH = 0;
static int16_t layoutChipHitX = 0, layoutChipHitY = 0, layoutChipHitW = 0, layoutChipHitH = 0;
static std::atomic<bool> dashForceFull{true}; // boot / wake / theme / layout; Core 1 may set, Core 0 clears
static bool dashBrandValid = false;

struct DashPalette {
  uint16_t bg, panel, line, text, muted, cyan, ok, bad, barTr, barFl;
};

static DashPalette pal() {
  // Match web_assets.h :root / html[data-theme=light] tokens (RGB888 -> RGB565).
  // Bar fill = cyan (same as .bar>i { background:var(--cyan) }).
  if (lcdThemeLight) {
    return {
      0xDF1D, // #dde2ea --k9-space
      0xF7BF, // #f3f5f8 --k9-panel
      0x8CB4, // #8b95a5 --k9-border
      0x08A5, // #0f172a --k9-text
      0x320A, // #334155 --k9-muted
      0x02EE, // #005f73 --k9-cyan
      0x1285, // #14532d --k9-green / --ok
      0x99A2, // #9a3412 --k9-orange / --bad
      0xFFFF, // #ffffff --bar-track
      0x02EE  // bar fill = cyan
    };
  }
  return {
    0x1082, // #121212 --k9-space
    0x18C3, // #1a1a1a --k9-panel
    0x2965, // #2c2c2c --k9-border
    0xE71C, // #e0e0e0 --k9-text
    0xBDF7, // #b8b8b8 --k9-muted
    0x5D9F, // #5eb3ff --k9-cyan
    0x2E6E, // #2ecc71 --k9-green / --ok
    0xFD84, // #ffb020 --k9-orange / --bad
    0x0841, // #0a0a0a --bar-track
    0x5D9F  // bar fill = cyan
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
    gfx->fillCircle(cx, cy, S(1.35f), 0x99A2); // #9a3412 --k9-orange light
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
  const uint16_t* logoBits = lcdThemeLight ? K9DTV_LOGO_BRIGHT_RGB565 : K9DTV_LOGO_RGB565;
  gfx->draw16bitRGBBitmap(logoX, LOGO_TOP_PAD, (uint16_t*)logoBits,
                          K9DTV_LOGO_W, K9DTV_LOGO_H);

  const int16_t chipY = LOGO_TOP_PAD + (K9DTV_LOGO_H - MENU_CHIP_S) / 2;

  // Left gap: Light/Dark (LCD only — web theme is independent).
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

// Same 3-column idea as web mline(): label | value | bar (one row per meter).
static void drawMetricMline(int16_t x, int16_t y, int16_t right, const char* label,
                            const char* value, int fillFull, const DashPalette& p) {
  if (!gfx) return;
  prtCol(p.muted, label, x, y, 1);
  const int16_t valX = x + 40;
  prtCol(p.text, value ? value : "", valX, y, 1);
  int16_t barX = valX + textW(value ? value : "", 1) + 4;
  if (barX < x + 100) barX = x + 100;
  int16_t barW = (int16_t)(right - barX - 2);
  if (barW < 24) barW = 24;
  if (barW > LCD_BAR_MAX) barW = LCD_BAR_MAX;
  int fillW = (fillFull * barW) / DASH_SIG_HEAP_BAR_MAX;
  if (fillW < 0) fillW = 0;
  if (fillW > barW) fillW = barW;
  gfx->fillRect(barX, y - 1, barW + 2, 10, p.barTr);
  gfx->drawRect(barX, y - 1, barW + 2, 10, p.line);
  if (fillW > 0) gfx->fillRect(barX + 1, y, fillW, 8, p.barFl);
}

static void drawSysRow(int16_t x, int16_t y, const char* k, const char* v, const DashPalette& p) {
  prtCol(p.muted, k, x, y, 1);
  prtCol(p.text, v ? v : "", x + 56, y, 1);
}

// Snapshot of what the LCD shows; Core 1 publishes, Core 0 paints.
enum { DASH_LOG_ROWS = 28 };
struct DashUserRow {
  char name[16];
  char status[8];
  char bot[12];
};
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
  uint32_t psFree, psTotal; // 0/0 if no PSRAM
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
  DashUserRow users[MAX_TRACKED_USERS];
  char logRows[DASH_LOG_ROWS][33];
  uint8_t logRowCount;
  char serialRows[DASH_LOG_ROWS][33];
  uint8_t serialRowCount;
};

static DashSnap drawnSnap = {};
// Seqlock: Core 1 writes publishedSnap between odd/even seq; Core 0 retries if torn.
// Avoids portENTER_CRITICAL + ~4KB memcpy (IRQ-off spin across cores).
static DashSnap publishedSnap = {};
static volatile uint32_t snapSeq = 0;

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
  formatLocalTimeStr(s.timeStr, sizeof(s.timeStr));
  formatLocalDateStr(s.dateStr, sizeof(s.dateStr));
  formatUptimeStr(s.upStr, sizeof(s.upStr));
  if (!gatewayConnected) s.gw = -1;
  else if (identified) s.gw = 1;
  else s.gw = 0;
  s.bot = (int8_t)botDiscordStatus;
  s.rssi = WiFi.RSSI();
  boardMemTotals(s.memFree, s.memTotal);
  boardPsramTotals(s.psFree, s.psTotal);
  s.servoDeg = lastServoDeg;
  // dashTempC/F written by Core 0 pollTemperatureNonBlocking only — never block here.
  s.tempC10 = (dashTempC > -998.0f) ? (int)(dashTempC * 10.0f) : -9990;
  s.identified = identified;
  s.nActive = 0;
  for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) {
    if (trackedUsers[i].active) s.nActive++;
    const char* src = "---";
    if (trackedUsers[i].active) {
      if (trackedUsers[i].userName.length()) src = trackedUsers[i].userName.c_str();
      else src = trackedUsers[i].userId.c_str();
    }
    size_t n = 0;
    while (src[n] && n + 1 < sizeof(s.users[i].name) && n < 12) {
      s.users[i].name[n] = src[n];
      n++;
    }
    s.users[i].name[n] = '\0';
    strncpy(s.users[i].status,
            statusToWord(trackedUsers[i].active ? trackedUsers[i].status : 0),
            sizeof(s.users[i].status) - 1);
    uint32_t uses = trackedUsers[i].active ? trackedUsers[i].useCount24h : 0;
    snprintf(s.users[i].bot, sizeof(s.users[i].bot), "%lu", (unsigned long)uses);
  }
  s.dm = alertDm;
  s.mention = alertMention;
  s.httpsBusy = httpsInUse;
  strncpy(s.event, lastEventLine.length() ? lastEventLine.c_str() : "-", sizeof(s.event) - 1);
  {
    IPAddress ip = WiFi.localIP();
    snprintf(s.ip, sizeof(s.ip), "%u.%u.%u.%u",
             (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
  }
  s.cpuMhz = getCpuFrequencyMhz();
  s.userHash = hashUsers();
  s.logGen = lcdLogGen();
  s.logRowCount = 0;
  for (uint8_t i = 0; i < DASH_LOG_ROWS; i++) {
    if (!lcdFullLogNewest(i, s.logRows[i], sizeof(s.logRows[i]))) break;
    s.logRowCount++;
  }
  s.serialRowCount = 0;
  for (uint8_t i = 0; i < DASH_LOG_ROWS; i++) {
    if (!lcdSerialNewest(i, s.serialRows[i], sizeof(s.serialRows[i]))) break;
    s.serialRowCount++;
  }
}

void publishDashSnap() {
  // Throttle: full snap copy every DASH_REFRESH_MS (loop can run much faster).
  static unsigned long lastPubMs = 0;
  unsigned long now = millis();
  if (lastPubMs != 0 && (now - lastPubMs) < DASH_REFRESH_MS) return;
  lastPubMs = now;

  static DashSnap tmp; // static: ~4 KB — keep off Core 1 loop stack
  captureSnap(tmp);
  uint32_t s = snapSeq;
  snapSeq = s + 1; // odd = writer in progress
  publishedSnap = tmp;
  snapSeq = s + 2; // even = stable
}

static void loadPublishedSnap(DashSnap& out) {
  for (;;) {
    uint32_t s1 = snapSeq;
    if (s1 & 1u) {
      taskYIELD();
      continue; // writer mid-update
    }
    out = publishedSnap;
    uint32_t s2 = snapSeq;
    if (s1 == s2 && !(s2 & 1u)) break;
    taskYIELD();
  }
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
      && a.psFree == b.psFree && a.psTotal == b.psTotal
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
                         const DashSnap& s, bool fullLog) {
  const int16_t maxRows = (int16_t)((bottom - sy) / USER_PITCH);
  uint8_t n = fullLog ? s.logRowCount : s.serialRowCount;
  if (n == 0) {
    prtCol(p.muted, "(empty)", sx, sy, 1);
    return;
  }
  if (maxRows < 1) return;
  if ((int16_t)n > maxRows) n = (uint8_t)maxRows;
  // Newest at bottom: snap rows are newest-first from lcd*Newest.
  for (uint8_t i = 0; i < n; i++) {
    uint8_t fromNewest = (uint8_t)(n - 1 - i);
    const char* line = fullLog ? s.logRows[fromNewest] : s.serialRows[fromNewest];
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

  if (s.layoutLog) {
    // Log button: LOG takes over the left window
    prtCol(p.muted, "LOG", cx, y, 1);
    y += 12;
    drawLogLines(cx, y, bottom, p, s, true);
    return;
  }

  // Exact same order as web_assets.h #metrics (left window):
  // MiniMe-II|GW|time, Bot|date, Sig, PSRAM, SRAM, Srv, Up/T, Id/Users,
  // DM/Mention, HTTPS, Event, IP, OTA, CPU, Write, Period, LCD.
  prtCol(p.text, "MiniMe-II", cx, y, 1);
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
    drawMetricMline(cx, y, right, "Sig", rb, dashSigBarW(s.rssi), p);
  }
  y += 10;

  if (s.psTotal > 0) {
    char pb[28];
    snprintf(pb, sizeof(pb), "%lu/%lu", (unsigned long)s.psFree, (unsigned long)s.psTotal);
    drawMetricMline(cx, y, right, "PSRAM", pb, dashHeapBarW(s.psFree, s.psTotal), p);
    y += 10;
  }
  {
    char hb[28];
    snprintf(hb, sizeof(hb), "%lu/%lu", (unsigned long)s.memFree, (unsigned long)s.memTotal);
    drawMetricMline(cx, y, right, "SRAM", hb, dashHeapBarW(s.memFree, s.memTotal), p);
  }
  y += 10;
  {
    char sb[12];
    snprintf(sb, sizeof(sb), "%d", s.servoDeg);
    drawMetricMline(cx, y, right, "Srv", sb, dashSrvBarW(s.servoDeg), p);
  }
  y += 12;

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

  static char otaHost[40];
  static bool otaHostReady = false;
  if (!otaHostReady) {
    snprintf(otaHost, sizeof(otaHost), "%s.local", OTA_HOSTNAME);
    otaHostReady = true;
  }
  char cpuBuf[16];
  snprintf(cpuBuf, sizeof(cpuBuf), "%u MHz", (unsigned)s.cpuMhz);
  char wrBuf[28];
  snprintf(wrBuf, sizeof(wrBuf), "%lu / %lu ms",
           (unsigned long)lastDashFlushMs, (unsigned long)lastDashDrawMs);
  char periodBuf[16];
  snprintf(periodBuf, sizeof(periodBuf), "%lu ms", (unsigned long)DASH_REFRESH_MS);
  const char* lcdState = displayAsleep.load() ? "asleep" : "awake";

  drawSysRow(cx, y, "IP", s.ip, p); y += 10;
  drawSysRow(cx, y, "OTA", otaHost, p); y += 10;
  drawSysRow(cx, y, "CPU", cpuBuf, p); y += 10;
  drawSysRow(cx, y, "Write", wrBuf, p); y += 10;
  drawSysRow(cx, y, "Period", periodBuf, p); y += 10;
  drawSysRow(cx, y, "LCD", lcdState, p);
}

static void drawRightPanel(const DashSnap& s, const DashPalette& p) {
  const int16_t top = logoBandHeight();
  const int16_t lh = (int16_t)(318 - top);
  const int16_t rx = 242, ry = top, rw = 234, rh = lh;
  drawPanelBox(rx, ry, rw, rh, p);
  const int16_t sx = rx + 6;
  const int16_t bottom = ry + rh - 4;
  int16_t sy = ry + 6;

  if (s.layoutLog) {
    // Log button: Serial takes over the right window
    prtCol(p.muted, "Serial", sx, sy, 1);
    sy += 12;
    drawLogLines(sx, sy, bottom, p, s, false);
    return;
  }

  // Display mode: users on the right (from published snap only)
  prtCol(p.muted, "User", sx, sy, 1);
  prtCol(p.muted, "Status", sx + 100, sy, 1);
  prtCol(p.muted, "Bot", sx + 168, sy, 1);
  sy += 12;

  for (uint8_t row = 0; row < MAX_TRACKED_USERS; row++) {
    if (sy + 8 > bottom) break;
    prtCol(p.text, s.users[row].name, sx, sy, 1);
    prtCol(p.cyan, s.users[row].status, sx + 100, sy, 1);
    prtCol(p.muted, s.users[row].bot, sx + 168, sy, 1);
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
  lastDisplayActivityMillis.store(millis());
  displayAsleep.store(false);
  dashForceFull.store(true);
  dashBrandValid = false;
  drawnSnap.valid = false;
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, HIGH);
  return true;
}

void noteDisplayActivity() {
  lastDisplayActivityMillis.store(millis());
  if (displayAsleep.load()) {
    displayAsleep.store(false);
    digitalWrite(LCD_BL_PIN, HIGH);
    lastDashMillis = 0;
    dashForceFull.store(true);
    // Paint only from Core 0 uiTask (do not drawDashboard here — Core 1 may call this).
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

void setLcdThemeLight(bool light) {
  if (lcdThemeLight == light) return;
  lcdThemeLight = light;
  dashForceFull.store(true);
  dashBrandValid = false;
  lastDashMillis = 0;
  noteDisplayActivity();
  // Core 0 uiTask redraws; Core 1 must not call drawDashboard.
}

void setLcdLayoutLog(bool logMode) {
  if (lcdLayoutLog == logMode) return;
  lcdLayoutLog = logMode;
  dashForceFull.store(true);
  dashBrandValid = false;
  lastDashMillis = 0;
  noteDisplayActivity();
}

void toggleLcdTheme() {
  setLcdThemeLight(!lcdThemeLight);
}

void toggleLcdLayout() {
  setLcdLayoutLog(!lcdLayoutLog);
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
  if (displayAsleep.load()) return;
  unsigned long now = millis();
  unsigned long lastAct = lastDisplayActivityMillis.load();
  if (lastAct == 0) {
    lastDisplayActivityMillis.store(now);
    return;
  }
  if (now - lastAct < DISPLAY_IDLE_MS) return;
  displayAsleep.store(true);
  digitalWrite(LCD_BL_PIN, LOW);
}

void noteLastEvent(const String& line) {
  lastEventLine = line;
  if (lastEventLine.length() > 36) lastEventLine = lastEventLine.substring(0, 36);
  noteDisplayActivity();
  lastDashMillis = 0;
}

void showTransient(const String& line1, const String& line2, const String& line3, unsigned long durationMs) {
  transientLine1 = line1;
  transientLine2 = line2;
  transientLine3 = line3;
  transientUntilMs = millis() + (durationMs ? durationMs : 3000UL);
  // Sticky Event (LCD + web msg1): all three lines. Web msg2 flash uses 1+2+3 while untilMs.
  char e[120];
  size_t n = 0;
  e[0] = '\0';
  const String* parts[3] = {&line1, &line2, &line3};
  for (uint8_t p = 0; p < 3; p++) {
    if (!parts[p]->length()) continue;
    if (n && n + 1 < sizeof(e)) e[n++] = ' ';
    for (size_t i = 0; i < parts[p]->length() && n + 1 < sizeof(e); i++) {
      e[n++] = (*parts[p])[i];
    }
    e[n] = '\0';
  }
  noteLastEvent(e);
}

void drawDashboard() {
  if (!gfx) return;
  unsigned long t0 = millis();

  static DashSnap nowSnap; // static: ~4 KB — keep off uiTask stack
  loadPublishedSnap(nowSnap);
  // Live chip toggles / Core 0 temp may be ahead of the last Core 1 publish.
  nowSnap.themeLight = lcdThemeLight;
  nowSnap.layoutLog = lcdLayoutLog;
  nowSnap.tempC10 = (dashTempC > -998.0f) ? (int)(dashTempC * 10.0f) : -9990;
  if (!nowSnap.valid) {
    lastDashDrawMs = 0;
    lastDashFlushMs = 0;
    return;
  }
  DashPalette p = pal();

  bool needBrand = dashForceFull.load() || !dashBrandValid
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
  }

  if (needLeft) drawLeftPanel(nowSnap, p);
  yield();
  if (needRight) drawRightPanel(nowSnap, p);
  yield();

  // Gateway stays on Core 1 — never pumpGateway mid-draw.
  unsigned long tFlush = millis();
  gfx->flush();
  lastDashFlushMs = millis() - tFlush;
  yield();
  lastDashDrawMs = millis() - t0;

  drawnSnap = nowSnap;
  dashForceFull.store(false);
}

void updateDisplay() {
  updateDisplaySleep();
  // DS18B20 on Core 0 only (non-blocking poll).
  {
    float tc = 0, tf = 0;
    if (pollTemperatureNonBlocking(tc, tf)) {
      dashTempC = tc;
      dashTempF = tf;
    }
  }
  if (displayAsleep) return;
  unsigned long now = millis();
  if (transientUntilMs != 0 && now >= transientUntilMs) {
    transientUntilMs = 0;
    lastDashMillis = 0;
  }
  if (lastDashMillis == 0 || now - lastDashMillis >= DASH_REFRESH_MS) {
    lastDashMillis = now;
    // Users / logs / metrics from Core 1 publishDashSnap(); temp from Core 0 above.
    drawDashboard();
  }
}
