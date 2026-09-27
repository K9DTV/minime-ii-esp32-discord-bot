#include "display_internal.h"

// Same 3-column idea as web mline(): label | fixed value col | bar (one row per meter).
static void drawMetricMline(int16_t x, int16_t y, int16_t right, const char* label,
                            const char* value, int fillFull, const DashPalette& p,
                            bool degreeSuffix = false, uint16_t labelCol = 0) {
  if (!gfx) return;
  if (labelCol == 0) labelCol = p.muted;
  prtCol(labelCol, label, x, y, 1);
  const int16_t valX = x + MLINE_VALUE_X;
  prtCol(p.text, value ? value : "", valX, y, 1);
  int16_t vw = textW(value ? value : "", 1);
  if (degreeSuffix && vw > 0) {
    // GFX default font has no degree glyph -- small circle like  deg
    gfx->drawCircle(valX + vw + 3, y + 1, 2, p.text);
  }
  // Fixed bar start (web .mline 3.2rem | 7ch | 1fr) -- do not shove bar for long values.
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

// Bright red (RGB565) for missing SD label / Wi-Fi-down IP.
static const uint16_t WARN_RED = 0xF800;

static void drawIpSysRow(int16_t x, int16_t y, const DashSnap& s, const DashPalette& p) {
  prtCol(p.muted, "IP", x, y, 1);
  // Solid red when Wi-Fi not connected (not SD-related).
  uint16_t vc = s.wifiOk ? p.text : WARN_RED;
  prtCol(vc, s.ip, x + SYS_VALUE_X, y, 1);
}

static void drawUpTempLine(int16_t cx, int16_t y, const DashSnap& s, const DashPalette& p) {
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

static void drawLogLines(int16_t sx, int16_t sy, int16_t bottom, const DashPalette& p,
                         const DashSnap& s, bool fullLog) {
  const int16_t maxRows = (int16_t)((bottom - sy) / USER_PITCH);
  const uint8_t total = fullLog ? s.logRowCount : s.serialRowCount;
  if (total == 0) {
    prtCol(p.muted, "(empty)", sx, sy, 1);
    return;
  }
  if (maxRows < 1) return;
  uint8_t vis = total;
  if ((int16_t)vis > maxRows) vis = (uint8_t)maxRows;
  uint8_t maxScroll = (total > vis) ? (uint8_t)(total - vis) : 0;
  uint8_t scroll = fullLog ? s.logScroll : s.serialScroll;
  if (scroll > maxScroll) scroll = maxScroll;
  // Newest at bottom; scroll skips N newest (drag up/down like web Log/Serial).
  for (uint8_t i = 0; i < vis; i++) {
    uint8_t fromNewest = (uint8_t)(scroll + vis - 1 - i);
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
  // MiniMe-II|GW|time, Bot|date, Up/T, Sig, PSRAM/SRAM, SD,
  // Users, HTTPS, Id (one row), DM/Mention, Event, IP, Src, OTA, Ver, CPU, Write+LCD.
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

  drawUpTempLine(cx, y, s, p);
  y += ROW_PITCH_UP_T;

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
    int fill = 0;
    uint16_t sdLab = p.muted;
    if (s.sdPresent && s.sdTotalMb > 0) {
      snprintf(sb, sizeof(sb), "%luM", (unsigned long)s.sdFreeMb);
      fill = dashHeapBarW(s.sdFreeMb, s.sdTotalMb);
    } else {
      snprintf(sb, sizeof(sb), "0M");
      fill = 0;
      sdLab = WARN_RED;
    }
    drawMetricMline(cx, y, right, "SD", sb, fill, p, false, sdLab);
  }
  y += ROW_PITCH_LOOSE;

  {
    char uBuf[28];
    snprintf(uBuf, sizeof(uBuf), "Users:%u/%u", (unsigned)s.nActive, (unsigned)MAX_TRACKED_USERS);
    prtCol(p.text, uBuf, cx, y, 1);
    const char* https = s.httpsBusy ? "HTTPS:busy" : "HTTPS:idle";
    prtCenter(s.httpsBusy ? p.bad : p.muted, https, mid, y, 1);
    char idBuf[16];
    snprintf(idBuf, sizeof(idBuf), "Id:%s", s.identified ? "yes" : "no");
    prtRight(s.identified ? p.ok : p.bad, idBuf, right, y, 1);
  }
  y += ROW_PITCH;
  {
    char dmBuf[16];
    snprintf(dmBuf, sizeof(dmBuf), "DM:%s", s.dm ? "ON" : "off");
    char menBuf[20];
    snprintf(menBuf, sizeof(menBuf), "Mention:%s", s.mention ? "ON" : "off");
    uint16_t ac = (s.dm || s.mention) ? p.bad : p.muted;
    prtCol(ac, dmBuf, cx, y, 1);
    prtRight(ac, menBuf, right, y, 1);
  }
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

  drawIpSysRow(cx, y, s, p); y += ROW_PITCH;
  drawSysRow(cx, y, "Src", s.secretsFromSd ? "SD card" : "firmware", p); y += ROW_PITCH;
  drawSysRow(cx, y, "OTA", otaHost, p); y += ROW_PITCH;
  drawSysRow(cx, y, "Ver", MINIME_VERSION, p); y += ROW_PITCH;
  drawSysRow(cx, y, "CPU", cpuBuf, p); y += ROW_PITCH;
  {
    // LCD left (awake/asleep); Refresh right (last write flush/draw ms).
    prtCol(p.muted, "LCD", cx, y, 1);
    prtCol(p.text, s.lcdAsleep ? "asleep" : "awake", cx + SYS_VALUE_X, y, 1);
    char refBuf[28];
    snprintf(refBuf, sizeof(refBuf), "Refresh %lu/%lu ms",
             (unsigned long)s.dashFlushMs, (unsigned long)s.dashDrawMs);
    prtRight(p.muted, refBuf, right, y, 1);
  }
  // Sticky Msg from !msg -- very last line of the left Display window.
  {
    const int16_t msgY = (int16_t)(bottom - 8);
    prtCol(p.muted, "Msg:", cx, msgY, 1);
    prtCol(p.text, s.msg[0] ? s.msg : "", cx + MSG_VALUE_X, msgY, 1);
  }
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

  // Display mode: users on the right (from published snap only).
  // Equal row gaps; last baseline sits on last paint line so slot 22 is the bottom row.
  prtCol(p.muted, "User", sx, sy, 1);
  prtCol(p.muted, "Status", sx + USER_STATUS_COL_X, sy, 1);
  prtCol(p.muted, "Bot", sx + USER_BOT_COL_X, sy, 1);
  sy += ROW_PITCH_LOOSE;

  const uint8_t uts = (uint8_t)USER_TEXT_SIZE;
  const int16_t textH = (int16_t)(USER_GLYPH_H * (int)uts);
  const int16_t firstSy = sy;
  const int16_t lastSy = (int16_t)(bottom - textH);
  for (uint8_t row = 0; row < MAX_TRACKED_USERS; row++) {
    int16_t y = firstSy;
    if (MAX_TRACKED_USERS > 1 && lastSy > firstSy) {
      y = (int16_t)(firstSy + (int32_t)(lastSy - firstSy) * (int32_t)row
                    / (int32_t)(MAX_TRACKED_USERS - 1));
    }
    prtCol(p.text, s.users[row].name, sx, y, uts);
    prtCol(p.cyan, s.users[row].status, sx + USER_STATUS_COL_X, y, uts);
    prtCol(p.muted, s.users[row].bot, sx + USER_BOT_COL_X, y, uts);
  }
}
