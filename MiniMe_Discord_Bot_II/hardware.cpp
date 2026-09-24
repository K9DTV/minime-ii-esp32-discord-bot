#include "minime.h"

OneWire oneWire(PIN_DS18B20);
DallasTemperature sensors(&oneWire);
Adafruit_NeoPixel pixels(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

void setupPins() {
  pinMode(RGB_LED_PIN, OUTPUT);
  digitalWrite(RGB_LED_PIN, LOW);
  pixels.begin();
  pixels.show();
  pinMode(PIN_DS18B20, INPUT_PULLUP);
  setupAudio();
}

// Core 0 uiTask only. Shared DallasTemperature/OneWire -- never call from Core 1
// (would race poll mid-conversion and corrupt the bus).
bool pollTemperatureNonBlocking(float& tempC, float& tempF) {
  static bool waiting = false;
  static unsigned long kickMs = 0;
  pinMode(PIN_DS18B20, INPUT_PULLUP);
  sensors.setWaitForConversion(false);
  if (!waiting) {
    sensors.requestTemperatures();
    kickMs = millis();
    waiting = true;
    return false;
  }
  // Datasheet max conversion ~750 ms at 12-bit (non-blocking: kick then harvest).
  if ((millis() - kickMs) < 750UL) {
    return false;
  }
  waiting = false;
  float c = sensors.getTempCByIndex(0);
  if (c == DEVICE_DISCONNECTED_C) {
    // Route via MmLog -> Core0 bridge (Serial panel shows [C0] ...). Rate-limit spam.
    static unsigned long lastDiscLogMs = 0;
    unsigned long now = millis();
    if (lastDiscLogMs == 0 || (now - lastDiscLogMs) >= 60000UL) {
      lastDiscLogMs = now;
      MmLog.println(F("DS18B20 disconnected"));
    }
    return false;
  }
  tempC = c;
  tempF = c * 9.0f / 5.0f + 32.0f;
  return true;
}

bool isLedByteToken(const String& s) {
  if (s.length() == 0 || s.length() > 3) return false;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c < '0' || c > '9') return false;
  }
  int v = s.toInt();
  return v >= 0 && v <= 255;
}

void setLedRgb(uint8_t r, uint8_t g, uint8_t b) {
  // Reserved: Guition module has no user RGB; !led is not shipped.
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

bool parseRgbTriplet(const String& args, uint8_t& r, uint8_t& g, uint8_t& b) {
  // Reserved for a future !led; unused by shipped commands.
  String a = args;
  a.trim();
  int sp1 = a.indexOf(' ');
  int sp2 = (sp1 >= 0) ? a.indexOf(' ', sp1 + 1) : -1;
  if (sp1 <= 0 || sp2 <= sp1) return false;
  String rs = a.substring(0, sp1);
  String gs = a.substring(sp1 + 1, sp2);
  String bs = a.substring(sp2 + 1);
  bs.trim();
  int sp3 = bs.indexOf(' ');
  if (sp3 >= 0) bs = bs.substring(0, sp3);
  if (!isLedByteToken(rs) || !isLedByteToken(gs) || !isLedByteToken(bs)) return false;
  r = (uint8_t)rs.toInt();
  g = (uint8_t)gs.toInt();
  b = (uint8_t)bs.toInt();
  return true;
}

bool isOwner(const String& authorId) {
  return authorId == OWNER_ID_STR;
}

void clearAlertFlags() {
  alertDm.store(false);
  alertMention.store(false);
}
