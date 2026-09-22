#include "display_internal.h"

static float dashTempC = -999.0f;
static float dashTempF = -999.0f;
static unsigned long dashTempLastMs = 0; // with C/F under tempMux only

char transientLine1[UI_TRANSIENT_COLS] = "";
char transientLine2[UI_TRANSIENT_COLS] = "";
char transientLine3[UI_TRANSIENT_COLS] = "";
static unsigned long transientUntilMs = 0; // under uiOverlayMux only
char lastEventLine[UI_EVENT_COLS] = "";
std::atomic<bool> alertDm{false};
std::atomic<bool> alertMention{false};

// Cross-core UI overlay (Core 1 writers / Core 0 + web readers). Char buffers — no String alloc in critical.
static portMUX_TYPE uiOverlayMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE tempMux = portMUX_INITIALIZER_UNLOCKED;

void displayCopyCapped(char* dst, size_t dstLen, const char* src) {
  if (!dst || dstLen == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

void dashTempStore(float c, float f) {
  portENTER_CRITICAL(&tempMux);
  dashTempC = c;
  dashTempF = f;
  dashTempLastMs = millis();
  portEXIT_CRITICAL(&tempMux);
}

bool dashTempSnapshot(float& c, float& f, bool& hadSample, bool& fresh) {
  unsigned long last = 0;
  portENTER_CRITICAL(&tempMux);
  c = dashTempC;
  f = dashTempF;
  last = dashTempLastMs;
  portEXIT_CRITICAL(&tempMux);
  hadSample = (last != 0) && (c > -998.0f);
  fresh = hadSample && ((millis() - last) < 30000UL);
  return fresh;
}

void uiOverlayCopyEvent(char* buf, size_t bufLen) {
  if (!buf || bufLen == 0) return;
  portENTER_CRITICAL(&uiOverlayMux);
  displayCopyCapped(buf, bufLen, lastEventLine[0] ? lastEventLine : "-");
  portEXIT_CRITICAL(&uiOverlayMux);
}

void uiOverlayCopyTransient(char* l1, size_t l1Len, char* l2, size_t l2Len, char* l3, size_t l3Len,
                            unsigned long* untilMs) {
  portENTER_CRITICAL(&uiOverlayMux);
  if (l1 && l1Len) displayCopyCapped(l1, l1Len, transientLine1);
  if (l2 && l2Len) displayCopyCapped(l2, l2Len, transientLine2);
  if (l3 && l3Len) displayCopyCapped(l3, l3Len, transientLine3);
  if (untilMs) *untilMs = transientUntilMs;
  portEXIT_CRITICAL(&uiOverlayMux);
}

bool uiOverlayExpireIfDue(unsigned long now) {
  bool cleared = false;
  portENTER_CRITICAL(&uiOverlayMux);
  if (transientUntilMs != 0 && now >= transientUntilMs) {
    transientUntilMs = 0;
    cleared = true;
  }
  portEXIT_CRITICAL(&uiOverlayMux);
  return cleared;
}

void noteLastEvent(const String& line) {
  char tmp[UI_EVENT_COLS];
  displayCopyCapped(tmp, sizeof(tmp), line.c_str());
  portENTER_CRITICAL(&uiOverlayMux);
  displayCopyCapped(lastEventLine, sizeof(lastEventLine), tmp);
  portEXIT_CRITICAL(&uiOverlayMux);
  noteDisplayActivity();
  lastDashMillis = 0;
}

void showTransient(const String& line1, const String& line2, const String& line3, unsigned long durationMs) {
  char t1[UI_TRANSIENT_COLS], t2[UI_TRANSIENT_COLS], t3[UI_TRANSIENT_COLS];
  displayCopyCapped(t1, sizeof(t1), line1.c_str());
  displayCopyCapped(t2, sizeof(t2), line2.c_str());
  displayCopyCapped(t3, sizeof(t3), line3.c_str());
  unsigned long until = millis() + (durationMs ? durationMs : 3000UL);

  portENTER_CRITICAL(&uiOverlayMux);
  displayCopyCapped(transientLine1, sizeof(transientLine1), t1);
  displayCopyCapped(transientLine2, sizeof(transientLine2), t2);
  displayCopyCapped(transientLine3, sizeof(transientLine3), t3);
  transientUntilMs = until;
  // Do not write lastEventLine here — Event is sticky via noteLastEvent / Gateway only.
  // Web msg2 still shows the transient while untilMs; msg1/Event stay the last real event.
  portEXIT_CRITICAL(&uiOverlayMux);
  noteDisplayActivity();
  lastDashMillis = 0;
}
