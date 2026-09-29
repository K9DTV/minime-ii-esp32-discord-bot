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
extern JsonDocument* statusDoc; // allocated in setupWebUi; used by webUiHandleStatus

// LAN web auth (WEB_UI_PASSWORD). Empty password => always ok.
bool webUiAuthEnabled();
bool webUiAuthOk();          // true if auth off or token matches
void webUiSendUnauthorized();
void webUiHandleLogin();     // POST /api/login

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
