#include "minime.h"
#include "cores.h"
#include "esp_crt_bundle.h"
#include "mbedtls/platform.h"
#include <new>

// Discord REST client lifecycle + messaging (body reader lives in discord_http.cpp).

WiFiClientSecure httpsClient;
bool httpsInUse = false;

#if MINIME_TLS_PSRAM
// This core builds mbedTLS with CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC: every TLS block comes from
// internal SRAM (~45 KB per session), too much for the Gateway and an HTTPS session together.
// Blocks this size and up (chiefly the two ~16 KB record buffers per session) go to PSRAM;
// smaller ones stay internal.
static const size_t TLS_PSRAM_MIN_BLOCK = 4096;

static void* tlsCalloc(size_t n, size_t size) {
  size_t total = 0;
  if (__builtin_mul_overflow(n, size, &total)) return nullptr;
  void* p = nullptr;
  if (total >= TLS_PSRAM_MIN_BLOCK) p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}

static void tlsFree(void* p) {
  heap_caps_free(p);
}
#endif

void tlsUsePsramForLargeBlocks() {
#if MINIME_TLS_PSRAM
  if (!psramFound()) {
    MmLog.println(F("[TLS] no PSRAM: mbedTLS blocks stay internal"));
    return;
  }
  if (mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree) != 0) {
    MmLog.println(F("[TLS] mbedTLS allocator hook failed: blocks stay internal"));
    return;
  }
  MmLog.print(F("[TLS] mbedTLS blocks >= "));
  MmLog.print((unsigned)TLS_PSRAM_MIN_BLOCK);
  MmLog.println(F(" B in PSRAM"));
#else
  MmLog.println(F("[TLS] MINIME_TLS_PSRAM 0: mbedTLS blocks stay internal"));
#endif
}

bool sendDiscordCmdError(const char* channelId, const char* content, bool suppressEmbeds) {
  noteCmdErrorReply(content ? content : "");
  return sendDiscordMessage(channelId, content, suppressEmbeds);
}

bool sendDiscordCmdError(const String& channelId, const String& content, bool suppressEmbeds) {
  return sendDiscordCmdError(channelId.c_str(), content.c_str(), suppressEmbeds);
}

// Escape for Discord JSON content (Core 1 only; not reentrant with concurrent send).
static size_t jsonEscape(const char* in, char* out, size_t outSize) {
  size_t o = 0;
  for (size_t i = 0; in && in[i] && o + 2 < outSize; i++) {
    const char c = in[i];
    if (c == '"' || c == '\\') {
      out[o++] = '\\';
      out[o++] = c;
    } else if (c == '\n') {
      out[o++] = '\\';
      out[o++] = 'n';
    } else if (c == '\r') {
      out[o++] = '\\';
      out[o++] = 'r';
    } else if (c == '\t') {
      out[o++] = '\\';
      out[o++] = 't';
    } else if ((uint8_t)c < 0x20) {
      // skip other controls
    } else {
      out[o++] = c;
    }
  }
  out[o] = '\0';
  return o;
}

bool sendDiscordMessage(const char* channelId, const char* content, bool suppressEmbeds) {
  if (!channelId || !channelId[0] || !content) return false;
  unsigned long startedAt = millis();

  // Truncate to Discord cap, then escape into fixed buffers (.bss -- Core 1, not reentrant).
  static char raw[DISCORD_CONTENT_MAX + 1];
  static char esc[DISCORD_CONTENT_MAX * 2 + 4];
  static char body[DISCORD_CONTENT_MAX * 2 + 80];
  static char request[DISCORD_CONTENT_MAX * 2 + 400];

  size_t n = strlen(content);
  if (n > (size_t)DISCORD_CONTENT_MAX) {
    memcpy(raw, content, (size_t)DISCORD_CONTENT_MAX - 3);
    raw[DISCORD_CONTENT_MAX - 3] = '.';
    raw[DISCORD_CONTENT_MAX - 2] = '.';
    raw[DISCORD_CONTENT_MAX - 1] = '.';
    raw[DISCORD_CONTENT_MAX] = '\0';
  } else {
    memcpy(raw, content, n);
    raw[n] = '\0';
  }
  jsonEscape(raw, esc, sizeof(esc));

  int bodyLen = snprintf(body, sizeof(body),
                         "{\"content\":\"%s\",\"tts\":false%s}",
                         esc,
                         suppressEmbeds ? ",\"flags\":4" : "");
  if (bodyLen < 0 || (size_t)bodyLen >= sizeof(body)) {
    MmLog.println(F("[REST] Discord body snprintf fail"));
    return false;
  }

  int reqLen = snprintf(request, sizeof(request),
                        "POST /api/v10/channels/%s/messages HTTP/1.1\r\n"
                        "Host: discord.com\r\n"
                        "Authorization: Bot %s\r\n"
                        "Content-Type: application/json\r\n"
                        "User-Agent: " MINIME_USER_AGENT "\r\n"
                        "Content-Length: %d\r\n"
                        "Connection: close\r\n\r\n"
                        "%s",
                        channelId, BOT_TOKEN, bodyLen, body);
  if (reqLen < 0 || (size_t)reqLen >= sizeof(request)) {
    MmLog.println(F("[REST] Discord request snprintf fail"));
    return false;
  }

  for (uint8_t attempt = 0; attempt < DISCORD_REST_MAX_ATTEMPTS; attempt++) {
    if ((millis() - startedAt) > 60000UL) {
      MmLog.println(F("[REST] Discord wall 60s"));
      return false;
    }
    if (!httpsAcquire("discord.com")) {
      MmLog.print(F("[REST] Discord connect fail attempt "));
      MmLog.print((unsigned)(attempt + 1));
      MmLog.print(F("/"));
      MmLog.println((unsigned)DISCORD_REST_MAX_ATTEMPTS);
      if (attempt + 1 >= DISCORD_REST_MAX_ATTEMPTS) return false;
      continue;
    }
    httpsClient.write((const uint8_t*)request, (size_t)reqLen);
    unsigned long deadline = millis() + 8000UL;
    char statusLine[160];
    statusLine[0] = '\0';
    bool chunked = false;
    int contentLength = -1;
    float retryAfterSec = -1.f;
    if (!httpsAwaitHeaders(httpsClient, deadline, true, statusLine, sizeof(statusLine), chunked,
                           contentLength, &retryAfterSec)) {
      httpsRelease();
      if ((millis() - startedAt) > 60000UL) return false;
      MmLog.print(F("[REST] Discord header timeout attempt "));
      MmLog.print((unsigned)(attempt + 1));
      MmLog.print(F("/"));
      MmLog.println((unsigned)DISCORD_REST_MAX_ATTEMPTS);
      if (attempt + 1 >= DISCORD_REST_MAX_ATTEMPTS) return false;
      unsigned long waitUntil = millis() + DISCORD_HEADER_RETRY_WAIT_MS;
      while ((long)(millis() - waitUntil) < 0) {
        if ((millis() - startedAt) > 60000UL) return false;
        pumpNetWait();
        delay(10);
      }
      continue;
    }
    int code = 0;
    const char* sp = strchr(statusLine, ' ');
    if (sp) code = atoi(sp + 1);

    if (code >= 200 && code < 300) {
      httpsRelease();
      return true;
    }

    if (code == 429) {
      if (retryAfterSec < 0.f) {
        char errBody[512];
        size_t errLen = 0;
        unsigned long bodyDeadline = millis() + 3000UL;
        if (readHttpBodyAfterHeaders(httpsClient, chunked, contentLength, errBody, sizeof(errBody),
                                     errLen, bodyDeadline)) {
          const char* idx = strstr(errBody, "\"retry_after\"");
          if (idx) {
            const char* colon = strchr(idx, ':');
            if (colon) retryAfterSec = (float)atof(colon + 1);
          }
        }
      }
      httpsRelease();
      if ((millis() - startedAt) > 60000UL) return false;

      unsigned long waitMs = DISCORD_429_WAIT_MIN_MS;
      if (retryAfterSec > 0.f) {
        waitMs = (unsigned long)(retryAfterSec * 1000.f + 0.5f);
      }
      if (waitMs < DISCORD_429_WAIT_MIN_MS) waitMs = DISCORD_429_WAIT_MIN_MS;
      if (waitMs > DISCORD_429_WAIT_MAX_MS) waitMs = DISCORD_429_WAIT_MAX_MS;
      {
        unsigned long elapsed = millis() - startedAt;
        unsigned long remain = (elapsed < 60000UL) ? (60000UL - elapsed) : 0UL;
        if (remain == 0UL) return false;
        if (waitMs > remain) waitMs = remain;
      }

      MmLog.print(F("[REST] Discord 429 attempt "));
      MmLog.print((unsigned)(attempt + 1));
      MmLog.print(F("/"));
      MmLog.print((unsigned)DISCORD_REST_MAX_ATTEMPTS);
      MmLog.print(F(" wait_ms="));
      MmLog.println((unsigned long)waitMs);

      unsigned long waitUntil = millis() + waitMs;
      while ((long)(millis() - waitUntil) < 0) {
        if ((millis() - startedAt) > 60000UL) return false;
        pumpNetWait();
        delay(10);
      }
      continue;
    }

    MmLog.print(F("[REST] Discord HTTP "));
    MmLog.print(code);
    MmLog.print(F(" status="));
    MmLog.println(statusLine[0] ? statusLine : "(empty)");
    httpsRelease();
    return false;
  }
  MmLog.println(F("[REST] Discord attempts exhausted"));
  return false;
}

bool sendDiscordMessage(const String& channelId, const String& content, bool suppressEmbeds) {
  return sendDiscordMessage(channelId.c_str(), content.c_str(), suppressEmbeds);
}

bool httpsConnect(const char* host, uint32_t timeoutMs) {
  httpsClient.stop();
  // Verify server certs. Never setInsecure -- BOT_TOKEN / API keys must not ride MITM TLS.
  // Call after every stop() -- NetworkClientSecure drops the attach callback on stop.
  // Arduino-ESP32 3.3.12+: useBuiltinCACertBundle() (IDF Mozilla bundle in the core).
  // Older 3.x (incl. many IDE installs): setCACertBundle with IDF-linked
  // _binary_x509_crt_bundle_* symbols + size (required since ~3.0.4).
  MmLog.print(F("[REST] connect "));
  MmLog.print(host ? host : "(null)");
  MmLog.print(F(" heap="));
  MmLog.print(ESP.getFreeHeap());
  MmLog.print(F(" maxAlloc="));
  MmLog.println(ESP.getMaxAllocHeap());
  if (host && host[0]) {
    IPAddress ip;
    if (WiFi.hostByName(host, ip)) {
      MmLog.print(F("[REST] DNS "));
      MmLog.print(host);
      MmLog.print(F(" -> "));
      MmLog.println(ip);
    } else {
      MmLog.print(F("[REST] DNS fail "));
      MmLog.println(host);
    }
  }
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
  gwYieldForTlsHeadroom();
  bool ok = httpsClient.connect(host, 443);
  if (!ok) {
    MmLog.print(F("[REST] TLS fail "));
    MmLog.print(host ? host : "(null)");
    MmLog.print(F(" heap="));
    MmLog.print(ESP.getFreeHeap());
    MmLog.print(F(" maxAlloc="));
    MmLog.println(ESP.getMaxAllocHeap());
  }
  return ok;
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
uint8_t httpsGetOpen(const char* host, const char* path, unsigned long headerTimeoutMs,
                     bool& outChunked, int& outContentLength,
                     const char* userAgent, const char* extraHeaders) {
  outChunked = false;
  outContentLength = -1;
  if (!host || !path) return 3;
  if (httpsInUse) return 1;
  if (!httpsAcquire(host)) return 3;
  if (!userAgent || !userAgent[0]) userAgent = MINIME_USER_AGENT;
  static char req[768];
  int n = snprintf(req, sizeof(req),
                   "GET %s HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "User-Agent: %s\r\n"
                   "%s"
                   "Connection: close\r\n\r\n",
                   path, host, userAgent,
                   (extraHeaders && extraHeaders[0]) ? extraHeaders : "");
  if (n < 0 || (size_t)n >= sizeof(req)) {
    httpsRelease();
    return 3;
  }
  httpsClient.write((const uint8_t*)req, (size_t)n);
  if (!httpSkipHeaders(httpsClient, headerTimeoutMs, outChunked, outContentLength)) {
    httpsRelease();
    return 2;
  }
  return 0;
}

uint8_t httpGetOpen(WiFiClient& client, const char* host, const char* path,
                    unsigned long headerTimeoutMs, bool& outChunked, int& outContentLength) {
  outChunked = false;
  outContentLength = -1;
  if (!host || !path) return 3;
  if (!client.connect(host, 80)) return 3;
  static char req[512];
  int n = snprintf(req, sizeof(req),
                   "GET %s HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "User-Agent: " MINIME_USER_AGENT "\r\n"
                   "Connection: close\r\n\r\n",
                   path, host);
  if (n < 0 || (size_t)n >= sizeof(req)) {
    client.stop();
    return 3;
  }
  client.write((const uint8_t*)req, (size_t)n);
  if (!httpSkipHeaders(client, headerTimeoutMs, outChunked, outContentLength)) {
    client.stop();
    return 2;
  }
  return 0;
}

void setHttpOpenError(char* outReport, size_t outCap, uint8_t err, const char* label) {
  if (!outReport || outCap == 0) return;
  const char* suffix = " connection failed.";
  if (err == 1) suffix = " busy.";
  else if (err == 2) suffix = " header timeout.";
  else if (err == 3) suffix = " connect/TLS failed.";
  snprintf(outReport, outCap, "%s%s", label ? label : "HTTP", suffix);
}

bool discordIdLooksValid(const char* id) {
  if (!id) return false;
  size_t n = strlen(id);
  if (n < 16) return false;
  for (size_t i = 0; i < n; i++) {
    char c = id[i];
    if (c < '0' || c > '9') return false;
  }
  return true;
}

bool discordIdLooksValid(const String& id) {
  return discordIdLooksValid(id.c_str());
}

bool discordRestGet(const char* path, char* outBody, size_t bodyCap, size_t& outLen,
                    char* outStatus, size_t statusCap) {
  outLen = 0;
  if (outBody && bodyCap) outBody[0] = '\0';
  if (outStatus && statusCap) outStatus[0] = '\0';
  if (!path || !outBody || bodyCap < 2) return false;
  if (!httpsAcquire("discord.com", 15000)) {
    if (outStatus && statusCap) {
      strncpy(outStatus, httpsInUse ? "busy" : "connect failed", statusCap - 1);
      outStatus[statusCap - 1] = '\0';
    }
    return false;
  }
  static char request[512];
  int reqLen = snprintf(request, sizeof(request),
                        "GET %s HTTP/1.1\r\n"
                        "Host: discord.com\r\n"
                        "Authorization: Bot %s\r\n"
                        "Accept: application/json\r\n"
                        "Accept-Encoding: identity\r\n"
                        "User-Agent: " MINIME_USER_AGENT "\r\n"
                        "Connection: close\r\n\r\n",
                        path, BOT_TOKEN);
  if (reqLen < 0 || (size_t)reqLen >= sizeof(request)) {
    httpsRelease();
    if (outStatus && statusCap) {
      strncpy(outStatus, "request overflow", statusCap - 1);
      outStatus[statusCap - 1] = '\0';
    }
    return false;
  }
  httpsClient.write((const uint8_t*)request, (size_t)reqLen);

  unsigned long deadline = millis() + 15000UL;
  bool chunked = false;
  int contentLength = -1;
  char statusBuf[160];
  char* statusPtr = outStatus && statusCap ? outStatus : statusBuf;
  size_t statusSz = outStatus && statusCap ? statusCap : sizeof(statusBuf);
  if (!httpsAwaitHeaders(httpsClient, deadline, false, statusPtr, statusSz, chunked, contentLength)) {
    httpsRelease();
    if (outStatus && statusCap) {
      strncpy(outStatus, "timeout", statusCap - 1);
      outStatus[statusCap - 1] = '\0';
    }
    return false;
  }
  bool ok = readHttpBodyAfterHeaders(httpsClient, chunked, contentLength, outBody, bodyCap, outLen,
                                     deadline);
  httpsRelease();
  return ok;
}

bool guildIdFromChannel(const char* channelId, char* outGid, size_t gidCap) {
  if (outGid && gidCap) outGid[0] = '\0';
  if (!discordIdLooksValid(channelId) || !outGid || gidCap < 2) return false;

  char path[80];
  snprintf(path, sizeof(path), "/api/v10/channels/%s", channelId);
  static char body[2048];
  char status[80];
  size_t bodyLen = 0;
  if (!discordRestGet(path, body, sizeof(body), bodyLen, status, sizeof(status))) {
    return false;
  }
  const char* jsonStart = (const char*)memchr(body, '{', bodyLen);
  if (!jsonStart) return false;

  JsonDocument filter;
  filter["guild_id"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, jsonStart, DeserializationOption::Filter(filter));
  if (err) return false;
  const char* gid = doc["guild_id"] | "";
  if (!gid[0]) return false;
  strncpy(outGid, gid, gidCap - 1);
  outGid[gidCap - 1] = '\0';
  return true;
}

bool appendMembersFromGuild(const char* guildId, uint8_t maxToAdd) {
  if (!discordIdLooksValid(guildId)) return false;

  char path[96];
  snprintf(path, sizeof(path), "/api/v10/guilds/%s/members?limit=200", guildId);
  static char body[24576];
  char status[80];
  size_t bodyLen = 0;
  if (!discordRestGet(path, body, sizeof(body), bodyLen, status, sizeof(status))) {
    return false;
  }

  const char* jsonStart = (const char*)memchr(body, '[', bodyLen);
  const char* objStart = (const char*)memchr(body, '{', bodyLen);
  if (!jsonStart || (objStart && objStart < jsonStart)) {
    return false;
  }

  JsonDocument filter;
  filter[0]["nick"] = true;
  filter[0]["user"]["id"] = true;
  filter[0]["user"]["username"] = true;
  filter[0]["user"]["global_name"] = true;
  filter[0]["user"]["bot"] = true;

  static JsonDocument* memberDoc = nullptr;
  if (!memberDoc) {
    memberDoc = new (std::nothrow) JsonDocument();
  }
  if (!memberDoc) return false;
  memberDoc->clear();
  DeserializationError err =
      deserializeJson(*memberDoc, jsonStart, DeserializationOption::Filter(filter));
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
    const char* uid = member["user"]["id"] | "";
    const char* nick = member["nick"] | "";
    char nameBuf[48];
    nameBuf[0] = '\0';
    if (nick[0]) {
      strncpy(nameBuf, nick, sizeof(nameBuf) - 1);
      nameBuf[sizeof(nameBuf) - 1] = '\0';
    } else {
      String dn = discordDisplayName(member["user"]);
      strncpy(nameBuf, dn.c_str(), sizeof(nameBuf) - 1);
      nameBuf[sizeof(nameBuf) - 1] = '\0';
    }
    if (!uid[0] || !nameBuf[0]) continue;
    if (findUserIndex(uid) >= 0) continue;
    fillTrackedSlot((uint8_t)slot, uid, nameBuf);
    added++;
  }

  return added > 0;
}

bool fetchGuildMembersAtStartup() {
  initTrackedUsers();
  cachedGuildCount = 0;
  rememberGuildId(String(BOT_GUILD_ID));
  char gid[DISCORD_SNOWFLAKE_MAX];
  if (guildIdFromChannel(TARGET_CHANNEL_ID, gid, sizeof(gid))) rememberGuildId(String(gid));
  if (guildIdFromChannel(TARGET_CHANNEL_ID1, gid, sizeof(gid))) rememberGuildId(String(gid));

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
