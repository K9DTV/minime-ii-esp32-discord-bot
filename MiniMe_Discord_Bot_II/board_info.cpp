#include "minime.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void boardMemTotals(uint32_t& memFree, uint32_t& memTotal) {
  // Internal SRAM only -- LCD/web heap bar must not hide internal exhaustion behind free PSRAM.
  memTotal = ESP.getHeapSize();
  memFree = ESP.getFreeHeap();
}

void boardPsramTotals(uint32_t& psFree, uint32_t& psTotal) {
  psTotal = ESP.getPsramSize();
  psFree = ESP.getFreePsram();
}

void uptimeDhms(unsigned long& days, unsigned long& hours, unsigned long& minutes, unsigned long& seconds) {
  unsigned long sec = millis() / 1000;
  days = sec / 86400;
  hours = (sec % 86400) / 3600;
  minutes = (sec % 3600) / 60;
  seconds = sec % 60;
  if (days > 9999) days = 9999;
}

static bool infoAppend(char* out, size_t cap, size_t& len, const char* fmt, ...) {
  if (!out || cap == 0 || len >= cap) return false;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(out + len, cap - len, fmt, ap);
  va_end(ap);
  if (n < 0) return false;
  if ((size_t)n >= cap - len) {
    len = cap - 1;
    out[len] = '\0';
    return false;
  }
  len += (size_t)n;
  return true;
}

bool formatSystemInfo(char* out, size_t outCap) {
  if (!out || outCap == 0) return false;
  long rssi = WiFi.RSSI();
  uint32_t freeHeap = 0, totalHeap = 0, freePs = 0, totalPs = 0;
  boardMemTotals(freeHeap, totalHeap);
  boardPsramTotals(freePs, totalPs);
  unsigned long days = 0, hours = 0, minutes = 0, seconds = 0;
  uptimeDhms(days, hours, minutes, seconds);

  size_t len = 0;
  out[0] = '\0';
  infoAppend(out, outCap, len,
             "📊 **System Diagnostics:**\n"
             "- **Uptime:** %lud %luh %lum %lus\n"
             "- **Internal heap:** %lu / %lu bytes\n",
             days, hours, minutes, seconds,
             (unsigned long)freeHeap, (unsigned long)totalHeap);
  if (totalPs > 0) {
    infoAppend(out, outCap, len, "- **PSRAM:** %lu / %lu bytes\n",
               (unsigned long)freePs, (unsigned long)totalPs);
  } else {
    infoAppend(out, outCap, len, "- **PSRAM:** none\n");
  }
  infoAppend(out, outCap, len,
             "- **WiFi RSSI:** %ld dBm\n"
             "- **Gateway Status:** %s\n"
             "- **MmLog Core0 drops:** %lu (ring overflow)\n",
             rssi,
             (gatewayConnected && identified) ? "Connected" : "Disconnected",
             (unsigned long)mmLogDropCore0.load());
  {
    uint8_t n = cmdErrorReplyCount();
    infoAppend(out, outCap, len, "- **Cmd errors (%u/%u):**\n",
               (unsigned)n, (unsigned)CMD_ERR_RING_N);
    if (n == 0) {
      infoAppend(out, outCap, len, "  (none)\n");
    } else {
      uint8_t show = n;
      if (show > 5) show = 5;
      for (uint8_t i = 0; i < show; i++) {
        char row[97];
        if (!cmdErrorReplyNewest(i, row, sizeof(row))) break;
        truncateTextBuf(row, 120);
        infoAppend(out, outCap, len, "  - %s\n", row);
      }
      if (n > show) {
        infoAppend(out, outCap, len, "  - ... +%u more (Serial / Log panel)\n",
                   (unsigned)(n - show));
      }
    }
  }
  infoAppend(out, outCap, len,
             "- **Firmware:** https://github.com/K9DTV/minime-ii-esp32-discord-bot");
  return true;
}

String getSystemInfo() {
  char buf[CMD_REPORT_MAX];
  formatSystemInfo(buf, sizeof(buf));
  return String(buf);
}
