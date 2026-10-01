#include "discord_gw_internal.h"
#include "esp_wifi.h"

void GatewayWsClient::disconnectWithCode(uint16_t code) {
  if (clientIsConnected(&_client)) {
    WebSockets::clientDisconnect(&_client, code);
  }
}

void GatewayWsClient::setReconnectHost(const char* host) {
  _host = host;
}

static bool gwSessionHeld() {
  return sessionId.length() || lastSeq != 0 || gwResumeHost[0];
}

// Library auto-reconnect target: resume host while a session is held, else the primary host.
static void gwAimReconnectHost() {
  gatewayWS.setReconnectHost(gwResumeHost[0] ? gwResumeHost : GW_PRIMARY_HOST);
}

// Drop the Discord session: the next Hello sends IDENTIFY (why=reason) on the primary host.
void gwClearSession(const char* reason) {
  if (!reason || !reason[0]) reason = "?";
  snprintf(gwSessionClearReason, sizeof(gwSessionClearReason), "%s", reason);
  if (!gwSessionHeld()) return;
  sessionId = "";
  lastSeq = 0;
  gwResumeHost[0] = '\0';
  gwResumeTries = 0;
  gwResumeSent = false;
  gwAimReconnectHost();
  char buf[64];
  snprintf(buf, sizeof(buf), "CLEAR_SESSION %s", reason);
  gwLogAppend(buf);
}

static const char* gwResumeBlocker() {
  if (!sessionId.length() || lastSeq <= 0) return "no_session";
  if (gwResumeTries >= GW_RESUME_MAX_TRIES) return "resume_tries";
  return nullptr;
}

bool gwCanResume() {
  return gwResumeBlocker() == nullptr;
}

// The socket that sent RESUME dropped before RESUMED / OP9. The library never reports the
// server's close code, so a 4007/4009 close looks the same as a network drop.
void gwResumeLostOnDrop() {
  if (!gwResumeSent) return;
  gwResumeSent = false;
  char buf[48];
  snprintf(buf, sizeof(buf), "RESUME_NO_REPLY try=%u/%u",
           (unsigned)gwResumeTries, (unsigned)GW_RESUME_MAX_TRIES);
  gwLogAppend(buf);
  if (gwResumeTries >= GW_RESUME_MAX_TRIES) gwClearSession("resume_tries");
}

// READY resume_gateway_url, e.g. "wss://gateway-us-east1-b.discord.gg". Anything but a plain
// *.discord.gg host leaves out empty, and RESUME then goes to the primary host.
static void gwParseResumeHost(const char* url, char* out, size_t outLen) {
  static const char kScheme[] = "wss://";
  static const char kSuffix[] = ".discord.gg";
  const size_t schemeLen = sizeof(kScheme) - 1;
  const size_t suffixLen = sizeof(kSuffix) - 1;
  out[0] = '\0';
  if (!url || strncmp(url, kScheme, schemeLen) != 0) return;
  const char* host = url + schemeLen;
  size_t n = 0;
  while (host[n] && host[n] != '/' && host[n] != '?') {
    char c = host[n];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    if (!ok || n + 1 >= outLen) return;
    n++;
  }
  if (n <= suffixLen || strncmp(host + n - suffixLen, kSuffix, suffixLen) != 0) return;
  memcpy(out, host, n);
  out[n] = '\0';
}

void gwSetResumeHost(const char* resumeGatewayUrl) {
  gwParseResumeHost(resumeGatewayUrl, gwResumeHost, sizeof(gwResumeHost));
  gwAimReconnectHost();
  char buf[GW_HOST_MAX + 16];
  snprintf(buf, sizeof(buf), "RESUME_HOST %s",
           gwResumeHost[0] ? gwResumeHost : "none (resume on primary)");
  gwLogAppend(buf);
}

// OP10 Hello: RESUME the held session, else IDENTIFY (SENT_IDENTIFY why= says what blocked it).
void gwSendResumeOrIdentify() {
  const char* blocker = gwResumeBlocker();
  if (!blocker) {
    gwResumeTries++;
    sendResume();
    return;
  }
  if (gwSessionHeld()) gwClearSession(blocker);
  sendIdentify();
}

void gwSetReconnectIntervalMs(unsigned long ms) {
  gwReconnectIntervalMs = ms;
  gatewayWS.setReconnectInterval(gwReconnectIntervalMs);
  char buf[48];
  snprintf(buf, sizeof(buf), "RECONNECT_INTERVAL_MS=%lu", (unsigned long)gwReconnectIntervalMs);
  gwLogAppend(buf);
}

// Start a drop episode + reset fail count. Interval via gwSetReconnectBackoff.
// tryResume keeps a still-resumable session (next Hello sends RESUME); otherwise clear it.
void gwBeginDropEpisode(const char* reason, bool tryResume) {
  gwResumeLostOnDrop();
  const char* blocker = tryResume ? gwResumeBlocker() : reason;
  if (!blocker) {
    char buf[48];
    snprintf(buf, sizeof(buf), "RESUME_ARMED %s seq=%d", reason ? reason : "", lastSeq);
    gwLogAppend(buf);
  } else if (!tryResume || gwSessionHeld()) {
    gwClearSession(blocker);
  }
  gwFastReconnectPending = true;
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
  if (!host || !host[0]) host = GW_PRIMARY_HOST;
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
  // Initial bind. After normal drops the library reconnects on its timer (to the resume host
  // while a session is held); if that stalls (no CONNECTED while wifi-up in a drop),
  // pumpGatewayKeepAlive calls gwMaybeRebindIfStuck() which drops the session and re-runs
  // beginSslWithBundle + onEvent + setReconnectInterval on the primary host.
  initGwJsonFilter();
  MmLog.print("[GW] intents=");
  MmLog.println(INTENTS_MINIME);
  bindGatewayHost(GW_PRIMARY_HOST);
  gwLastConnectOrRebindMillis = millis();
}

void gwMaybeRebindIfStuck() {
  // Pump/keepalive only -- never from the websocket callback.
  if (!gwInDropState || gatewayConnected) return;
  if (WiFi.status() != WL_CONNECTED) return;
  // Leave OTA park alone (1h interval); do not rebind into a flash window.
  if (gwReconnectIntervalMs >= 3600000UL) return;

  unsigned long now = millis();
  unsigned long waitMs = 2UL * gwReconnectIntervalMs;
  if (waitMs < GW_REBIND_MIN_MS) waitMs = GW_REBIND_MIN_MS;
  // Anchor = last CONNECTED/rebind, but never before this drop started (else a long
  // prior session would make OP7 rebind on the very next pump).
  unsigned long anchor = gwLastConnectOrRebindMillis;
  if (gwDropStartedMillis && (anchor == 0 || anchor < gwDropStartedMillis))
    anchor = gwDropStartedMillis;
  if (anchor == 0) anchor = now;
  if (now - anchor < waitMs) return;

  gwLastConnectOrRebindMillis = now;
  char rebindMsg[48];
  snprintf(rebindMsg, sizeof(rebindMsg), "GW_REBIND stuck_ms=%lu", (unsigned long)(now - anchor));
  gwLogAppend(rebindMsg);
  gwClearSession("rebind");
  gatewayWS.disconnect(); // begin() forgets a half-open client without freeing it
  bindGatewayHost(GW_PRIMARY_HOST);
}

void gwParkReconnectForOta() {
  gwClearSession("ota"); // ota.cpp then disconnect()s, and close 1000 ends the session anyway
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
  // Same pattern as OP7/HB: arm library reconnect + RESUME, then close without ending the session.
  gwBeginDropEpisode("tls_headroom", true);
  gwSetReconnectBackoff(true); // base interval, not climbing backoff
  gatewayWS.disconnectWithCode(GW_RESUME_CLOSE_CODE);
  delay(20);
  MmLog.print(F("[TLS] after GW drop heap="));
  MmLog.print(ESP.getFreeHeap());
  MmLog.print(F(" maxAlloc="));
  MmLog.println(ESP.getMaxAllocHeap());
}
