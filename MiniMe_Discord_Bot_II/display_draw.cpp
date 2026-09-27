#include "display_internal.h"

void drawDashboard() {
  if (!gfx) return;
  unsigned long t0 = millis();

  static DashSnap nowSnap; // static: ~4 KB -- keep off uiTask stack
  loadPublishedSnap(nowSnap);
  // Live chip toggles / Core 0 temp may be ahead of the last Core 1 publish.
  nowSnap.themeLight = lcdThemeLight;
  nowSnap.layoutLog = lcdLayoutLog;
  nowSnap.layoutControls = lcdLayoutControls;
  nowSnap.controlsGen = uiControlsGen.load();
  nowSnap.logScroll = lcdLogScroll;
  nowSnap.serialScroll = lcdSerialScroll;
  {
    float tc = 0, tf = 0;
    bool had = false, fresh = false;
    dashTempSnapshot(tc, tf, had, fresh);
    nowSnap.tempC10 = fresh ? (int)(tc * 10.0f) : -9990;
  }
  // Live SD / Wi-Fi / secrets source (Core 0 may be ahead of last Core 1 publish).
  nowSnap.sdPresent = sdCardPresent();
  if (nowSnap.sdPresent) {
    boardSdTotalsMb(nowSnap.sdFreeMb, nowSnap.sdTotalMb);
  } else {
    nowSnap.sdFreeMb = 0;
    nowSnap.sdTotalMb = 0;
  }
  nowSnap.wifiOk = (WiFi.status() == WL_CONNECTED);
  nowSnap.secretsFromSd = secretsFromSd;
  nowSnap.dashFlushMs = (uint32_t)lastDashFlushMs;
  nowSnap.dashDrawMs = (uint32_t)lastDashDrawMs;
  nowSnap.lcdAsleep = displayAsleep.load();
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
    // Nothing visible changed -- skip canvas work and QSPI flush.
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

  // Gateway stays on Core 1 -- never pumpGateway mid-draw.
  unsigned long tFlush = millis();
  gfx->flush();
  lastDashFlushMs = millis() - tFlush;
  yield();
  lastDashDrawMs = millis() - t0;

  drawnSnap = nowSnap;
  dashForceFull.store(false);
}
