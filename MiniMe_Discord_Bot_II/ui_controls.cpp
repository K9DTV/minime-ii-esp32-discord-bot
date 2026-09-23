#include "minime.h"

// Runtime UI controls (RAM). Flash persist comes later (todo).

extern std::atomic<bool> dashForceFull;

std::atomic<uint8_t> uiBrightPct{LCD_BL_PCT_DEFAULT}; // UI 0..100 -> duty 10..100%
std::atomic<uint8_t> uiVolPct{70};    // 0..100 -> I2S peak scale
std::atomic<bool> uiNotifyOn{true};   // DM/@mention alarm
std::atomic<bool> uiTicksOn{true};    // touch ticks
std::atomic<bool> uiSoundOn{true};    // master mute
std::atomic<uint32_t> uiControlsGen{0};

bool lcdLayoutControls = false;

// Hit boxes filled while drawing Controls (landscape).
int16_t ctrlBrightTrackX = 0, ctrlBrightTrackY = 0, ctrlBrightTrackW = 0, ctrlBrightTrackH = 0;
int16_t ctrlVolTrackX = 0, ctrlVolTrackY = 0, ctrlVolTrackW = 0, ctrlVolTrackH = 0;
int16_t ctrlToggle0X = 0, ctrlToggle0Y = 0, ctrlToggle0W = 0, ctrlToggle0H = 0;
int16_t ctrlToggle1X = 0, ctrlToggle1Y = 0, ctrlToggle1W = 0, ctrlToggle1H = 0;
int16_t ctrlToggle2X = 0, ctrlToggle2Y = 0, ctrlToggle2W = 0, ctrlToggle2H = 0;
int16_t logoHitX = 0, logoHitY = 0, logoHitW = 0, logoHitH = 0;

// Snapshot when entering Controls (Cancel restores).
static uint8_t snapBright = LCD_BL_PCT_DEFAULT;
static uint8_t snapVol = 70;
static bool snapNotify = true;
static bool snapTicks = true;
static bool snapSound = true;
static uint8_t controlsReturnMode = 0; // 0=Display, 1=Log
static bool controlsLeavingCommit = false;

static void bumpControls() {
  uiControlsGen.fetch_add(1);
  lastDashMillis = 0;
  dashForceFull.store(true);
}

static uint8_t brightUiToDuty(uint8_t ui) {
  if (ui > 100) ui = 100;
  return (uint8_t)(LCD_BL_PCT_MIN + ((uint16_t)ui * (100 - LCD_BL_PCT_MIN)) / 100);
}

void applyBacklightFromSettings() {
  if (displayAsleep.load()) {
    ledcWrite(LCD_BL_PIN, 0);
    return;
  }
  const uint8_t duty = brightUiToDuty(uiBrightPct.load());
  const uint32_t maxDuty = (1UL << LCD_BL_PWM_BITS) - 1UL;
  ledcWrite(LCD_BL_PIN, (duty * maxDuty) / 100UL);
}

void setUiBrightPct(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (uiBrightPct.load() == pct) {
    applyBacklightFromSettings();
    return;
  }
  uiBrightPct.store(pct);
  applyBacklightFromSettings();
  bumpControls();
}

void setUiVolPct(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (uiVolPct.load() == pct) return;
  uiVolPct.store(pct);
  bumpControls();
}

void setUiNotifyOn(bool on) {
  if (uiNotifyOn.load() == on) return;
  uiNotifyOn.store(on);
  bumpControls();
}

void setUiTicksOn(bool on) {
  if (uiTicksOn.load() == on) return;
  uiTicksOn.store(on);
  bumpControls();
}

void setUiSoundOn(bool on) {
  if (uiSoundOn.load() == on) return;
  uiSoundOn.store(on);
  bumpControls();
}

void controlsSnapshotEnter(uint8_t returnMode) {
  snapBright = uiBrightPct.load();
  snapVol = uiVolPct.load();
  snapNotify = uiNotifyOn.load();
  snapTicks = uiTicksOn.load();
  snapSound = uiSoundOn.load();
  controlsReturnMode = (returnMode == 1) ? 1 : 0;
}

void controlsRestoreSnapshot() {
  uiBrightPct.store(snapBright);
  uiVolPct.store(snapVol);
  uiNotifyOn.store(snapNotify);
  uiTicksOn.store(snapTicks);
  uiSoundOn.store(snapSound);
  applyBacklightFromSettings();
  bumpControls();
}

bool controlsLeavingIsCommit() {
  return controlsLeavingCommit;
}

void controlsCancel() {
  controlsRestoreSnapshot();
  controlsLeavingCommit = true;
  applyLcdLayoutMode(controlsReturnMode);
  controlsLeavingCommit = false;
}

void controlsSave() {
  // Keep live values (flash persist later).
  controlsLeavingCommit = true;
  applyLcdLayoutMode(controlsReturnMode);
  controlsLeavingCommit = false;
}

bool lcdLogoHit(uint16_t x, uint16_t y) {
  if (logoHitW <= 0 || logoHitH <= 0) return false;
  return (int16_t)x >= logoHitX && (int16_t)x < logoHitX + logoHitW
      && (int16_t)y >= logoHitY && (int16_t)y < logoHitY + logoHitH;
}

static bool hitBox(uint16_t x, uint16_t y, int16_t bx, int16_t by, int16_t bw, int16_t bh) {
  if (bw <= 0 || bh <= 0) return false;
  return (int16_t)x >= bx && (int16_t)x < bx + bw && (int16_t)y >= by && (int16_t)y < by + bh;
}

static uint8_t pctFromTrack(uint16_t x, int16_t trackX, int16_t trackW) {
  if (trackW < 1) return 0;
  int16_t rel = (int16_t)x - trackX;
  if (rel < 0) rel = 0;
  if (rel > trackW) rel = trackW;
  uint8_t pct = (uint8_t)((rel * 100) / trackW);
  if (pct > 100) pct = 100;
  return pct;
}

bool handleControlsTouch(uint16_t x, uint16_t y, bool rising) {
  if (!lcdLayoutControls) return false;

  if (hitBox(x, y, ctrlBrightTrackX, ctrlBrightTrackY - 8, ctrlBrightTrackW, ctrlBrightTrackH + 16)) {
    setUiBrightPct(pctFromTrack(x, ctrlBrightTrackX, ctrlBrightTrackW));
    return true;
  }
  if (hitBox(x, y, ctrlVolTrackX, ctrlVolTrackY - 8, ctrlVolTrackW, ctrlVolTrackH + 16)) {
    setUiVolPct(pctFromTrack(x, ctrlVolTrackX, ctrlVolTrackW));
    return true;
  }

  if (!rising) return false;

  if (hitBox(x, y, ctrlToggle0X, ctrlToggle0Y, ctrlToggle0W, ctrlToggle0H)) {
    setUiSoundOn(!uiSoundOn.load());
    return true;
  }
  if (hitBox(x, y, ctrlToggle1X, ctrlToggle1Y, ctrlToggle1W, ctrlToggle1H)) {
    setUiTicksOn(!uiTicksOn.load());
    return true;
  }
  if (hitBox(x, y, ctrlToggle2X, ctrlToggle2Y, ctrlToggle2W, ctrlToggle2H)) {
    setUiNotifyOn(!uiNotifyOn.load());
    return true;
  }
  return false;
}
