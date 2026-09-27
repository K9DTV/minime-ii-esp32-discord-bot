#include "display_internal.h"

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

void prtCol(uint16_t col, const char* text, int16_t x, int16_t y, uint8_t size) {
  if (!gfx || !text) return;
  gfx->setTextColor(col);
  gfx->setTextSize(size);
  gfx->setCursor(x, y);
  gfx->print(text);
}

int16_t textW(const char* text, uint8_t size) {
  if (!gfx || !text) return 0;
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  gfx->setTextSize(size);
  gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)w;
}

void prtRight(uint16_t col, const char* text, int16_t rightX, int16_t y, uint8_t size) {
  prtCol(col, text, rightX - textW(text, size), y, size);
}

void prtCenter(uint16_t col, const char* text, int16_t midX, int16_t y, uint8_t size) {
  prtCol(col, text, midX - textW(text, size) / 2, y, size);
}

int16_t panelBottomY() {
  return (int16_t)PANEL_BOTTOM_Y_FULL;
}

void drawPanelBox(int16_t x, int16_t y, int16_t w, int16_t h, const DashPalette& p) {
  if (!gfx) return;
  gfx->fillRoundRect(x, y, w, h, PANEL_CORNER_R, p.panel);
  gfx->drawRoundRect(x, y, w, h, PANEL_CORNER_R, p.line);
}
