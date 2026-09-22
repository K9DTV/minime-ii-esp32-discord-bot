#include "minime.h"
#include "cores.h"
#include "esp_crt_bundle.h"
#include <new>

WiFiClientSecure httpsClient;
bool httpsInUse = false;

// Gateway HB always; drain cmds only when shared HTTPS is free (DeepSeek has its own TLS).
// No TWDT here — that stays deferred after 0.7.30/0.7.34 panics.
static void pumpNetWait() {
  pumpGateway();
  if (!httpsInUse) drainDiscordCmds();
}

void boardMemTotals(uint32_t& memFree, uint32_t& memTotal) {
  // Internal SRAM only — LCD/web heap bar must not hide internal exhaustion behind free PSRAM.
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

bool sendDiscordCmdError(const String& channelId, const String& content, bool suppressEmbeds) {
  noteCmdErrorReply(content.c_str());
  return sendDiscordMessage(channelId, content, suppressEmbeds);
}

bool sendDiscordMessage(const String& channelId, const String& content, bool suppressEmbeds) {
  String post = content;
  if (post.length() > DISCORD_CONTENT_MAX) post = post.substring(0, DISCORD_CONTENT_MAX - 3) + "...";
  JsonDocument doc;
  doc["content"] = post;
  doc["tts"] = false;
  if (suppressEmbeds) doc["flags"] = 4; // SUPPRESS_EMBEDS: link stays a URL, no GitHub card

  String body;
  serializeJson(doc, body);
  String url = "/api/v10/channels/" + channelId + "/messages";
  String request =
    "POST " + url + " HTTP/1.1\r\n"
    "Host: discord.com\r\n"
    "Authorization: Bot " + String(BOT_TOKEN) + "\r\n"
    "Content-Type: application/json\r\n"
    "User-Agent: " MINIME_USER_AGENT "\r\n"
    "Content-Length: " + String(body.length()) + "\r\n"
    "Connection: close\r\n\r\n" +
    body;

  for (uint8_t attempt = 0; attempt < DISCORD_REST_MAX_ATTEMPTS; attempt++) {
    if (!httpsAcquire("discord.com")) return false;
    httpsClient.print(request);
    unsigned long deadline = millis() + 8000UL;
    String statusLine;
    bool chunked = false;
    int contentLength = -1;
    float retryAfterSec = -1.f;
    // pump=true: HB stays alive while waiting for Discord headers / during retry backoff.
    if (!httpsAwaitHeaders(httpsClient, deadline, true, statusLine, chunked, contentLength, &retryAfterSec)) {
      httpsRelease();
      MmLog.print(F("[REST] Discord header timeout attempt "));
      MmLog.print((unsigned)(attempt + 1));
      MmLog.print(F("/"));
      MmLog.println((unsigned)DISCORD_REST_MAX_ATTEMPTS);
      if (attempt + 1 >= DISCORD_REST_MAX_ATTEMPTS) return false;
      unsigned long waitUntil = millis() + DISCORD_HEADER_RETRY_WAIT_MS;
      while ((long)(millis() - waitUntil) < 0) {
        pumpNetWait();
        delay(10);
      }
      continue;
    }
    int code = 0;
    int sp = statusLine.indexOf(' ');
    if (sp >= 0) code = statusLine.substring(sp + 1).toInt();

    if (code >= 200 && code < 300) {
      // Body unused; Connection: close + release discards it.
      httpsRelease();
      return true;
    }

    if (code == 429) {
      // Prefer Retry-After header; else skim JSON body for "retry_after".
      if (retryAfterSec < 0.f) {
        String errBody;
        unsigned long bodyDeadline = millis() + 3000UL;
        if (readHttpBodyAfterHeaders(httpsClient, chunked, contentLength, errBody, bodyDeadline)) {
          int idx = errBody.indexOf("\"retry_after\"");
          if (idx >= 0) {
            int colon = errBody.indexOf(':', idx);
            if (colon >= 0) retryAfterSec = errBody.substring(colon + 1).toFloat();
          }
        }
      }
      httpsRelease();

      unsigned long waitMs = DISCORD_429_WAIT_MIN_MS;
      if (retryAfterSec > 0.f) {
        waitMs = (unsigned long)(retryAfterSec * 1000.f + 0.5f);
      }
      if (waitMs < DISCORD_429_WAIT_MIN_MS) waitMs = DISCORD_429_WAIT_MIN_MS;
      if (waitMs > DISCORD_429_WAIT_MAX_MS) waitMs = DISCORD_429_WAIT_MAX_MS;

      MmLog.print(F("[REST] Discord 429 attempt "));
      MmLog.print((unsigned)(attempt + 1));
      MmLog.print(F("/"));
      MmLog.print((unsigned)DISCORD_REST_MAX_ATTEMPTS);
      MmLog.print(F(" wait_ms="));
      MmLog.println((unsigned long)waitMs);

      unsigned long waitUntil = millis() + waitMs;
      while ((long)(millis() - waitUntil) < 0) {
        pumpNetWait();
        delay(10);
      }
      continue;
    }

    httpsRelease();
    return false;
  }
  return false;
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
  if (!userAgent || !userAgent[0]) userAgent = MINIME_USER_AGENT;
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
               "User-Agent: " + String(MINIME_USER_AGENT) + "\r\n"
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

bool httpsAwaitHeaders(Client& client, unsigned long deadlineMs, bool pump, String& outStatus,
                       bool& chunked, int& contentLength, float* outRetryAfterSec) {
  if (outRetryAfterSec) *outRetryAfterSec = -1.f;
  while (client.available() == 0) {
    if (millis() > deadlineMs) {
      client.stop();
      return false;
    }
    if (pump) {
      pumpNetWait();
    }
    delay(10);
  }
  if (!readHttpLineCapped(client, outStatus, deadlineMs)) {
    client.stop();
    return false;
  }
  chunked = false;
  contentLength = -1;
  while (millis() <= deadlineMs) {
    String line;
    if (!readHttpLineCapped(client, line, deadlineMs)) {
      client.stop();
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
    if (outRetryAfterSec && lower.startsWith("retry-after:")) {
      // Discord sends seconds (int/float). Ignore HTTP-date forms (.toFloat() == 0).
      float v = lower.substring(lower.indexOf(':') + 1).toFloat();
      if (v > 0.f) *outRetryAfterSec = v;
    }
  }
  return true;
}

bool readHttpBodyAfterHeaders(Client& client, bool chunked, int contentLength,
                              String& outBody, unsigned long deadlineMs) {
  outBody = "";
  const size_t maxBody = 48000;
  char blk[256];
  if (chunked) {
    uint8_t emptySizeLines = 0;
    while (millis() < deadlineMs) {
      while (!client.available() && client.connected() && millis() < deadlineMs) {
        pumpNetWait();
        delay(5);
      }
      if (!client.available()) {
        // Missing final 0-chunk (or timeout). Do not treat accumulated body as success.
        client.stop();
        return false;
      }
      String sizeLine;
      if (!readHttpLineCapped(client, sizeLine, deadlineMs)) {
        client.stop();
        return false;
      }
      // Rare bare CRLF between chunks (malformed / CDN quirk). Bound so endless CRLFs
      // cannot spin past deadlineMs without failing. Trailer already ate the post-chunk CRLF.
      if (sizeLine.length() == 0) {
        if (++emptySizeLines > 8) {
          client.stop();
          return false;
        }
        continue;
      }
      emptySizeLines = 0;
      int sc = sizeLine.indexOf(';');
      if (sc >= 0) sizeLine = sizeLine.substring(0, sc);
      long chunkSize = strtol(sizeLine.c_str(), nullptr, 16);
      if (chunkSize <= 0) {
        // Final 0-size chunk: complete message (empty body still false for callers).
        return outBody.length() > 0;
      }
      if (outBody.length() + (size_t)chunkSize <= maxBody) {
        outBody.reserve(outBody.length() + (size_t)chunkSize);
      }
      long got = 0;
      bool hitCap = false;
      while (got < chunkSize && millis() < deadlineMs) {
        if (client.available()) {
          size_t want = (size_t)(chunkSize - got);
          if (want > sizeof(blk)) want = sizeof(blk);
          int n = client.read((uint8_t*)blk, want);
          if (n <= 0) {
            pumpNetWait();
            delay(1);
            continue;
          }
          got += n;
          if (!hitCap) {
            if (outBody.length() + (size_t)n > maxBody) {
              hitCap = true;
            } else {
              outBody.concat(blk, (unsigned int)n);
            }
          }
        } else if (!client.connected()) {
          client.stop();
          return false; // truncated mid-chunk
        } else {
          pumpNetWait();
          delay(1);
        }
      }
      if (got < chunkSize) {
        client.stop();
        return false; // deadline mid-chunk
      }
      // Always consume trailing CRLF after the chunk (or abandon socket).
      String trailer;
      if (!readHttpLineCapped(client, trailer, deadlineMs)) {
        client.stop();
        return false; // partial body is not success (same class as 0.7.8 CL truncate)
      }
      if (hitCap) {
        client.stop();
        return false; // capped body is incomplete — not success
      }
    }
    client.stop();
    return false; // deadline without final 0-chunk
  }
  if (contentLength > 0) {
    size_t need = (size_t)contentLength;
    if (need > maxBody) need = maxBody;
    outBody.reserve(need);
    while ((int)outBody.length() < contentLength && millis() < deadlineMs) {
      while (client.available()) {
        size_t remain = (size_t)contentLength - outBody.length();
        if (remain > sizeof(blk)) remain = sizeof(blk);
        int n = client.read((uint8_t*)blk, remain);
        if (n <= 0) break;
        if (outBody.length() + (size_t)n > maxBody) {
          client.stop();
          return false; // truncated
        }
        outBody.concat(blk, (unsigned int)n);
        if ((int)outBody.length() >= contentLength) break;
      }
      if (!client.connected() && !client.available()) break;
      pumpNetWait();
      delay(5);
    }
    if ((int)outBody.length() < contentLength) {
      client.stop();
      return false; // incomplete Content-Length body
    }
    return true;
  }
  // Until-close fallback (no chunked, no Content-Length). Best-effort only: peer close
  // with a partial body still returns length>0. Callers must validate JSON / shape.
  while (millis() < deadlineMs) {
    while (client.available()) {
      size_t room = maxBody - outBody.length();
      if (room == 0) {
        client.stop();
        return false; // truncated
      }
      size_t want = room;
      if (want > sizeof(blk)) want = sizeof(blk);
      int n = client.read((uint8_t*)blk, want);
      if (n <= 0) break;
      outBody.concat(blk, (unsigned int)n);
    }
    if (!client.connected() && !client.available()) break;
    pumpNetWait();
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
    "User-Agent: " + String(MINIME_USER_AGENT) + "\r\n"
    "Connection: close\r\n\r\n";
  httpsClient.print(request);

  unsigned long deadline = millis() + 15000UL;
  bool chunked = false;
  int contentLength = -1;
  if (!httpsAwaitHeaders(httpsClient, deadline, false, outStatus, chunked, contentLength)) {
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

  JsonDocument filter;
  filter["guild_id"] = true;
  JsonDocument doc;
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

  JsonDocument filter;
  filter[0]["nick"] = true;
  filter[0]["user"]["id"] = true;
  filter[0]["user"]["username"] = true;
  filter[0]["user"]["global_name"] = true;
  filter[0]["user"]["bot"] = true;

  // Heap JsonDocument — not on Core 1 setup/loop stack.
  static JsonDocument* memberDoc = nullptr;
  if (!memberDoc) {
    memberDoc = new (std::nothrow) JsonDocument();
  }
  if (!memberDoc) return false;
  memberDoc->clear();
  // Parse from offset — avoid body.substring() second large String copy.
  DeserializationError err = deserializeJson(
      *memberDoc, body.c_str() + jsonStart, DeserializationOption::Filter(filter));
  if (err) {
    return false;
  }

  JsonArray members = memberDoc->as<JsonArray>();
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
