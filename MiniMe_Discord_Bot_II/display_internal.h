#ifndef DISPLAY_INTERNAL_H
#define DISPLAY_INTERNAL_H

// Private LCD modules: display.cpp / display_overlay.cpp / dash_snap.cpp / display_draw.cpp
#include "minime.h"
#include "display_layout.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct DashPalette {
  uint16_t bg, panel, line, text, muted, cyan, ok, bad, barTr, barFl;
};

struct DashUserRow {
  char name[16];
  char status[8];
  char bot[12];
};

struct DashSnap {
  bool valid;
  bool themeLight;
  bool layoutLog;
  bool layoutControls;
  uint32_t controlsGen;
  char timeStr[10];
  char dateStr[20];
  char upStr[24];
  int8_t gw; // -1 bad, 0 wait, 1 good
  int8_t bot; // 2 online else idle
  long rssi;
  uint32_t memFree, memTotal;
  uint32_t psFree, psTotal; // 0/0 if no PSRAM
  bool sdPresent;
  uint32_t sdFreeMb, sdTotalMb; // 0/0 if no card
  bool ipFlashOn; // no-SD: 2 s on / 2 s off bright-red IP (dirty redraw)
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

extern Arduino_DataBus* lcdBus;
extern Arduino_GFX* lcdPanel;

extern int16_t themeChipHitX, themeChipHitY, themeChipHitW, themeChipHitH;
extern int16_t layoutChipHitX, layoutChipHitY, layoutChipHitW, layoutChipHitH;
extern int16_t dogLeftHitX, dogLeftHitY, dogLeftHitW, dogLeftHitH;
extern int16_t dogRightHitX, dogRightHitY, dogRightHitW, dogRightHitH;
extern int16_t logoHitX, logoHitY, logoHitW, logoHitH;
extern int16_t ctrlBrightTrackX, ctrlBrightTrackY, ctrlBrightTrackW, ctrlBrightTrackH;
extern int16_t ctrlVolTrackX, ctrlVolTrackY, ctrlVolTrackW, ctrlVolTrackH;
extern int16_t ctrlToggle0X, ctrlToggle0Y, ctrlToggle0W, ctrlToggle0H;
extern int16_t ctrlToggle1X, ctrlToggle1Y, ctrlToggle1W, ctrlToggle1H;
extern int16_t ctrlToggle2X, ctrlToggle2Y, ctrlToggle2W, ctrlToggle2H;
extern std::atomic<bool> dashForceFull;
extern bool dashBrandValid;
extern DashSnap drawnSnap;

void displayCopyCapped(char* dst, size_t dstLen, const char* src);

DashPalette dashPalette();
void loadPublishedSnap(DashSnap& out);
bool snapLeftEqual(const DashSnap& a, const DashSnap& b);
bool snapRightEqual(const DashSnap& a, const DashSnap& b);

void drawBrandBar(const DashPalette& p);
void drawDogFooter(const DashPalette& p);
void drawLeftPanel(const DashSnap& s, const DashPalette& p);
void drawRightPanel(const DashSnap& s, const DashPalette& p);
void drawControlsLeft(const DashPalette& p);
void drawControlsRight(const DashPalette& p);

#endif
