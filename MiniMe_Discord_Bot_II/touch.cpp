#include "minime.h"

// No USB VBUS ADC on this board (GPIO 1 is LCD backlight).

unsigned long lastTouchWakeMillis = 0;
bool touchWasActive = false;

static volatile bool touchIrqFlag = false;

static void IRAM_ATTR touchIsr() {
  touchIrqFlag = true;
}

// AXS15231B in-cell touch (same chip as LCD). Landscape mapping after gfx setRotation(1).
bool lcdTouchPoint(uint16_t& x, uint16_t& y) {
  const uint8_t maxPts = 1;
  uint8_t data[maxPts * 6 + 2] = {0};
  const uint8_t readCmd[11] = {
      0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00,
      (uint8_t)((maxPts * 6 + 2) >> 8),
      (uint8_t)((maxPts * 6 + 2) & 0xff),
      0x00, 0x00, 0x00};

  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(readCmd, sizeof(readCmd));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)TOUCH_I2C_ADDR, (int)sizeof(data)) != (int)sizeof(data)) {
    return false;
  }
  for (size_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)Wire.read();

  if (data[1] == 0 || data[1] > maxPts) return false;
  uint16_t rawX = (uint16_t)(((data[2] & 0x0F) << 8) | data[3]);
  uint16_t rawY = (uint16_t)(((data[4] & 0x0F) << 8) | data[5]);
  if (rawX > 500 || rawY > 500) return false;
  y = (uint16_t)map(rawX, 0, LCD_NATIVE_W, LCD_NATIVE_W, 0);
  x = rawY;
  return true;
}

void setupTouch() {
  touchWasActive = false;
  lastTouchWakeMillis = 0;
  touchIrqFlag = false;
  Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
  Wire.setClock(400000);
  // Bound stuck-bus stalls so pollTouchWake cannot hang the Core 0 UI task.
  Wire.setTimeOut(50); // ms (Arduino-ESP32 TwoWire)
  pinMode(TOUCH_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TOUCH_INT_PIN), touchIsr, FALLING);
}

void pollTouchWake() {
  unsigned long now = millis();
  uint16_t x = 0, y = 0;
  bool touched = touchIrqFlag;
  touchIrqFlag = false;

  if (displayAsleep || touched || digitalRead(TOUCH_INT_PIN) == LOW) {
    if (lcdTouchPoint(x, y)) touched = true;
  }

  if (!touched) {
    touchWasActive = false;
    return;
  }

  if (now - lastTouchWakeMillis < TOUCH_DEBOUNCE_MS) return;

  lastTouchWakeMillis = now;
  const bool rising = !touchWasActive;
  touchWasActive = true;

  // Wake-from-sleep: backlight only (no chip toggle on the same tap).
  if (displayAsleep) {
    noteDisplayActivity();
    return;
  }

  if (rising && lcdThemeChipHit(x, y)) {
    toggleLcdTheme();
  } else if (rising && lcdLayoutChipHit(x, y)) {
    toggleLcdLayout();
  } else if (rising) {
    noteDisplayActivity();
  }
}
