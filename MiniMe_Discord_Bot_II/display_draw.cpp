#include "display_internal.h"
#include "k9dtv_logo_rgb565.h"

DashPalette dashPalette() {
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
  (void)p;
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

void drawBrandBar(const DashPalette& p) {
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

void drawLeftPanel(const DashSnap& s, const DashPalette& p) {
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
  drawSysRow(cx, y, "Ver", MINIME_VERSION, p); y += 10;
  drawSysRow(cx, y, "CPU", cpuBuf, p); y += 10;
  drawSysRow(cx, y, "Write", wrBuf, p); y += 10;
  drawSysRow(cx, y, "Period", periodBuf, p); y += 10;
  drawSysRow(cx, y, "LCD", lcdState, p);
}

void drawRightPanel(const DashSnap& s, const DashPalette& p) {
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

void drawDashboard() {
  if (!gfx) return;
  unsigned long t0 = millis();

  static DashSnap nowSnap; // static: ~4 KB — keep off uiTask stack
  loadPublishedSnap(nowSnap);
  // Live chip toggles / Core 0 temp may be ahead of the last Core 1 publish.
  nowSnap.themeLight = lcdThemeLight;
  nowSnap.layoutLog = lcdLayoutLog;
  {
    float tc = 0, tf = 0;
    bool had = false, fresh = false;
    dashTempSnapshot(tc, tf, had, fresh);
    nowSnap.tempC10 = fresh ? (int)(tc * 10.0f) : -9990;
  }
  if (!nowSnap.valid) {
    lastDashDrawMs = 0;
    lastDashFlushMs = 0;
    return;
  }
  DashPalette p = dashPalette();

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
