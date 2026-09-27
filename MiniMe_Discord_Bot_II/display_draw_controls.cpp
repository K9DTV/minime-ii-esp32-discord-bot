#include "display_internal.h"
#include "k9_mark_icon_rgb565.h"

// Controls Cancel/Save mark: native RGB565 from gen_k9_mark_icon_rgb565.py (72x48).
static void blitMarkIcon(int16_t dx, int16_t dy, int16_t dw, int16_t dh, bool faceRight) {
  if (!gfx) return;
  const uint16_t* bits;
  if (faceRight) {
    bits = lcdThemeLight ? K9_MARK_RIGHT_BRIGHT_RGB565 : K9_MARK_RIGHT_RGB565;
  } else {
    bits = lcdThemeLight ? K9_MARK_LEFT_BRIGHT_RGB565 : K9_MARK_LEFT_RGB565;
  }
  if (dw == K9_MARK_W && dh == K9_MARK_H) {
    gfx->draw16bitRGBBitmap(dx, dy, (uint16_t*)bits, K9_MARK_W, K9_MARK_H);
    return;
  }
  // Layout size should match K9_MARK_* -- fallback only if they diverge.
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
  // Touch hit box: written here so paint and hit share one source. See ui_controls.cpp.
  trackX = x;
  trackY = ty;
  trackW = w;
  trackH = th;
}

static void drawCtrlToggle(int16_t x, int16_t y, int16_t w, const char* label, bool on,
                           const DashPalette& p,
                           int16_t& hitX, int16_t& hitY, int16_t& hitW, int16_t& hitH) {
  if (!gfx) return;
  const int16_t h = CTRL_TOGGLE_H;
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
  drawCtrlSlider(sx, (int16_t)(ly + CTRL_ROW0), sw, "Brightness", uiBrightPct.load(), 0, p,
                 ctrlBrightTrackX, ctrlBrightTrackY, ctrlBrightTrackW, ctrlBrightTrackH);
  drawCtrlSlider(sx, (int16_t)(ly + CTRL_ROW0 + CTRL_ROW_PITCH), sw, "Volume", uiVolPct.load(), 0, p,
                 ctrlVolTrackX, ctrlVolTrackY, ctrlVolTrackW, ctrlVolTrackH);
  // Clear DM/@mention/Msg -- same row top as Notify (Toggles).
  {
    const int16_t x = sx, y = (int16_t)(ly + CTRL_ROW0 + 2 * CTRL_ROW_PITCH);
    const int16_t w = sw, h = CTRL_TOGGLE_H;
    gfx->fillRoundRect(x, y, w, h, 4, p.panel);
    gfx->drawRoundRect(x, y, w, h, 4, p.line);
    prtCenter(p.cyan, "Clear DM/Mention/Msg", x + w / 2, y + 14, 1);
    ctrlClearHitX = x;
    ctrlClearHitY = y;
    ctrlClearHitW = w;
    ctrlClearHitH = h;
  }
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
  drawCtrlToggle(sx, (int16_t)(ry + CTRL_ROW0), sw, "Sound", uiSoundOn.load(), p,
                 ctrlToggle0X, ctrlToggle0Y, ctrlToggle0W, ctrlToggle0H);
  drawCtrlToggle(sx, (int16_t)(ry + CTRL_ROW0 + CTRL_ROW_PITCH), sw, "Ticks", uiTicksOn.load(), p,
                 ctrlToggle1X, ctrlToggle1Y, ctrlToggle1W, ctrlToggle1H);
  drawCtrlToggle(sx, (int16_t)(ry + CTRL_ROW0 + 2 * CTRL_ROW_PITCH), sw, "Notify", uiNotifyOn.load(), p,
                 ctrlToggle2X, ctrlToggle2Y, ctrlToggle2W, ctrlToggle2H);
  drawPanelDog(rx, ry, rw, rh, true, "Save", p,
               dogRightHitX, dogRightHitY, dogRightHitW, dogRightHitH);
}
