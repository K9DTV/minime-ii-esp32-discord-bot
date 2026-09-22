#include "minime.h"
#include "esp_crt_bundle.h"

WiFiClientSecure httpsClient;
bool httpsInUse = false;

void boardMemTotals(uint32_t& memFree, uint32_t& memTotal) {
  uint32_t psramSize = ESP.getPsramSize();
  uint32_t psramFree = ESP.getFreePsram();
  if (psramSize < BOARD_PSRAM_BYTES) psramSize = BOARD_PSRAM_BYTES;
  if (ESP.getPsramSize() == 0 || psramFree < 1) psramFree = BOARD_PSRAM_BYTES;
  memTotal = ESP.getHeapSize() + psramSize;
  memFree = ESP.getFreeHeap() + psramFree;
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
  uint32_t freeHeap = 0, totalHeap = 0;
  boardMemTotals(freeHeap, totalHeap);
  unsigned long days = 0, hours = 0, minutes = 0, seconds = 0;
  uptimeDhms(days, hours, minutes, seconds);
  String uptimeStr = String(days) + "d " + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s";
  return "📊 **System Diagnostics:**\n"
         "• **Uptime:** " + uptimeStr + "\n"
         "• **Free Heap:** " + String((unsigned long)freeHeap) + " / " +
         String((unsigned long)totalHeap) + " bytes\n"
         "• **WiFi RSSI:** " + String(rssi) + " dBm\n"
         "• **IP:** " + WiFi.localIP().toString() + "\n"
         "• **OTA host:** " + String(OTA_HOSTNAME) + ".local\n"
         "• **Gateway Status:** " + String((gatewayConnected && identified) ? "Connected" : "Disconnected") + "\n"
         "• **Firmware:** https://github.com/K9DTV/minime-ii-esp32-discord-bot";
}

bool sendDiscordMessage(const String& channelId, const String& content, bool suppressEmbeds) {
  String post = content;
  if (post.length() > DISCORD_CONTENT_MAX) post = post.substring(0, DISCORD_CONTENT_MAX - 3) + "...";
  if (!httpsAcquire("discord.com")) return false;
  String url = "/api/v10/channels/" + channelId + "/messages";
  StaticJsonDocument<4096> doc;
  doc["content"] = post;
  doc["tts"] = false;
  if (suppressEmbeds) doc["flags"] = 4; // SUPPRESS_EMBEDS: link stays a URL, no GitHub card

  String body;
  serializeJson(doc, body);
  String request =
    "POST " + url + " HTTP/1.1\r\n"
    "Host: discord.com\r\n"
    "Authorization: Bot " + String(BOT_TOKEN) + "\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: " + String(body.length()) + "\r\n"
    "Connection: close\r\n\r\n" +
    body;
  httpsClient.print(request);
  unsigned long deadline = millis() + 8000UL;
  String statusLine;
  bool chunked = false;
  int contentLength = -1;
  if (!httpsAwaitHeaders(deadline, false, statusLine, chunked, contentLength)) {
    httpsRelease();
    return false;
  }
  // Status line only — message POST body is unused; Connection: close + httpsRelease() discards it.
  httpsRelease();
  int code = 0;
  int sp = statusLine.indexOf(' ');
  if (sp >= 0) code = statusLine.substring(sp + 1).toInt();
  return code >= 200 && code < 300;
}

// Cap header/status lines so a broken peer cannot grow String without bound.
static const size_t HTTP_LINE_MAX = 512;

static bool readHttpLineCapped(Client& client, String& out, unsigned long deadlineMs) {
  out = "";
  while (millis() <= deadlineMs) {
    if (!client.available()) {
      // Peer closed mid-line: partial is not a complete line.
      if (!client.connected() && !client.available()) return false;
      delay(1);
      continue;
    }
    int b = client.read();
    if (b < 0) continue;
    char c = (char)b;
    if (c == '\n') return true;
    if (c == '\r') continue;
    if (out.length() < HTTP_LINE_MAX) out += c;
  }
  return false;
}

// Skip status + headers; fill Transfer-Encoding / Content-Length for the body reader.
static bool skipHttpHeaders(Client& client, unsigned long timeoutMs,
                            bool& outChunked, int& outContentLength) {
  outChunked = false;
  outContentLength = -1;
  unsigned long deadline = millis() + timeoutMs;
  while (client.available() == 0) {
    if (millis() > deadline) return false;
    delay(1);
  }
  // Status line
  String line;
  if (!readHttpLineCapped(client, line, deadline)) return false;
  while (millis() <= deadline && (client.connected() || client.available())) {
    if (!readHttpLineCapped(client, line, deadline)) return false;
    if (line.length() == 0) return true;
    String lower = line;
    lower.toLowerCase();
    if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) {
      outChunked = true;
    }
    if (lower.startsWith("content-length:")) {
      outContentLength = lower.substring(lower.indexOf(':') + 1).toInt();
    }
  }
  return false;
}

bool httpsConnect(const char* host, uint32_t timeoutMs) {
  httpsClient.stop();
  // Verify server certs. Never setInsecure -- BOT_TOKEN / API keys must not ride MITM TLS.
  // Call after every stop() -- NetworkClientSecure drops the attach callback on stop.
  // Arduino-ESP32 3.3.12+: useBuiltinCACertBundle() (IDF Mozilla bundle in the core).
  // Older 3.x (incl. many IDE installs): setCACertBundle with IDF-linked
  // _binary_x509_crt_bundle_* symbols + size (required since ~3.0.4).
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 12))
  httpsClient.useBuiltinCACertBundle();
#else
  extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
  extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
  httpsClient.setCACertBundle(rootca_crt_bundle_start,
                              (size_t)(rootca_crt_bundle_end - rootca_crt_bundle_start));
#endif
  httpsClient.setTimeout(timeoutMs);
  httpsClient.setHandshakeTimeout((timeoutMs + 999UL) / 1000UL);
  return httpsClient.connect(host, 443);
}

void httpsRelease() {
  httpsClient.stop();
  httpsInUse = false;
}

bool httpsAcquire(const char* host, uint32_t timeoutMs) {
  if (httpsInUse) return false;
  httpsInUse = true;
  if (!httpsConnect(host, timeoutMs)) {
    httpsRelease();
    return false;
  }
  return true;
}

// Returns 0=ok (httpsInUse held until httpsRelease), 1=busy, 2=header timeout, 3=connect/TLS/DNS fail.
uint8_t httpsGetOpen(const char* host, const String& path, unsigned long headerTimeoutMs,
                     bool& outChunked, int& outContentLength,
                     const char* userAgent, const char* extraHeaders) {
  outChunked = false;
  outContentLength = -1;
  if (httpsInUse) return 1;
  if (!httpsAcquire(host)) return 3;
  String req = String("GET ") + path + " HTTP/1.1\r\n"
               "Host: " + host + "\r\n"
               "User-Agent: " + userAgent + "\r\n";
  if (extraHeaders && extraHeaders[0]) req += extraHeaders;
  req += "Connection: close\r\n\r\n";
  httpsClient.print(req);
  if (!skipHttpHeaders(httpsClient, headerTimeoutMs, outChunked, outContentLength)) {
    httpsRelease();
    return 2;
  }
  return 0;
}

uint8_t httpGetOpen(WiFiClient& client, const char* host, const String& path,
                    unsigned long headerTimeoutMs, bool& outChunked, int& outContentLength) {
  outChunked = false;
  outContentLength = -1;
  if (!client.connect(host, 80)) return 3;
  client.print(String("GET ") + path + " HTTP/1.1\r\n"
               "Host: " + host + "\r\n"
               "Connection: close\r\n\r\n");
  if (!skipHttpHeaders(client, headerTimeoutMs, outChunked, outContentLength)) {
    client.stop();
    return 2;
  }
  return 0;
}

void setHttpOpenError(String& outReport, uint8_t err, const char* label) {
  outReport = String(label);
  if (err == 1) outReport += " busy.";
  else if (err == 2) outReport += " header timeout.";
  else if (err == 3) outReport += " connect/TLS failed.";
  else outReport += " connection failed.";
}

bool httpsAwaitHeaders(unsigned long deadlineMs, bool pump, String& outStatus,
                       bool& chunked, int& contentLength) {
  while (httpsClient.available() == 0) {
    if (millis() > deadlineMs) {
      httpsClient.stop();
      return false;
    }
    if (pump) pumpGateway();
    delay(10);
  }
  if (!readHttpLineCapped(httpsClient, outStatus, deadlineMs)) {
    httpsClient.stop();
    return false;
  }
  chunked = false;
  contentLength = -1;
  while (millis() <= deadlineMs) {
    String line;
    if (!readHttpLineCapped(httpsClient, line, deadlineMs)) {
      httpsClient.stop();
      return false;
    }
    if (line.length() == 0) break;
    String lower = line;
    lower.toLowerCase();
    if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) {
      chunked = true;
    }
    if (lower.startsWith("content-length:")) {
      contentLength = lower.substring(lower.indexOf(':') + 1).toInt();
    }
  }
  return true;
}

bool readHttpBodyAfterHeaders(Client& client, bool chunked, int contentLength,
                              String& outBody, unsigned long deadlineMs) {
  outBody = "";
  const size_t maxBody = 48000;
  if (chunked) {
    while (millis() < deadlineMs) {
      while (!client.available() && client.connected() && millis() < deadlineMs) {
        pumpGateway();
        delay(5);
      }
      if (!client.available()) break;
      String sizeLine;
      if (!readHttpLineCapped(client, sizeLine, deadlineMs)) break;
      if (sizeLine.length() == 0) continue;
      int sc = sizeLine.indexOf(';');
      if (sc >= 0) sizeLine = sizeLine.substring(0, sc);
      long chunkSize = strtol(sizeLine.c_str(), nullptr, 16);
      if (chunkSize <= 0) break;
      long got = 0;
      bool hitCap = false;
      while (got < chunkSize && millis() < deadlineMs) {
        if (client.available()) {
          char ch = (char)client.read();
          got++;
          if (!hitCap) {
            outBody += ch;
            if (outBody.length() >= maxBody) hitCap = true;
          }
        } else if (!client.connected()) {
          break;
        } else {
          pumpGateway();
          delay(1);
        }
      }
      // Always consume trailing CRLF after the chunk (or abandon socket).
      String trailer;
      if (!readHttpLineCapped(client, trailer, deadlineMs)) {
        client.stop();
        return outBody.length() > 0;
      }
      if (hitCap) {
        client.stop();
        return false; // capped body is incomplete — not success
      }
    }
    return outBody.length() > 0;
  }
  if (contentLength > 0) {
    while ((int)outBody.length() < contentLength && millis() < deadlineMs) {
      while (client.available()) {
        outBody += (char)client.read();
        if (outBody.length() >= maxBody) {
          client.stop();
          return false; // truncated
        }
        if ((int)outBody.length() >= contentLength) break;
      }
      if (!client.connected() && !client.available()) break;
      pumpGateway();
      delay(5);
    }
    return outBody.length() > 0;
  }
  while (millis() < deadlineMs) {
    while (client.available()) {
      outBody += (char)client.read();
      if (outBody.length() >= maxBody) {
        client.stop();
        return false; // truncated
      }
    }
    if (!client.connected() && !client.available()) break;
    pumpGateway();
    delay(10);
  }
  return outBody.length() > 0;
}

bool discordIdLooksValid(const String& id) {
  if (id.length() < 16) return false;
  for (unsigned int i = 0; i < id.length(); i++) {
    char c = id.charAt(i);
    if (c < '0' || c > '9') return false;
  }
  return true;
}

bool discordRestGet(const String& path, String& outBody, String& outStatus) {
  outBody = "";
  if (!httpsAcquire("discord.com", 15000)) {
    outStatus = httpsInUse ? "busy" : "connect failed";
    return false;
  }
  String request =
    "GET " + path + " HTTP/1.1\r\n"
    "Host: discord.com\r\n"
    "Authorization: Bot " + String(BOT_TOKEN) + "\r\n"
    "Accept: application/json\r\n"
    "Accept-Encoding: identity\r\n"
    "User-Agent: MiniMeBot/1.0\r\n"
    "Connection: close\r\n\r\n";
  httpsClient.print(request);

  unsigned long deadline = millis() + 15000UL;
  bool chunked = false;
  int contentLength = -1;
  if (!httpsAwaitHeaders(deadline, false, outStatus, chunked, contentLength)) {
    httpsRelease();
    outStatus = "timeout";
    return false;
  }
  bool ok = readHttpBodyAfterHeaders(httpsClient, chunked, contentLength, outBody, deadline);
  httpsRelease();
  return ok;
}

String guildIdFromChannel(const String& channelId) {
  if (!discordIdLooksValid(channelId)) return "";

  String body, status;
  if (!discordRestGet("/api/v10/channels/" + channelId, body, status)) {
    return "";
  }
  int jsonStart = body.indexOf('{');
  if (jsonStart < 0) {
    return "";
  }
  if (jsonStart > 0) body = body.substring(jsonStart);

  StaticJsonDocument<64> filter;
  filter["guild_id"] = true;
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    return "";
  }
  String gid = doc["guild_id"] | "";
  return gid;
}

bool appendMembersFromGuild(const String& guildId, uint8_t maxToAdd) {
  if (!discordIdLooksValid(guildId)) return false;

  String body, status;
  String path = "/api/v10/guilds/" + guildId + "/members?limit=200";
  if (!discordRestGet(path, body, status)) {
    return false;
  }

  int jsonStart = body.indexOf('[');
  int objStart = body.indexOf('{');
  if (jsonStart < 0 || (objStart >= 0 && objStart < jsonStart)) {
    return false;
  }
  if (jsonStart > 0) body = body.substring(jsonStart);

  StaticJsonDocument<256> filter;
  filter[0]["nick"] = true;
  filter[0]["user"]["id"] = true;
  filter[0]["user"]["username"] = true;
  filter[0]["user"]["global_name"] = true;
  filter[0]["user"]["bot"] = true;

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    return false;
  }

  JsonArray members = doc.as<JsonArray>();
  if (members.isNull()) {
    return false;
  }

  uint8_t added = 0;
  for (JsonObject member : members) {
    if (added >= maxToAdd) break;
    int slot = findFreeTrackedSlot();
    if (slot < 0) break;
    if (member["user"]["bot"] == true) continue;
    String uid = member["user"]["id"] | "";
    String name = member["nick"] | "";
    if (name.length() == 0) name = discordDisplayName(member["user"]);
    if (uid.length() == 0 || name.length() == 0) continue;
    if (findUserIndex(uid) >= 0) continue;
    fillTrackedSlot((uint8_t)slot, uid, name);
    added++;
  }

  return added > 0;
}

bool fetchGuildMembersAtStartup() {
  initTrackedUsers();
  cachedGuildCount = 0;
  rememberGuildId(String(BOT_GUILD_ID));
  rememberGuildId(guildIdFromChannel(TARGET_CHANNEL_ID));
  rememberGuildId(guildIdFromChannel(TARGET_CHANNEL_ID1));

  if (cachedGuildCount == 0) {
    return false;
  }

  bool any = false;
  uint8_t share = (cachedGuildCount > 0) ? (MAX_TRACKED_USERS / cachedGuildCount) : MAX_TRACKED_USERS;
  if (share < 1) share = 1;
  for (uint8_t g = 0; g < cachedGuildCount; g++) {
    if (appendMembersFromGuild(cachedGuildIds[g], share)) any = true;
  }
  for (uint8_t g = 0; g < cachedGuildCount; g++) {
    if (appendMembersFromGuild(cachedGuildIds[g], MAX_TRACKED_USERS)) any = true;
  }
  return any;
}
