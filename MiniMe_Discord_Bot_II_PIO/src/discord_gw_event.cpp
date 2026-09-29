#include "discord_gw_internal.h"

// Discord Gateway WebSocket event dispatcher (WStype_* to session/state/log helpers).

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

      // Identify-only after drops. Do not beginSslWithBundle again -- library reconnects to BIND_HOST.
      // Wifi up: few fast tries, then climb (3/7/12..40s). Wifi down: same climb (no fast flood).
      if (wifiUp) {
        if (!gwFastIdentifyPending) {
          gwBeginDropEpisode("disconnect");
        }
        gwSetReconnectBackoff(false);
      } else {
        gwClearSession("wifi_down");
        gwFastIdentifyPending = false;
        gwSetReconnectBackoff(false);
      }
      ensureWifiForGateway();
      // No LCD setTransient here: full-frame QSPI flush on every drop starves TLS/HB.
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
        gwBeginDropEpisode("op7");
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
        gwBeginDropEpisode("op9");
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

          // HTTPS/commands run from Core 1 loop via drainDiscordCmds -- not inline here.
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
