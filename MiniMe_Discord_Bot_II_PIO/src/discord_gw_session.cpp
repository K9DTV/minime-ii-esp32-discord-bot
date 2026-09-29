#include "discord_gw_internal.h"
#include "esp_wifi.h"

// Clear Discord session fields; always fresh IDENTIFY after the next connect.
void gwClearSession(const char* reason) {
  sessionId = "";
  lastSeq = 0;
  char buf[64];
  snprintf(buf, sizeof(buf), "CLEAR_SESSION %s", reason ? reason : "");
  gwLogAppend(buf);
}

void gwSetReconnectIntervalMs(unsigned long ms) {
  gwReconnectIntervalMs = ms;
  gatewayWS.setReconnectInterval(gwReconnectIntervalMs);
  char buf[48];
  snprintf(buf, sizeof(buf), "RECONNECT_INTERVAL_MS=%lu", (unsigned long)gwReconnectIntervalMs);
  gwLogAppend(buf);
}

// Start a drop episode: clear session + reset fail count. Interval via gwSetReconnectBackoff.
void gwBeginDropEpisode(const char* reason) {
  gwClearSession(reason);
  gwFastIdentifyPending = true;
  gwReconnectFailCount = 0; // new drop episode -- next backoff(false) starts at fast tries
}

static unsigned long gwNextBackoffMs(uint8_t failCount) {
  // failCount is 1..n after each failed reconnect attempt.
  if (failCount == 0) return GW_RECONNECT_BASE_MS;
  if (failCount <= GW_RECONNECT_FAST_TRIES) return GW_RECONNECT_FAST_MS;
  // Climb: 3s, 7s, 12s, then +8s toward 40s.
  static const unsigned long kClimb[] = { 3000UL, 7000UL, 12000UL };
  uint8_t step = (uint8_t)(failCount - GW_RECONNECT_FAST_TRIES - 1);
  if (step < 3) return kClimb[step];
  unsigned long ms = 12000UL + (unsigned long)(step - 2) * 8000UL;
  if (ms > GW_RECONNECT_MAX_MS) ms = GW_RECONNECT_MAX_MS;
  return ms;
}

void gwSetReconnectBackoff(bool reset) {
  if (reset) {
    gwReconnectFailCount = 0;
    gwSetReconnectIntervalMs(GW_RECONNECT_BASE_MS);
    return;
  }
  if (gwReconnectFailCount < 255) gwReconnectFailCount++;
  gwSetReconnectIntervalMs(gwNextBackoffMs(gwReconnectFailCount));
}

void ensureWifiForGateway() {
  // Never tear Wi-Fi under nested pump (TLS body/header wait) or while HTTPS holds the socket.
  if (gwPumping || httpsInUse) return;
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - gwLastWifiKickMillis < 10000UL) return;
  gwLastWifiKickMillis = now;
  gwLogAppend("WIFI_RETRY begin()");
  WiFi.disconnect();
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

static void bindGatewayHost(const char* host) {
  if (!host || !host[0]) host = "gateway.discord.gg";
  // beginSSL() with no CA calls setInsecure() inside WebSockets -- BOT_TOKEN would ride
  // unverified TLS. beginSslWithBundle uses the same ESP32 Mozilla CA blob as REST.
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 4))
  extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
  extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
  gatewayWS.beginSslWithBundle(host, 443, "/?v=10&encoding=json",
                               rootca_crt_bundle_start,
                               (size_t)(rootca_crt_bundle_end - rootca_crt_bundle_start));
#else
#error "ESP32 Arduino core >= 3.0.4 required for Gateway CA-verified TLS (beginSslWithBundle)"
#endif
  gatewayWS.onEvent(gatewayEvent);
  gatewayWS.setReconnectInterval(gwReconnectIntervalMs);
  char bindMsg[64];
  snprintf(bindMsg, sizeof(bindMsg), "BIND_HOST %s", host);
  gwLogAppend(bindMsg);
}

void connectGateway() {
  // One beginSslWithBundle for the life of the bot. After drops, only setReconnectInterval +
  // disconnect(); do not beginSslWithBundle again (fights the library reconnect timer).
  initGwJsonFilter();
  MmLog.print("[GW] intents=");
  MmLog.println(INTENTS_MINIME);
  bindGatewayHost("gateway.discord.gg");
}

void gwParkReconnectForOta() {
  gwSetReconnectIntervalMs(3600000UL);
}

void gwRestoreReconnectAfterOta() {
  gwSetReconnectIntervalMs(GW_RECONNECT_BASE_MS);
}


void gwYieldForTlsHeadroom() {
  const uint32_t minMaxAlloc = 24576UL;
  uint32_t maxAlloc = ESP.getMaxAllocHeap();
  if (maxAlloc >= minMaxAlloc) return;
  MmLog.print(F("[TLS] low maxAlloc="));
  MmLog.print(maxAlloc);
  MmLog.println(F(" drop GW for headroom"));
  // Same pattern as OP7/HB: arm library reconnect, then disconnect.
  gwBeginDropEpisode("tls_headroom");
  gwSetReconnectBackoff(true); // base interval, not climbing backoff
  gatewayWS.disconnect();
  delay(20);
  MmLog.print(F("[TLS] after GW drop heap="));
  MmLog.print(ESP.getFreeHeap());
  MmLog.print(F(" maxAlloc="));
  MmLog.println(ESP.getMaxAllocHeap());
}
