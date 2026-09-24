#include "display_internal.h"
#include "k9dtv_logo_rgb565.h"
#include "k9_mark_icon_rgb565.h"

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

static int16_t panelBottomY() {
  return (int16_t)PANEL_BOTTOM_Y_FULL;
}

// Site menu-chip.svg / menu-chip-bright.svg, scaled; label under chip like web.
static void drawMenuChip(int16_t ox, int16_t oy, const DashPalette& p) {
  if (!gfx) return;
  const int16_t s = MENU_CHIP_S;
  auto S = [s](float v) -> int16_t {
    return (int16_t)(v * (float)s / (float)MENU_CHIP_VIEWBOX + 0.5f);
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
  const int16_t labY = chipY + MENU_CHIP_S + CHIP_LABEL_GAP_Y;
  prtCenter(p.muted, label, chipX + MENU_CHIP_S / 2, labY, 1);
  hitX = chipX - CHIP_HIT_PAD_L;
  hitY = chipY - CHIP_HIT_PAD_T;
  hitW = MENU_CHIP_S + CHIP_HIT_EXTRA_W;
  hitH = (int16_t)(labY + CHIP_LABEL_TEXT_H - hitY);
  const int16_t bandH = logoBandHeight();
  if (hitY + hitH > bandH) hitH = bandH - hitY;
}

void drawBrandBar(const DashPalette& p) {
  if (!gfx) return;
  const int16_t bandH = logoBandHeight();
  gfx->fillRect(0, 0, LCD_LANDSCAPE_W, bandH, p.bg);

  const int16_t logoX = (LCD_LANDSCAPE_W - K9DTV_LOGO_W) / 2;
  const uint16_t* logoBits = lcdThemeLight ? K9DTV_LOGO_BRIGHT_RGB565 : K9DTV_LOGO_RGB565;
  gfx->draw16bitRGBBitmap(logoX, LOGO_TOP_PAD, (uint16_t*)logoBits,
                          K9DTV_LOGO_W, K9DTV_LOGO_H);
  // Logo is brand only (not a Controls hit).
  logoHitX = 0;
  logoHitY = 0;
  logoHitW = 0;
  logoHitH = 0;

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

  // Right gap: Display / Log / Controls (cycle).
  {
    const int16_t gapL = logoX + K9DTV_LOGO_W;
    const int16_t gapW = LCD_LANDSCAPE_W - gapL;
    const int16_t chipX = gapL + (gapW - MENU_CHIP_S) / 2;
    const char* lab = lcdLayoutControls ? "Controls" : (lcdLayoutLog ? "Log" : "Display");
    placeChip(chipX, chipY, lab, p,
              layoutChipHitX, layoutChipHitY, layoutChipHitW, layoutChipHitH);
  }
}

// Controls Cancel/Save mark: same PROGMEM RGB565 as logo path (fast blit, not per-pixel).
static void blitMarkIcon(int16_t dx, int16_t dy, int16_t dw, int16_t dh, bool faceRight) {
  if (!gfx) return;
  const uint16_t* bits;
  if (faceRight) {
    bits = lcdThemeLight ? K9_MARK_RIGHT_BRIGHT_RGB565 : K9_MARK_RIGHT_RGB565;
  } else {
    bits = lcdThemeLight ? K9_MARK_LEFT_BRIGHT_RGB565 : K9_MARK_LEFT_RGB565;
  }
  // Dog buttons are native mark size (48x32); use QSPI bitmap path like the brand logo.
  if (dw == K9_MARK_W && dh == K9_MARK_H) {
    gfx->draw16bitRGBBitmap(dx, dy, (uint16_t*)bits, K9_MARK_W, K9_MARK_H);
    return;
  }
  // Fallback only if layout sizes ever diverge.
  for (int16_t y = 0; y < dh; y++) {
    const int16_t srcY = (int16_t)(((int32_t)y * K9_MARK_H) / dh);
    if (srcY < 0 || srcY >= K9_MARK_H) continue;
    for (int16_t x = 0; x < dw; x++) {
      const int16_t srcX = (int16_t)(((int32_t)x * K9_MARK_W) / dw);
      if (srcX < 0 || srcX >= K9_MARK_W) continue;
      gfx->drawPixel(dx + x, dy + y, bits[(int)srcY * K9_MARK_W + srcX]);
    }
  }
}

// Cancel left edge of Controls panel / Save right edge of Toggles panel.
static void drawPanelDog(int16_t panelX, int16_t panelY, int16_t panelW, int16_t panelH,
                         bool faceRight, const char* lab, const DashPalette& p,
                         int16_t& hx, int16_t& hy, int16_t& hw, int16_t& hh) {
  if (!gfx) {
    hw = 0;
    hh = 0;
    return;
  }
  const int16_t dw = DOG_BTN_W;
  const int16_t dh = DOG_BTN_H_ICON;
  const int16_t labH = 10;
  const int16_t bh = (int16_t)(dh + labH);
  // Cancel = very left of its window; Save = very right of its window
  const int16_t dx = faceRight
      ? (int16_t)(panelX + panelW - PANEL_PAD - dw)
      : (int16_t)(panelX + PANEL_PAD);
  const int16_t dy = (int16_t)(panelY + panelH - PANEL_PAD_BOTTOM - bh);
  blitMarkIcon(dx, dy, dw, dh, faceRight);
  prtCenter(p.muted, lab, dx + dw / 2, (int16_t)(dy + dh + 1), 1);
  hx = dx;
  hy = dy;
  hw = dw;
  hh = bh;
}

void drawDogFooter(const DashPalette& p) {
  // Legacy name: dogs are drawn inside each Controls panel now (see drawControlsLeft/Right).
  (void)p;
  if (!lcdLayoutControls) {
    dogLeftHitW = 0;
    dogRightHitW = 0;
  }
}

static void drawPanelBox(int16_t x, int16_t y, int16_t w, int16_t h, const DashPalette& p) {
  if (!gfx) return;
  gfx->fillRoundRect(x, y, w, h, PANEL_CORNER_R, p.panel);
  gfx->drawRoundRect(x, y, w, h, PANEL_CORNER_R, p.line);
}

static void drawCtrlSlider(int16_t x, int16_t y, int16_t w, const char* label, uint8_t pct,
                           uint8_t minPct, const DashPalette& p,
                           int16_t& trackX, int16_t& trackY, int16_t& trackW, int16_t& trackH) {
  if (!gfx) return;
  char buf[28];
  snprintf(buf, sizeof(buf), "%s %u%%", label, (unsigned)pct);
  prtCol(p.muted, buf, x, y, 1);
  const int16_t ty = (int16_t)(y + 14);
  const int16_t th = 16;
  gfx->fillRect(x, ty, w, th, p.barTr);
  gfx->drawRect(x, ty, w, th, p.line);
  const int span = (minPct >= 100) ? 1 : (100 - (int)minPct);
  int rel = (int)pct - (int)minPct;
  if (rel < 0) rel = 0;
  if (rel > span) rel = span;
  int fillW = (rel * (w - 2)) / span;
  if (fillW < 0) fillW = 0;
  if (fillW > w - 2) fillW = w - 2;
  if (fillW > 0) gfx->fillRect(x + 1, ty + 1, fillW, th - 2, p.barFl);
  // Knob
  int16_t kx = (int16_t)(x + fillW - 2);
  if (kx < x) kx = x;
  if (kx > x + w - 6) kx = (int16_t)(x + w - 6);
  gfx->fillRect(kx, ty - 2, 6, th + 4, p.cyan);
  trackX = x;
  trackY = ty;
  trackW = w;
  trackH = th;
}

static void drawCtrlToggle(int16_t x, int16_t y, int16_t w, const char* label, bool on,
                           const DashPalette& p,
                           int16_t& hitX, int16_t& hitY, int16_t& hitW, int16_t& hitH) {
  if (!gfx) return;
  const int16_t h = 36;
  gfx->fillRoundRect(x, y, w, h, 4, p.panel);
  gfx->drawRoundRect(x, y, w, h, 4, p.line);
  prtCol(p.muted, label, x + 8, y + 14, 1);
  const char* st = on ? "ON" : "off";
  prtCol(on ? p.ok : p.bad, st, x + w - 36, y + 14, 1);
  hitX = x;
  hitY = y;
  hitW = w;
  hitH = h;
}

void drawControlsLeft(const DashPalette& p) {
  const int16_t top = logoBandHeight();
  const int16_t lx = PANEL_LEFT_X, ly = top, lw = PANEL_LEFT_W;
  const int16_t lh = (int16_t)(panelBottomY() - top);
  drawPanelBox(lx, ly, lw, lh, p);
  const int16_t sx = lx + PANEL_PAD;
  const int16_t sw = (int16_t)(lw - 2 * PANEL_PAD);
  prtCol(p.cyan, "Controls", sx, ly + PANEL_CONTENT_TOP, 1);
  drawCtrlSlider(sx, ly + 28, sw, "Brightness", uiBrightPct.load(), 0, p,
                 ctrlBrightTrackX, ctrlBrightTrackY, ctrlBrightTrackW, ctrlBrightTrackH);
  drawCtrlSlider(sx, ly + 78, sw, "Volume", uiVolPct.load(), 0, p,
                 ctrlVolTrackX, ctrlVolTrackY, ctrlVolTrackW, ctrlVolTrackH);
  drawPanelDog(lx, ly, lw, lh, false, "Cancel", p,
               dogLeftHitX, dogLeftHitY, dogLeftHitW, dogLeftHitH);
}

void drawControlsRight(const DashPalette& p) {
  const int16_t top = logoBandHeight();
  const int16_t rx = PANEL_RIGHT_X, ry = top, rw = PANEL_RIGHT_W;
  const int16_t rh = (int16_t)(panelBottomY() - top);
  drawPanelBox(rx, ry, rw, rh, p);
  const int16_t sx = rx + PANEL_PAD;
  const int16_t sw = (int16_t)(rw - 2 * PANEL_PAD);
  prtCol(p.cyan, "Toggles", sx, ry + PANEL_CONTENT_TOP, 1);
  drawCtrlToggle(sx, ry + 28, sw, "Sound", uiSoundOn.load(), p,
                 ctrlToggle0X, ctrlToggle0Y, ctrlToggle0W, ctrlToggle0H);
  drawCtrlToggle(sx, ry + 74, sw, "Ticks", uiTicksOn.load(), p,
                 ctrlToggle1X, ctrlToggle1Y, ctrlToggle1W, ctrlToggle1H);
  drawCtrlToggle(sx, ry + 120, sw, "Notify", uiNotifyOn.load(), p,
                 ctrlToggle2X, ctrlToggle2Y, ctrlToggle2W, ctrlToggle2H);
  drawPanelDog(rx, ry, rw, rh, true, "Save", p,
               dogRightHitX, dogRightHitY, dogRightHitW, dogRightHitH);
}

// Same 3-column idea as web mline(): label | fixed value col | bar (one row per meter).
static void drawMetricMline(int16_t x, int16_t y, int16_t right, const char* label,
                            const char* value, int fillFull, const DashPalette& p,
                            bool degreeSuffix = false) {
  if (!gfx) return;
  prtCol(p.muted, label, x, y, 1);
  const int16_t valX = x + MLINE_VALUE_X;
  prtCol(p.text, value ? value : "", valX, y, 1);
  int16_t vw = textW(value ? value : "", 1);
  if (degreeSuffix && vw > 0) {
    // GFX default font has no degree glyph — small circle like °
    gfx->drawCircle(valX + vw + 3, y + 1, 2, p.text);
  }
  // Fixed bar start (web .mline 3.2rem | 7ch | 1fr) — do not shove bar for long values.
  int16_t barX = x + MLINE_BAR_MIN_X;
  int16_t barW = (int16_t)(right - barX - 2);
  if (barW < MLINE_BAR_MIN_W) barW = MLINE_BAR_MIN_W;
  if (barW > LCD_BAR_MAX) barW = LCD_BAR_MAX;
  int fillW = (fillFull * barW) / DASH_SIG_HEAP_BAR_MAX;
  if (fillW < 0) fillW = 0;
  if (fillW > barW) fillW = barW;
  gfx->fillRect(barX, y - 1, barW + 2, MLINE_BAR_FRAME_H, p.barTr);
  gfx->drawRect(barX, y - 1, barW + 2, MLINE_BAR_FRAME_H, p.line);
  if (fillW > 0) gfx->fillRect(barX + 1, y, fillW, MLINE_BAR_FILL_H, p.barFl);
}

static void drawSysRow(int16_t x, int16_t y, const char* k, const char* v, const DashPalette& p) {
  prtCol(p.muted, k, x, y, 1);
  prtCol(p.text, v ? v : "", x + SYS_VALUE_X, y, 1);
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
  const int16_t lx = PANEL_LEFT_X, ly = top, lw = PANEL_LEFT_W;
  const int16_t lh = (int16_t)(panelBottomY() - top);
  drawPanelBox(lx, ly, lw, lh, p);

  const int16_t cx = lx + PANEL_PAD;
  const int16_t right = lx + lw - PANEL_PAD;
  const int16_t mid = lx + lw / 2;
  const int16_t bottom = ly + lh - PANEL_PAD_BOTTOM;
  int16_t y = ly + PANEL_CONTENT_TOP;

  if (s.layoutLog) {
    // Log layout: LOG left (matches web #box-logfile)
    prtCol(p.muted, "LOG", cx, y, 1);
    y += ROW_PITCH_LOOSE;
    drawLogLines(cx, y, bottom, p, s, true);
    return;
  }

  // Same order + formatting as web_assets.h #metrics:
  // MiniMe-II|GW|time, Bot|date, Sig dBm, PSRAM/SRAM freeK, Srv deg,
  // Up/T, Id/Users, DM/Mention, HTTPS, Event, IP, OTA, Ver, CPU, Write, Period, LCD.
  prtCol(p.text, "MiniMe-II", cx, y, 1);
  const char* gwLabel = (s.gw < 0) ? "GW:Bad" : (s.gw > 0 ? "GW:Good" : "GW:Wait");
  uint16_t gwCol = (s.gw > 0) ? p.ok : (s.gw == 0 ? p.cyan : p.bad);
  prtCenter(gwCol, gwLabel, mid, y, 1);
  prtRight(p.muted, s.timeStr, right, y, 1);
  y += ROW_PITCH;

  {
    char botBuf[16];
    snprintf(botBuf, sizeof(botBuf), "Bot %s", (s.bot == 2) ? "Online" : "Idle");
    prtCol(p.text, botBuf, cx, y, 1);
    prtRight(p.muted, s.dateStr, right, y, 1);
  }
  y += ROW_PITCH;

  {
    char rb[16];
    snprintf(rb, sizeof(rb), "%ld dBm", s.rssi);
    drawMetricMline(cx, y, right, "Sig", rb, dashSigBarW(s.rssi), p);
  }
  y += ROW_PITCH;

  if (s.psTotal > 0) {
    char pb[16];
    // Remaining free (same idea as web), not raw byte totals.
    snprintf(pb, sizeof(pb), "%luK", (unsigned long)(s.psFree / 1024UL));
    drawMetricMline(cx, y, right, "PSRAM", pb, dashHeapBarW(s.psFree, s.psTotal), p);
    y += ROW_PITCH;
  }
  {
    char hb[16];
    snprintf(hb, sizeof(hb), "%luK", (unsigned long)(s.memFree / 1024UL));
    drawMetricMline(cx, y, right, "SRAM", hb, dashHeapBarW(s.memFree, s.memTotal), p);
  }
  y += ROW_PITCH;
  {
    char sb[12];
    snprintf(sb, sizeof(sb), "%d", s.servoDeg);
    drawMetricMline(cx, y, right, "Srv", sb, dashSrvBarW(s.servoDeg), p, true);
  }
  y += ROW_PITCH_LOOSE;

  {
    if (s.tempC10 > -9980) {
      float tc = s.tempC10 / 10.0f;
      float tf = tc * 9.0f / 5.0f + 32.0f;
      int iF = (int)(tf >= 0.0f ? tf + 0.5f : tf - 0.5f);
      int iC = (int)(tc >= 0.0f ? tc + 0.5f : tc - 0.5f);
      char upPart[28];
      snprintf(upPart, sizeof(upPart), "Up %s  T ", s.upStr);
      prtCol(p.cyan, upPart, cx, y, 1);
      int16_t tx = cx + textW(upPart, 1);
      char nb[8];
      snprintf(nb, sizeof(nb), "%d", iF);
      prtCol(p.cyan, nb, tx, y, 1);
      tx += textW(nb, 1);
      gfx->drawCircle(tx + 3, y + 1, 2, p.cyan);
      tx += 7;
      prtCol(p.cyan, "F/", tx, y, 1);
      tx += textW("F/", 1);
      snprintf(nb, sizeof(nb), "%d", iC);
      prtCol(p.cyan, nb, tx, y, 1);
      tx += textW(nb, 1);
      gfx->drawCircle(tx + 3, y + 1, 2, p.cyan);
      tx += 7;
      prtCol(p.cyan, "C", tx, y, 1);
    } else {
      char line[40];
      snprintf(line, sizeof(line), "Up %s  T --Error--", s.upStr);
      prtCol(p.cyan, line, cx, y, 1);
    }
  }
  y += ROW_PITCH_UP_T;

  {
    char idBuf[28];
    snprintf(idBuf, sizeof(idBuf), "Id:%s", s.identified ? "yes" : "no");
    char uBuf[28];
    snprintf(uBuf, sizeof(uBuf), "Users:%u/%u", (unsigned)s.nActive, (unsigned)MAX_TRACKED_USERS);
    prtCol(s.identified ? p.ok : p.bad, idBuf, cx, y, 1);
    prtCol(p.text, uBuf, cx + USERS_COL_X, y, 1);
  }
  y += ROW_PITCH;
  {
    char al[40];
    snprintf(al, sizeof(al), "DM:%s  Mention:%s",
             s.dm ? "ON" : "off", s.mention ? "ON" : "off");
    prtCol((s.dm || s.mention) ? p.bad : p.muted, al, cx, y, 1);
  }
  y += ROW_PITCH;
  prtCol(s.httpsBusy ? p.bad : p.muted,
         s.httpsBusy ? "HTTPS:busy" : "HTTPS:idle", cx, y, 1);
  y += ROW_PITCH;
  {
    prtCol(p.muted, "Event:", cx, y, 1);
    prtCol(p.text, s.event, cx + EVENT_VALUE_X, y, 1);
  }
  y += ROW_PITCH_LOOSE;

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

  drawSysRow(cx, y, "IP", s.ip, p); y += ROW_PITCH;
  drawSysRow(cx, y, "OTA", otaHost, p); y += ROW_PITCH;
  drawSysRow(cx, y, "Ver", MINIME_VERSION, p); y += ROW_PITCH;
  drawSysRow(cx, y, "CPU", cpuBuf, p); y += ROW_PITCH;
  drawSysRow(cx, y, "Write", wrBuf, p); y += ROW_PITCH;
  drawSysRow(cx, y, "Period", periodBuf, p); y += ROW_PITCH;
  drawSysRow(cx, y, "LCD", lcdState, p);
}

void drawRightPanel(const DashSnap& s, const DashPalette& p) {
  const int16_t top = logoBandHeight();
  const int16_t lh = (int16_t)(panelBottomY() - top);
  const int16_t rx = PANEL_RIGHT_X, ry = top, rw = PANEL_RIGHT_W, rh = lh;
  drawPanelBox(rx, ry, rw, rh, p);
  const int16_t sx = rx + PANEL_PAD;
  const int16_t bottom = ry + rh - PANEL_PAD_BOTTOM;
  int16_t sy = ry + PANEL_PAD;

  if (s.layoutLog) {
    // Log layout: Serial right (matches web #box-serial)
    prtCol(p.muted, "Serial", sx, sy, 1);
    sy += ROW_PITCH_LOOSE;
    drawLogLines(sx, sy, bottom, p, s, false);
    return;
  }

  // Display mode: users on the right (from published snap only)
  prtCol(p.muted, "User", sx, sy, 1);
  prtCol(p.muted, "Status", sx + USER_STATUS_COL_X, sy, 1);
  prtCol(p.muted, "Bot", sx + USER_BOT_COL_X, sy, 1);
  sy += ROW_PITCH_LOOSE;

  const uint8_t uts = (uint8_t)USER_TEXT_SIZE;
  for (uint8_t row = 0; row < MAX_TRACKED_USERS; row++) {
    if (sy + 8 > bottom) break;
    prtCol(p.text, s.users[row].name, sx, sy, uts);
    prtCol(p.cyan, s.users[row].status, sx + USER_STATUS_COL_X, sy, uts);
    prtCol(p.muted, s.users[row].bot, sx + USER_BOT_COL_X, sy, uts);
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
  nowSnap.layoutControls = lcdLayoutControls;
  nowSnap.controlsGen = uiControlsGen.load();
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
                || drawnSnap.layoutLog != nowSnap.layoutLog
                || drawnSnap.layoutControls != nowSnap.layoutControls;
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
    if (!nowSnap.layoutControls) {
      dogLeftHitW = 0;
      dogRightHitW = 0;
    }
    dashBrandValid = true;
    needLeft = true;
    needRight = true;
  }

  if (needLeft) {
    if (nowSnap.layoutControls) drawControlsLeft(p);
    else drawLeftPanel(nowSnap, p);
  }
  yield();
  if (needRight) {
    if (nowSnap.layoutControls) drawControlsRight(p);
    else drawRightPanel(nowSnap, p);
  }
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
