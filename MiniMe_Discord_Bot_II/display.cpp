#include "display_internal.h"

Arduino_DataBus* lcdBus = nullptr;
Arduino_GFX* lcdPanel = nullptr;
Arduino_Canvas* gfx = nullptr;

int lastServoDeg = 45;
bool lcdThemeLight = false;
bool lcdLayoutLog = false; // false = left metrics + right users; true = left LOG + right Serial

unsigned long lastDashMillis = 0;
std::atomic<unsigned long> lastDisplayActivityMillis{0};
unsigned long lastDashDrawMs = 0;
unsigned long lastDashFlushMs = 0;
std::atomic<bool> displayAsleep{false};

// Hit boxes for IC chips (chip + label); landscape coords.
int16_t themeChipHitX = 0, themeChipHitY = 0, themeChipHitW = 0, themeChipHitH = 0;
int16_t layoutChipHitX = 0, layoutChipHitY = 0, layoutChipHitW = 0, layoutChipHitH = 0;
int16_t dogLeftHitX = 0, dogLeftHitY = 0, dogLeftHitW = 0, dogLeftHitH = 0;
int16_t dogRightHitX = 0, dogRightHitY = 0, dogRightHitW = 0, dogRightHitH = 0;
std::atomic<bool> dashForceFull{true}; // boot / wake / theme / layout; Core 1 may set, Core 0 clears
bool dashBrandValid = false;

static void lcdBacklightOn() {
  applyBacklightFromSettings();
}

static void lcdBacklightOff() {
  ledcWrite(LCD_BL_PIN, 0);
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
  DashPalette p = dashPalette();
  gfx->fillScreen(p.bg);
  gfx->flush();
  lastDisplayActivityMillis.store(millis());
  displayAsleep.store(false);
  dashForceFull.store(true);
  dashBrandValid = false;
  drawnSnap.valid = false;
  ledcAttach(LCD_BL_PIN, LCD_BL_PWM_HZ, LCD_BL_PWM_BITS);
  lcdBacklightOn();
  return true;
}

void noteDisplayActivity() {
  lastDisplayActivityMillis.store(millis());
  if (displayAsleep.load()) {
    displayAsleep.store(false);
    lcdBacklightOn();
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

void applyLcdLayoutMode(uint8_t mode) {
  // 0 = Display, 1 = Log, 2 = Controls
  if (mode > 2) mode = 0;
  const uint8_t prev = lcdLayoutControls ? 2u : (lcdLayoutLog ? 1u : 0u);
  const bool wantCtrl = (mode == 2);
  const bool wantLog = (mode == 1);
  if (lcdLayoutControls == wantCtrl && (wantCtrl || lcdLayoutLog == wantLog)) return;

  if (wantCtrl && prev != 2) {
    controlsSnapshotEnter(prev == 1 ? 1 : 0);
  } else if (prev == 2 && !wantCtrl && !controlsLeavingIsCommit()) {
    // Left Controls via layout chip: same as Cancel.
    controlsRestoreSnapshot();
  }

  lcdLayoutControls = wantCtrl;
  if (!wantCtrl) lcdLayoutLog = wantLog;
  dashForceFull.store(true);
  dashBrandValid = false;
  lastDashMillis = 0;
  noteDisplayActivity();
}

void setLcdLayoutLog(bool logMode) {
  applyLcdLayoutMode(logMode ? 1 : 0);
}

void setLcdControls(bool on) {
  if (on) applyLcdLayoutMode(2);
  else if (lcdLayoutControls) applyLcdLayoutMode(0);
}

void toggleLcdControls() {
  setLcdControls(!lcdLayoutControls);
}

void toggleLcdTheme() {
  setLcdThemeLight(!lcdThemeLight);
}

void toggleLcdLayout() {
  cycleLcdLayout(1);
}

void cycleLcdLayout(int dir) {
  uint8_t m = lcdLayoutControls ? 2u : (lcdLayoutLog ? 1u : 0u);
  int next = (int)m + dir;
  while (next < 0) next += 3;
  next %= 3;
  applyLcdLayoutMode((uint8_t)next);
}

bool lcdDogLeftHit(uint16_t x, uint16_t y) {
  if (dogLeftHitW <= 0 || dogLeftHitH <= 0) return false;
  return (int16_t)x >= dogLeftHitX && (int16_t)x < dogLeftHitX + dogLeftHitW
      && (int16_t)y >= dogLeftHitY && (int16_t)y < dogLeftHitY + dogLeftHitH;
}

bool lcdDogRightHit(uint16_t x, uint16_t y) {
  if (dogRightHitW <= 0 || dogRightHitH <= 0) return false;
  return (int16_t)x >= dogRightHitX && (int16_t)x < dogRightHitX + dogRightHitW
      && (int16_t)y >= dogRightHitY && (int16_t)y < dogRightHitY + dogRightHitH;
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
  lcdBacklightOff();
}

void updateDisplay() {
  updateDisplaySleep();
  // DS18B20 on Core 0 only (non-blocking poll).
  {
    float tc = 0, tf = 0;
    if (pollTemperatureNonBlocking(tc, tf)) {
      dashTempStore(tc, tf);
    }
  }
  if (displayAsleep) return;
  unsigned long now = millis();
  if (uiOverlayExpireIfDue(now)) {
    lastDashMillis = 0;
  }
  if (lastDashMillis == 0 || now - lastDashMillis >= DASH_REFRESH_MS) {
    lastDashMillis = now;
    // Users / logs / metrics from Core 1 publishDashSnap(); temp from Core 0 above.
    drawDashboard();
  }
}
