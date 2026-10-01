#include "discord_gw_internal.h"

void initGwJsonFilter() {
  if (gwFilterReady) return;
  gwFilter.clear();
  gwFilter["op"] = true;
  gwFilter["s"] = true;
  gwFilter["t"] = true;
  gwFilter["d"]["heartbeat_interval"] = true;
  gwFilter["d"]["session_id"] = true;
  gwFilter["d"]["resume_gateway_url"] = true;
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
  char sent[48];
  snprintf(sent, sizeof(sent), "SENT_IDENTIFY why=%s", gwSessionClearReason);
  gwLogAppend(sent);
  snprintf(gwSessionClearReason, sizeof(gwSessionClearReason), "no_session");
  applyCpuForBotOnline(true);
}

// Op 6: Discord replays missed events after seq, then sends RESUMED (or OP9 if too late).
void sendResume() {
  JsonDocument doc;
  doc["op"] = 6;
  JsonObject d = doc["d"].to<JsonObject>();
  d["token"] = BOT_TOKEN;
  d["session_id"] = sessionId;
  d["seq"] = lastSeq;
  gwSendJson(doc);
  gwResumeSent = true;
  char sent[48];
  snprintf(sent, sizeof(sent), "SENT_RESUME seq=%d try=%u/%u",
           lastSeq, (unsigned)gwResumeTries, (unsigned)GW_RESUME_MAX_TRIES);
  gwLogAppend(sent);
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
