#include "minime.h"

// MmLog -> webLogFeed only. Do not write to USB Serial / UART0.

size_t MmLogClass::write(uint8_t c) {
  return write(&c, 1);
}

size_t MmLogClass::write(const uint8_t* buffer, size_t size) {
  if (!buffer || size == 0) return 0;
  webLogFeed(buffer, size);
  return size;
}

MmLogClass MmLog;

void mmSerialBegin() {
  // Do not Serial.begin / Serial0.begin -- no log traffic on the serial port.
  // Upload/OTA still work; open http://<board-ip>/ for LOG + Serial panels.
  MmLog.println("[GW] MiniMe log -> web only (USB Serial port killed)");
  MmLog.println("[GW] gateway drop log armed (5s remind / 60s full dump)");
}
