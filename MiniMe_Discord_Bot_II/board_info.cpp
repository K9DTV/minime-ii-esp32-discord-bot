#include "minime.h"

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

String getSystemInfo() {
  long rssi = WiFi.RSSI();
  uint32_t freeHeap = 0, totalHeap = 0, freePs = 0, totalPs = 0;
  boardMemTotals(freeHeap, totalHeap);
  boardPsramTotals(freePs, totalPs);
  unsigned long days = 0, hours = 0, minutes = 0, seconds = 0;
  uptimeDhms(days, hours, minutes, seconds);
  String uptimeStr = String(days) + "d " + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";
  String msg = "📊 **System Diagnostics:**\n"
         "• **Uptime:** " + uptimeStr + "\n"
         "• **Internal heap:** " + String((unsigned long)freeHeap) + " / " +
         String((unsigned long)totalHeap) + " bytes\n";
  if (totalPs > 0) {
    msg += "• **PSRAM:** " + String((unsigned long)freePs) + " / " +
           String((unsigned long)totalPs) + " bytes\n";
  } else {
    msg += "• **PSRAM:** none\n";
  }
  msg += "• **WiFi RSSI:** " + String(rssi) + " dBm\n"
         "• **Gateway Status:** " + String((gatewayConnected && identified) ? "Connected" : "Disconnected") + "\n"
         "• **MmLog Core0 drops:** " + String((unsigned long)mmLogDropCore0.load()) + " (ring overflow)\n";
  {
    uint8_t n = cmdErrorReplyCount();
    msg += "• **Cmd errors (" + String((unsigned)n) + "/" + String((unsigned)CMD_ERR_RING_N) + "):**\n";
    if (n == 0) {
      msg += "  (none)\n";
    } else {
      // Newest first; cap so !sys still fits Discord 2000.
      uint8_t show = n;
      if (show > 5) show = 5;
      for (uint8_t i = 0; i < show; i++) {
        char row[97];
        if (!cmdErrorReplyNewest(i, row, sizeof(row))) break;
        msg += "  - " + truncateText(String(row), 120) + "\n";
      }
      if (n > show) msg += "  - … +" + String((unsigned)(n - show)) + " more (Serial / Log panel)\n";
    }
  }
  msg += "• **Firmware:** https://github.com/K9DTV/minime-ii-esp32-discord-bot";
  return msg;
}
