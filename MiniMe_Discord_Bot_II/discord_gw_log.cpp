#include "discord_gw_internal.h"
#include <string.h>

void gwLogAppend(const char* ev) {
  if (!ev || !ev[0]) return;
  if (gwLogCount > 0 && strcmp(gwLogLastAdded, ev) == 0) return;
  strncpy(gwLogLastAdded, ev, GW_LOG_COLS);
  gwLogLastAdded[GW_LOG_COLS] = '\0';
  char line[GW_LOG_COLS + 1];
  snprintf(line, sizeof(line), "[%lu] %s", (unsigned long)millis(), ev);
  MmLog.print("[GW] ");
  MmLog.println(line);
  if (gwLogCount < GW_LOG_MAX) {
    strncpy(gwLog[gwLogCount], line, GW_LOG_COLS);
    gwLog[gwLogCount][GW_LOG_COLS] = '\0';
    gwLogCount++;
  } else {
    for (uint8_t i = 1; i < GW_LOG_MAX; i++) {
      memcpy(gwLog[i - 1], gwLog[i], GW_LOG_COLS + 1);
    }
    strncpy(gwLog[GW_LOG_MAX - 1], line, GW_LOG_COLS);
    gwLog[GW_LOG_MAX - 1][GW_LOG_COLS] = '\0';
  }
}

void gwLogEvent(const String& ev) {
  gwLogAppend(ev.c_str());
}

// kind = coarse category; detail = full text. Log DROP_START once, then only when
// kind OR detail changes (identical lines suppressed). 5s DROP still reminder separate.
void gwNoteDrop(const char* kind, const char* detail) {
  if (!kind) kind = "";
  if (!detail) detail = "";
  if (!gwInDropState) {
    gwInDropState = true;
    gwDropStartedMillis = millis();
    strncpy(gwDropStartEvent, detail, GW_LOG_COLS);
    gwDropStartEvent[GW_LOG_COLS] = '\0';
    strncpy(gwLastDropKind, kind, sizeof(gwLastDropKind) - 1);
    gwLastDropKind[sizeof(gwLastDropKind) - 1] = '\0';
    strncpy(gwLastDropDetail, detail, GW_LOG_COLS);
    gwLastDropDetail[GW_LOG_COLS] = '\0';
    gwLastDropRemindMillis = millis();
    char start[GW_LOG_COLS + 1];
    snprintf(start, sizeof(start), "DROP_START: %s", detail);
    gwLogAppend(start);
  } else if (strcmp(kind, gwLastDropKind) != 0 || strcmp(detail, gwLastDropDetail) != 0) {
    strncpy(gwLastDropKind, kind, sizeof(gwLastDropKind) - 1);
    gwLastDropKind[sizeof(gwLastDropKind) - 1] = '\0';
    strncpy(gwLastDropDetail, detail, GW_LOG_COLS);
    gwLastDropDetail[GW_LOG_COLS] = '\0';
    gwLogAppend(detail);
  }
}

void gwClearDropState() {
  if (!gwInDropState) return;
  gwLogAppend("RECOVERED");
  gwInDropState = false;
  gwDropStartEvent[0] = '\0';
  gwLastDropKind[0] = '\0';
  gwLastDropDetail[0] = '\0';
  gwLoggedConnectDuringDrop = false;
  gwDropStartedMillis = 0;
  gwFastIdentifyPending = false;
}

void gwSerialService() {
  unsigned long now = millis();
  // Alive pulse (60 s) -- Serial panel only (outside FULL LOG markers).
  static unsigned long gwLastAliveMillis = 0;
  if (gwLastAliveMillis == 0) gwLastAliveMillis = now;
  if (now - gwLastAliveMillis >= 60000UL) {
    gwLastAliveMillis = now;
    MmLog.print("[GW] alive up_ms=");
    MmLog.print(now);
    MmLog.print(" wifi=");
    MmLog.print(WiFi.status() == WL_CONNECTED ? "up" : "DOWN");
    MmLog.print(" rssi=");
    MmLog.print(WiFi.RSSI());
    MmLog.print(" gw=");
    MmLog.print(gatewayConnected ? "1" : "0");
    MmLog.print(" id=");
    MmLog.print(identified ? "1" : "0");
    MmLog.print(" drop=");
    MmLog.println(gwInDropState ? "1" : "0");
  }
  if (gwInDropState && gwDropStartEvent[0] &&
      (now - gwLastDropRemindMillis >= 5000UL)) {
    gwLastDropRemindMillis = now;
    MmLog.print("[GW] DROP still (started): ");
    MmLog.println(gwDropStartEvent);
  }
  // Dump drop/reconnect ring into LOG panel (web routes FULL LOG..END LOG -> fulllog).
  if (now - gwLastFullLogMillis >= 60000UL) {
    gwLastFullLogMillis = now;
    MmLog.println("[GW] === FULL LOG ===");
    if (gwLogCount == 0) {
      MmLog.println("  (empty)");
    } else {
      for (uint8_t i = 0; i < gwLogCount; i++) {
        MmLog.print("  ");
        MmLog.println(gwLog[i]);
      }
    }
    if (gwInDropState) {
      MmLog.print("  drop_start=");
      MmLog.println(gwDropStartEvent);
    }
    MmLog.println("[GW] === END LOG ===");
  }
}
