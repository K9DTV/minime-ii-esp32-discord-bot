#include "display_internal.h"
#include "k9dtv_logo_rgb565.h"

int16_t logoBandHeight() {
  return (int16_t)(LOGO_TOP_PAD + K9DTV_LOGO_H + LOGO_BOTTOM_GAP);
}

// Site menu-chip.svg / menu-chip-bright.svg, scaled; label under chip like web.
static void drawMenuChip(int16_t ox, int16_t oy, const DashPalette& p) {
  if (!gfx) return;
  const int16_t s = MENU_CHIP_S;
  auto S = [s](float v) -> int16_t {
    return (int16_t)(v * (float)s / (float)MENU_CHIP_VIEWBOX + 0.5f);
  };

  uint16_t body, stroke, die, dieIn, pad, pin;
  if (lcdThemeLight.load()) {
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

  if (lcdThemeLight.load()) {
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
  const uint16_t* logoBits = lcdThemeLight.load() ? K9DTV_LOGO_BRIGHT_RGB565 : K9DTV_LOGO_RGB565;
  gfx->draw16bitRGBBitmap(logoX, LOGO_TOP_PAD, (uint16_t*)logoBits,
                          K9DTV_LOGO_W, K9DTV_LOGO_H);
  // Logo is brand only (not a Controls hit).
  logoHitX = 0;
  logoHitY = 0;
  logoHitW = 0;
  logoHitH = 0;

  const int16_t chipY = LOGO_TOP_PAD + (K9DTV_LOGO_H - MENU_CHIP_S) / 2;

  // Left gap: Light/Dark (LCD only -- web theme is independent).
  {
    const int16_t gapW = logoX;
    const int16_t chipX = (gapW - MENU_CHIP_S) / 2;
    char lab[16];
    if (lcdThemeLight.load()) snprintf(lab, sizeof(lab), ") Dark");
    else snprintf(lab, sizeof(lab), "* Light");
    placeChip(chipX, chipY, lab, p, themeChipHitX, themeChipHitY, themeChipHitW, themeChipHitH);
  }

  // Right gap: Menus (above) + Display / Log / Controls (under); chip cycles pages.
  {
    const int16_t gapL = logoX + K9DTV_LOGO_W;
    const int16_t gapW = LCD_LANDSCAPE_W - gapL;
    const int16_t chipX = gapL + (gapW - MENU_CHIP_S) / 2;
    const char* lab = lcdLayoutControls.load() ? "Controls" : (lcdLayoutLog.load() ? "Log" : "Display");
    const int16_t menusY = chipY - CHIP_LABEL_TEXT_H - 1;
    if (menusY >= 0) prtCenter(p.muted, "Menus", chipX + MENU_CHIP_S / 2, menusY, 1);
    placeChip(chipX, chipY, lab, p,
              layoutChipHitX, layoutChipHitY, layoutChipHitW, layoutChipHitH);
    if (menusY >= 0 && layoutChipHitY > menusY) {
      const int16_t grow = layoutChipHitY - menusY;
      layoutChipHitY = menusY;
      layoutChipHitH = (int16_t)(layoutChipHitH + grow);
    }
  }
}
