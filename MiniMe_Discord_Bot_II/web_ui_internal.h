#ifndef WEB_UI_INTERNAL_H
#define WEB_UI_INTERNAL_H

// Shared by web_ui.cpp (routing + log rings) and web_render.cpp (HTML/JSON/assets).
#include "minime.h"
#include <WebServer.h>

enum {
  WEB_FULL_N = 55,    // LOG ring (matches DASH_LOG_ROWS)
  WEB_SERIAL_N = 55,  // Serial ring (same depth)
  WEB_LOG_COLS = 96
};

extern WebServer webServer;
extern char webFullLines[WEB_FULL_N][WEB_LOG_COLS + 1];
extern uint8_t webFullHead;
extern uint8_t webFullCount;
extern char webSerialLines[WEB_SERIAL_N][WEB_LOG_COLS + 1];
extern uint8_t webSerialHead;
extern uint8_t webSerialCount;
extern JsonDocument* statusDoc; // allocated in setupWebUi; used by webUiHandleStatus

void webUiSendNoCacheHeaders();
void webUiHandleRoot();
void webUiHandleStatus();
void webUiHandleUiCss();
void webUiHandleUiJs();
void webUiHandleLogo();
void webUiHandleLogoBright();
void webUiHandleLogoSpin();
void webUiHandleLogoSpinBright();
void webUiHandleChip();
void webUiHandleChipBright();
void webUiHandleMarkLeft();
void webUiHandleMarkLeftBright();
void webUiHandleMarkRight();
void webUiHandleMarkRightBright();

#endif
