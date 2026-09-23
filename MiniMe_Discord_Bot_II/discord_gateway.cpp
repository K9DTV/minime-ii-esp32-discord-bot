#include "minime.h"
#include "cores.h"
#include "esp_wifi.h"

WebSocketsClient gatewayWS;
JsonDocument* gwDoc = nullptr;
bool gatewayConnected     = false;
bool identified           = false;
bool gotHello             = false;
int  heartbeatIntervalMs   = 0;
unsigned long lastHeartbeatMillis = 0;
int lastSeq               = 0;
String sessionId;
unsigned long lastBotActivityMillis = 0;
uint8_t botDiscordStatus = 0;

// Serial drop/reconnect diagnostics (ring dumped to LOG panel every 60 s).
static const uint8_t GW_LOG_MAX = 40;
static const uint8_t GW_LOG_COLS = 96;
static char gwLog[GW_LOG_MAX][GW_LOG_COLS + 1];
static uint8_t gwLogCount = 0;
static char gwLogLastAdded[GW_LOG_COLS + 1];
static char gwDropStartEvent[GW_LOG_COLS + 1];
static bool gwInDropState = false;
static unsigned long gwLastDropRemindMillis = 0;
static unsigned long gwLastFullLogMillis = 0;
static unsigned long gwReconnectIntervalMs = 3000;
// Fast tries after a drop (wifi up), then climb: 3s -> 7s -> 12s, +8s steps, cap 40s.
// Avoids hammering Discord every 5s (or 200ms) for the length of a real outage.
static const unsigned long GW_RECONNECT_FAST_MS = 200UL;
static const uint8_t GW_RECONNECT_FAST_TRIES = 3;
static const unsigned long GW_RECONNECT_BASE_MS = 3000UL;
static const unsigned long GW_RECONNECT_MAX_MS = 40000UL;
static uint8_t gwReconnectFailCount = 0;
static unsigned long gwLastWifiKickMillis = 0;
static char gwLastDropKind[32];
static bool gwLoggedConnectDuringDrop = false;
static unsigned long gwDropStartedMillis = 0;
static unsigned long gwLastDisconnectMillis = 0;
static bool gwFastIdentifyPending = false; // DISCONNECTED must not climb back to 5s after OP7/OP9
static bool hbAckPending = false;
static unsigned long hbSentMillis = 0;
// Nested pumpGateway (re-entry while gatewayWS.loop runs): skip — do not re-enter loop().
static bool gwPumping = false;
static bool gwDeferPresenceOnline = false;

// ArduinoJson filter for all Gateway TEXT frames. Built once in connectGateway — not on first message.
static JsonDocument gwFilter;
static bool gwFilterReady = false;

static void initGwJsonFilter() {
  if (gwFilterReady) return;
  gwFilter.clear();
  gwFilter["op"] = true;
  gwFilter["s"] = true;
  gwFilter["t"] = true;
  gwFilter["d"]["heartbeat_interval"] = true;
  gwFilter["d"]["session_id"] = true;
  gwFilter["d"]["status"] = true; // PRESENCE_UPDATE top-level status (not only d.presences[])
  gwFilter["d"]["user"]["id"] = true;
  gwFilter["d"]["user"]["username"] = true;
  gwFilter["d"]["user"]["global_name"] = true;
  gwFilter["d"]["content"] = true;
  gwFilter["d"]["channel_id"] = true;
  gwFilter["d"]["guild_id"] = true;
  gwFilter["d"]["author"]["id"] = true;
  gwFilter["d"]["author"]["username"] = true;
  gwFilter["d"]["author"]["global_name"] = true;
  gwFilter["d"]["author"]["bot"] = true;
  gwFilter["d"]["mentions"][0]["id"] = true;
  gwFilter["d"]["presences"][0]["user"]["id"] = true;
  gwFilter["d"]["presences"][0]["status"] = true;
  gwFilter["d"]["guilds"][0]["presences"][0]["user"]["id"] = true;
  gwFilter["d"]["guilds"][0]["presences"][0]["status"] = true;
  gwFilterReady = true;
}

static void gwLogAppend(const char* ev) {
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

// kind = coarse category (dedupe); detail = full text for first DROP_START / new kinds
static void gwNoteDrop(const char* kind, const char* detail) {
  if (!kind) kind = "";
  if (!detail) detail = "";
  if (!gwInDropState) {
    gwInDropState = true;
    gwDropStartedMillis = millis();
    strncpy(gwDropStartEvent, detail, GW_LOG_COLS);
    gwDropStartEvent[GW_LOG_COLS] = '\0';
    strncpy(gwLastDropKind, kind, sizeof(gwLastDropKind) - 1);
    gwLastDropKind[sizeof(gwLastDropKind) - 1] = '\0';
    gwLastDropRemindMillis = millis();
    char start[GW_LOG_COLS + 1];
    snprintf(start, sizeof(start), "DROP_START: %s", detail);
    gwLogAppend(start);
  } else if (strcmp(kind, gwLastDropKind) != 0) {
    strncpy(gwLastDropKind, kind, sizeof(gwLastDropKind) - 1);
    gwLastDropKind[sizeof(gwLastDropKind) - 1] = '\0';
    gwLogAppend(detail);
  }
}

static void gwClearDropState() {
  if (!gwInDropState) return;
  gwLogAppend("RECOVERED");
  gwInDropState = false;
  gwDropStartEvent[0] = '\0';
  gwLastDropKind[0] = '\0';
  gwLoggedConnectDuringDrop = false;
  gwDropStartedMillis = 0;
  gwFastIdentifyPending = false;
}

// Clear Discord session fields; always fresh IDENTIFY after the next connect.
static void gwClearSession(const char* reason) {
  sessionId = "";
  lastSeq = 0;
  char buf[64];
  snprintf(buf, sizeof(buf), "CLEAR_SESSION %s", reason ? reason : "");
  gwLogAppend(buf);
}

static void gwSetReconnectIntervalMs(unsigned long ms) {
  gwReconnectIntervalMs = ms;
  gatewayWS.setReconnectInterval(gwReconnectIntervalMs);
  char buf[48];
  snprintf(buf, sizeof(buf), "RECONNECT_INTERVAL_MS=%lu", (unsigned long)gwReconnectIntervalMs);
  gwLogAppend(buf);
}

// Drop path: clear session; interval comes from gwSetReconnectBackoff (fast then climb).
static void gwArmFastIdentify(const char* reason) {
  gwClearSession(reason);
  gwFastIdentifyPending = true;
  gwReconnectFailCount = 0; // new drop episode — next backoff(false) starts at fast tries
}

void gwSerialService() {
  unsigned long now = millis();
  // Alive pulse (60 s) — Serial panel only (outside FULL LOG markers).
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

static void gwSetReconnectBackoff(bool reset) {
  if (reset) {
    gwReconnectFailCount = 0;
    gwSetReconnectIntervalMs(GW_RECONNECT_BASE_MS);
    return;
  }
  if (gwReconnectFailCount < 255) gwReconnectFailCount++;
  gwSetReconnectIntervalMs(gwNextBackoffMs(gwReconnectFailCount));
}

static void ensureWifiForGateway() {
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
  // beginSSL() with no CA calls setInsecure() inside WebSockets — BOT_TOKEN would ride
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

void gwSendJson(JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);
  gatewayWS.sendTXT(payload);
}

void requestTrackedUserPresences() {
  if (cachedGuildCount == 0) return;
  bool any = false;
  for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) {
    if (trackedUsers[i].active && trackedUsers[i].userId[0]) {
      any = true;
      break;
    }
  }
  if (!any) return;

  for (uint8_t g = 0; g < cachedGuildCount; g++) {
    if (strlen(cachedGuildIds[g]) < 16) continue;
    JsonDocument doc;
    doc["op"] = 8;
    JsonObject d = doc["d"].to<JsonObject>();
    d["guild_id"] = cachedGuildIds[g];
    d["limit"] = 0;
    d["presences"] = true;
    JsonArray ids = d["user_ids"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) {
      if (trackedUsers[i].active && trackedUsers[i].userId[0]) {
        ids.add(trackedUsers[i].userId);
      }
    }
    gwSendJson(doc);
  }
}

void sendBotPresence(const char* status, bool afk) {
  if (!gatewayConnected || !identified) return;
  JsonDocument doc;
  doc["op"] = 3;
  JsonObject d = doc["d"].to<JsonObject>();
  d["since"] = nullptr;
  d["activities"].to<JsonArray>();
  d["status"] = status;
  d["afk"] = afk;
  gwSendJson(doc);
}

static void applyCpuForBotOnline(bool online) {
  const uint32_t want = online ? CPU_MHZ_ACTIVE : CPU_MHZ_IDLE;
  if (getCpuFrequencyMhz() != want) setCpuFrequencyMhz(want);
}

void noteBotActivity() {
  // Always stamp; idle uses this after reconnect too (no forced Idle for 5 min if recent activity).
  lastBotActivityMillis = millis();
  applyCpuForBotOnline(true);
  if (!gatewayConnected || !identified) return;
  if (botDiscordStatus == 2) return;
  botDiscordStatus = 2;
  // Do not gwSendJson from inside gatewayWS.loop dispatch; flush after outer pump returns.
  if (gwPumping) {
    gwDeferPresenceOnline = true;
    return;
  }
  sendBotPresence("online", false);
}

void updateBotPresenceIdle() {
  if (lastBotActivityMillis == 0) {
    lastBotActivityMillis = millis();
    return;
  }
  if (millis() - lastBotActivityMillis < BOT_PRESENCE_IDLE_MS) return;
  // Drop CPU even if Gateway never identified (commands/OTA may have bumped to 240).
  if (!gatewayConnected || !identified) {
    applyCpuForBotOnline(false);
    return;
  }
  if (botDiscordStatus == 1) return;
  botDiscordStatus = 1;
  sendBotPresence("idle", true);
  applyCpuForBotOnline(false);
}

void sendIdentify() {
  JsonDocument doc;
  doc["op"] = 2;
  JsonObject d = doc["d"].to<JsonObject>();
  d["token"] = BOT_TOKEN;
  d["properties"].to<JsonObject>(); // Discord accepts empty properties
  d["compress"] = false;
  d["large_threshold"] = 250;
  d["intents"] = INTENTS_MINIME;
  JsonObject presence = d["presence"].to<JsonObject>();
  presence["since"] = nullptr;
  presence["activities"].to<JsonArray>();
  presence["status"] = "online";
  presence["afk"] = false;
  gwSendJson(doc);
  lastBotActivityMillis = millis();
  botDiscordStatus = 2;
  gwLogAppend("SENT_IDENTIFY");
  applyCpuForBotOnline(true);
}

void sendHeartbeat() {
  JsonDocument doc;
  doc["op"] = 1;
  if (lastSeq == 0) {
    doc["d"] = nullptr;
  } else {
    doc["d"] = lastSeq;
  }
  gwSendJson(doc);
  hbAckPending = true;
  hbSentMillis = millis();
}

// Heartbeat / wifi kick. Called from the outer pumpGateway only (not nested).
// Wi-Fi reconnect is skipped while gwPumping/httpsInUse (see ensureWifiForGateway).
static void pumpGatewayKeepAlive() {
  if (!gatewayConnected && WiFi.status() != WL_CONNECTED) {
    ensureWifiForGateway();
  }
  if (heartbeatIntervalMs <= 0 || !gatewayConnected || !gotHello) return;
  unsigned long now = millis();
  unsigned long hbInterval = (unsigned long)heartbeatIntervalMs;
  unsigned long hbAckDeadline = hbInterval + GW_HB_ACK_GRACE_MS;
  if (hbAckPending) {
    if (now - hbSentMillis >= hbAckDeadline) {
      char toMsg[48];
      snprintf(toMsg, sizeof(toMsg), "HB_ACK_TIMEOUT after_ms=%lu",
               (unsigned long)(now - hbSentMillis));
      gwLogAppend(toMsg);
      gwArmFastIdentify("hb_ack");
      gwSetReconnectBackoff(false);
      hbAckPending = false;
      gatewayWS.disconnect();
    }
    return;
  }
  if (now - lastHeartbeatMillis >= hbInterval) {
    lastHeartbeatMillis = now;
    sendHeartbeat();
  }
}

void pumpGateway() {
  // Re-entry guard: gatewayWS.loop must not nest. After dual-core, commands/HTTPS run from
  // loop() via drainDiscordCmds (not inside the WS callback), so nested pumps are rare;
  // if they happen, skip entirely (0.7.40 pruned the old nested HB-only path).
  if (gwPumping) return;

  gwPumping = true;
  gatewayWS.loop();
  gwSerialService();
  pumpGatewayKeepAlive();

  if (gwDeferPresenceOnline) {
    gwDeferPresenceOnline = false;
    if (gatewayConnected && identified && botDiscordStatus == 2) {
      sendBotPresence("online", false);
    }
  }
  gwPumping = false;
}

void gwParkReconnectForOta() {
  gwSetReconnectIntervalMs(3600000UL);
}

void gwRestoreReconnectAfterOta() {
  gwSetReconnectIntervalMs(GW_RECONNECT_BASE_MS);
}

void gatewayEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED: {
      gatewayConnected = false;
      identified       = false;
      gotHello         = false;
      botDiscordStatus = 0;
      heartbeatIntervalMs = 0;
      hbAckPending = false;

      bool wifiUp = (WiFi.status() == WL_CONNECTED);
      const char* kind = wifiUp ? "WS_DISCONNECTED_WIFI_UP" : "WS_DISCONNECTED_WIFI_DOWN";
      char detail[GW_LOG_COLS + 1];
      size_t n = 0;
      n += (size_t)snprintf(detail + n, sizeof(detail) - n, "%s", kind);
      if (payload && length > 0 && n + 9 < sizeof(detail)) {
        n += (size_t)snprintf(detail + n, sizeof(detail) - n, " reason=");
        size_t lim = length < 40 ? length : 40;
        for (size_t i = 0; i < lim && n + 1 < sizeof(detail); i++) {
          char c = (char)payload[i];
          if (c >= 32 && c < 127) detail[n++] = c;
        }
        detail[n] = '\0';
      }
      snprintf(detail + n, sizeof(detail) - n, " rssi=%ld seq=%d session=%s",
               (long)WiFi.RSSI(), lastSeq, sessionId.length() ? "yes" : "no");

      gwNoteDrop(kind, detail);
      gwLoggedConnectDuringDrop = false;
      gwLastDisconnectMillis = millis();
      char discAt[96];
      snprintf(discAt, sizeof(discAt),
               "DISCONNECT_AT millis=%lu rssi=%ld heap=%lu psram=%lu",
               (unsigned long)gwLastDisconnectMillis, (long)WiFi.RSSI(),
               (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram());
      gwLogAppend(discAt);
      noteLastEvent(wifiUp ? "GW drop" : "GW wifi down");

      // Identify-only after drops. Do not beginSslWithBundle again — library reconnects to BIND_HOST.
      // Wifi up: few fast tries, then climb (3/7/12..40s). Wifi down: same climb (no fast flood).
      if (wifiUp) {
        if (!gwFastIdentifyPending) {
          gwArmFastIdentify("disconnect");
        }
        gwSetReconnectBackoff(false);
      } else {
        gwClearSession("wifi_down");
        gwFastIdentifyPending = false;
        gwSetReconnectBackoff(false);
      }
      ensureWifiForGateway();
      // No LCD showTransient here: full-frame QSPI flush on every drop starves TLS/HB.
      break;
    }
    case WStype_CONNECTED: {
      gatewayConnected = true;
      gwFastIdentifyPending = false;
      unsigned long nowMs = millis();
      unsigned long gapMs = gwLastDisconnectMillis ? (nowMs - gwLastDisconnectMillis) : 0;
      char connAt[64];
      snprintf(connAt, sizeof(connAt), "CONNECT_AT millis=%lu gap_ms=%lu",
               nowMs, gapMs);
      gwLogAppend(connAt);
      if (!gwInDropState || !gwLoggedConnectDuringDrop) {
        gwLogAppend("WS_CONNECTED");
        if (gwInDropState) gwLoggedConnectDuringDrop = true;
      }
      break;
    }
    case WStype_ERROR: {
      char detail[GW_LOG_COLS + 1];
      size_t n = (size_t)snprintf(detail, sizeof(detail), "WS_ERROR");
      if (payload && length > 0 && n + 2 < sizeof(detail)) {
        detail[n++] = ' ';
        size_t lim = length < 80 ? length : 80;
        for (size_t i = 0; i < lim && n + 1 < sizeof(detail); i++) {
          char c = (char)payload[i];
          if (c >= 32 && c < 127) detail[n++] = c;
        }
        detail[n] = '\0';
      }
      gwNoteDrop("WS_ERROR", detail);
      break;
    }
    case WStype_TEXT: {
      if (!gwFilterReady) initGwJsonFilter(); // belt: connectGateway should already have done this
      if (!gwDoc) return;
      gwDoc->clear();
      DeserializationError err = deserializeJson(*gwDoc, payload, length, DeserializationOption::Filter(gwFilter));
      if (err) {
        char jerr[48];
        snprintf(jerr, sizeof(jerr), "JSON_ERR %s", err.c_str());
        gwLogAppend(jerr);
        return;
      }
      int op = (*gwDoc)["op"] | -1;
      if (!(*gwDoc)["s"].isNull()) {
        lastSeq = (*gwDoc)["s"].as<int>();
      }

      // Hello: start HB (jittered first), then Identify
      if (op == 10) {
        heartbeatIntervalMs = (*gwDoc)["d"]["heartbeat_interval"] | 0;
        hbAckPending = false;
        if (heartbeatIntervalMs > 0) {
          unsigned long jitter = (unsigned long)(esp_random() % (uint32_t)heartbeatIntervalMs);
          lastHeartbeatMillis = millis() - ((unsigned long)heartbeatIntervalMs - jitter);
        } else {
          lastHeartbeatMillis = millis();
        }
        gotHello = true;
        char hello[40];
        snprintf(hello, sizeof(hello), "OP10_HELLO hb_ms=%d", heartbeatIntervalMs);
        gwLogAppend(hello);
        sendIdentify();
        return;
      }

      // Reconnect: clear session; library reconnects to same BIND_HOST (no second beginSslWithBundle)
      if (op == 7) {
        gwNoteDrop("OP7_RECONNECT", "OP7_RECONNECT");
        gwArmFastIdentify("op7");
        gwSetReconnectBackoff(false);
        gatewayWS.disconnect();
        return;
      }

      // Invalid Session: fresh IDENTIFY
      if (op == 9) {
        bool resumable = false;
        JsonDocument small;
        if (!deserializeJson(small, payload, length)) {
          resumable = small["d"] | false;
        }
        char detail[48];
        snprintf(detail, sizeof(detail), "OP9_INVALID_SESSION resumable=%c",
                 resumable ? '1' : '0');
        gwNoteDrop(detail, detail);
        gwArmFastIdentify("op9");
        gwSetReconnectBackoff(false);
        gatewayWS.disconnect();
        return;
      }

      if (op == 11) {
        hbAckPending = false;
        return;
      }

      if (op == 0) {
        const char* t = (*gwDoc)["t"];
        if (!t) return;
        if (strcmp(t, "READY") == 0) {
          identified = true;
          sessionId = (*gwDoc)["d"]["session_id"] | "";
          gwSetReconnectBackoff(true);
          gwClearDropState();
          gwLogAppend(sessionId.length() ? "READY session=yes" : "READY session=no");
          JsonArray guilds = (*gwDoc)["d"]["guilds"].as<JsonArray>();
          if (!guilds.isNull()) {
            for (JsonObject g : guilds) {
              applyPresencesArray(g["presences"].as<JsonArray>());
            }
          }
          requestTrackedUserPresences();
          return;
        }
        if (strcmp(t, "RESUMED") == 0) {
          // Identify-only firmware should not see RESUMED; treat like READY cleanup.
          identified = true;
          gwSetReconnectBackoff(true);
          gwClearDropState();
          gwLogAppend("RESUMED");
          requestTrackedUserPresences();
          return;
        }
        if (strcmp(t, "GUILD_CREATE") == 0) {
          applyPresencesArray((*gwDoc)["d"]["presences"].as<JsonArray>());
          requestTrackedUserPresences();
          return;
        }
        if (strcmp(t, "GUILD_MEMBERS_CHUNK") == 0) {
          applyPresencesArray((*gwDoc)["d"]["presences"].as<JsonArray>());
          return;
        }
        if (strcmp(t, "PRESENCE_UPDATE") == 0) {
          handlePresenceUpdate((*gwDoc)["d"]);
          return;
        }
        if (strcmp(t, "MESSAGE_CREATE") == 0) {
          JsonObject d = (*gwDoc)["d"];
          if (d["author"]["bot"] == true) return;
          String content   = d["content"].as<String>();
          String channelId = d["channel_id"].as<String>();
          String authorId  = d["author"]["id"].as<String>();
          String authorName = discordDisplayName(d["author"]);
          bool isDM = d["guild_id"].isNull();

          // Owner alerts (LCD flags; !clear clears). No GPIO set1/set2.
          if (isDM) {
            // Sticky LCD flags until owner !clear (no auto-expiry).
            alertDm.store(true);
            noteLastEvent("DM");
          } else {
            bool ownerMentioned = false;
            JsonArray mentions = d["mentions"].as<JsonArray>();
            if (!mentions.isNull()) {
              for (JsonObject m : mentions) {
                const char* mid = m["id"] | "";
                if (mid[0] && strcmp(mid, OWNER_ID_STR) == 0) {
                  ownerMentioned = true;
                  break;
                }
              }
            }
            if (!ownerMentioned) {
              String ping = String("<@") + OWNER_ID_STR + ">";
              String pingNick = String("<@!") + OWNER_ID_STR + ">";
              if (content.indexOf(ping) >= 0 || content.indexOf(pingNick) >= 0) {
                ownerMentioned = true;
              }
            }
            if (ownerMentioned) {
              // Sticky LCD flags until owner !clear (no auto-expiry).
              alertMention.store(true);
              noteLastEvent("Mention");
            }
          }

          // HTTPS/commands run from Core 1 loop via drainDiscordCmds — not inline here.
          if (!enqueueDiscordCmd(content, authorId, authorName, channelId, isDM)) {
            gwLogAppend("CMDQ full");
          }
        }
      }
      break;
    }
    default:
      break;
  }
}
