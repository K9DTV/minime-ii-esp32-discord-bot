#include "minime.h"
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// External API fetchers + DeepSeek (Core 1). Dispatch stays in commands.cpp.
// Cold path: fixed body/report buffers -- no String growth for HTTPS bodies or Discord replies.

static JsonDocument* deepSeekDoc = nullptr;

// Shared fetch body (Core 1 sequential -- not concurrent with another fetch).
static char gFetchBody[HTTP_FETCH_BODY_MAX];

static bool reportSet(char* out, size_t cap, const char* msg) {
  if (!out || cap == 0) return false;
  strncpy(out, msg ? msg : "", cap - 1);
  out[cap - 1] = '\0';
  return true;
}

static bool reportPrintf(char* out, size_t cap, size_t& len, const char* fmt, ...) {
  if (!out || cap == 0 || len >= cap) return false;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(out + len, cap - len, fmt, ap);
  va_end(ap);
  if (n < 0) return false;
  if ((size_t)n >= cap - len) {
    len = cap - 1;
    out[len] = '\0';
    return false;
  }
  len += (size_t)n;
  return true;
}

void collapseWhitespaceBuf(char* s) {
  if (!s) return;
  // Newlines/tabs -> space
  for (char* p = s; *p; p++) {
    if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
  }
  // Collapse runs of spaces + trim
  char* w = s;
  bool space = true; // leading trim
  for (const char* r = s; *r; r++) {
    if (*r == ' ') {
      if (space) continue;
      space = true;
      *w++ = ' ';
    } else {
      space = false;
      *w++ = *r;
    }
  }
  while (w > s && w[-1] == ' ') w--;
  *w = '\0';
}

void truncateTextBuf(char* s, size_t maxLen) {
  if (!s) return;
  size_t n = strlen(s);
  if (n <= maxLen) return;
  if (maxLen < 3) {
    s[maxLen] = '\0';
    return;
  }
  s[maxLen - 3] = '.';
  s[maxLen - 2] = '.';
  s[maxLen - 1] = '.';
  s[maxLen] = '\0';
}

String collapseWhitespace(String s) {
  char buf[512];
  size_t n = s.length();
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  memcpy(buf, s.c_str(), n);
  buf[n] = '\0';
  collapseWhitespaceBuf(buf);
  return String(buf);
}

String truncateText(const String& s, int maxLen) {
  if (maxLen <= 0) return "";
  if ((int)s.length() <= maxLen) return s;
  char buf[512];
  size_t n = s.length();
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  memcpy(buf, s.c_str(), n);
  buf[n] = '\0';
  truncateTextBuf(buf, (size_t)maxLen);
  return String(buf);
}

static bool readOpenBodyPumped(Client& client, bool chunked, int contentLength,
                               char* outBuf, size_t outCap, size_t& outLen,
                               unsigned long timeoutMs) {
  return readHttpBodyAfterHeaders(client, chunked, contentLength, outBuf, outCap, outLen,
                                  millis() + timeoutMs);
}

bool getWeather(const char* zip, char* outReport, size_t outCap) {
  if (!zip || !outReport || outCap == 0) return false;
  WiFiClient client;
  char path[192];
  snprintf(path, sizeof(path),
           "/data/2.5/weather?zip=%s,US&units=imperial&appid=%s", zip, WEATHER_API_KEY);
  bool chunked = false;
  int contentLength = -1;
  uint8_t openErr = httpGetOpen(client, "api.openweathermap.org", path, 5000, chunked, contentLength);
  if (openErr) {
    setHttpOpenError(outReport, outCap, openErr, "Weather service");
    return false;
  }
  size_t bodyLen = 0;
  if (!readOpenBodyPumped(client, chunked, contentLength, gFetchBody, sizeof(gFetchBody), bodyLen,
                          5000UL)) {
    client.stop();
    return reportSet(outReport, outCap, "Weather empty response."), false;
  }
  client.stop();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, gFetchBody, bodyLen);
  if (err) {
    return reportSet(outReport, outCap, "Weather JSON parse error."), false;
  }
  const char* city = doc["name"] | "Unknown";
  float tempF = doc["main"]["temp"] | 0.0f;
  float tempC = (tempF - 32.0f) * 5.0f / 9.0f;
  int humidity = doc["main"]["humidity"] | 0;
  const char* cond = doc["weather"][0]["description"] | "Unknown";
  size_t len = 0;
  outReport[0] = '\0';
  reportPrintf(outReport, outCap, len,
               "☁️ **Weather Report (%s - %s):**\n"
               "- **Condition:** %s\n"
               "- **Temperature:** %.1f degF (%.1f degC)\n"
               "- **Humidity:** %d%%",
               city, zip, cond, tempF, tempC, humidity);
  return true;
}

bool getScienceNews(char* outReport, size_t outCap) {
  if (!outReport || outCap == 0) return false;
  bool chunked = false;
  int contentLength = -1;
  uint8_t openErr = httpsGetOpen("api.spaceflightnewsapi.net", "/v4/articles/?limit=3", 8000,
                                 chunked, contentLength);
  if (openErr) {
    setHttpOpenError(outReport, outCap, openErr, "Science news");
    return false;
  }
  size_t bodyLen = 0;
  if (!readOpenBodyPumped(httpsClient, chunked, contentLength, gFetchBody, sizeof(gFetchBody),
                          bodyLen, 8000UL)) {
    httpsRelease();
    return reportSet(outReport, outCap, "Science news empty response."), false;
  }
  httpsRelease();
  JsonDocument filter;
  filter["results"][0]["title"] = true;
  filter["results"][0]["news_site"] = true;
  filter["results"][0]["url"] = true;
  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, gFetchBody, bodyLen, DeserializationOption::Filter(filter));
  if (err) {
    return reportSet(outReport, outCap, "Science news JSON parse error."), false;
  }
  JsonArray results = doc["results"].as<JsonArray>();
  if (results.isNull() || results.size() == 0) {
    return reportSet(outReport, outCap, "No science headlines right now."), false;
  }
  size_t len = 0;
  outReport[0] = '\0';
  reportPrintf(outReport, outCap, len, "🛰️ **Space & high-tech headlines:**\n");
  int n = 0;
  for (JsonObject item : results) {
    if (n >= 3) break;
    char title[160];
    char site[64];
    char url[256];
    strncpy(title, item["title"] | "Untitled", sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    collapseWhitespaceBuf(title);
    truncateTextBuf(title, 140);
    strncpy(site, item["news_site"] | "Source", sizeof(site) - 1);
    site[sizeof(site) - 1] = '\0';
    strncpy(url, item["url"] | "", sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    reportPrintf(outReport, outCap, len, "%d. **%s** (%s)", n + 1, title, site);
    if (url[0]) reportPrintf(outReport, outCap, len, "\n%s", url);
    reportPrintf(outReport, outCap, len, "\n");
    n++;
  }
  return n > 0;
}

bool getPhysicsPapers(char* outReport, size_t outCap) {
  if (!outReport || outCap == 0) return false;
  const char* path =
      "/api/query?search_query=cat:physics.*&start=0&max_results=3&sortBy=submittedDate&sortOrder=descending";
  bool chunked = false;
  int contentLength = -1;
  uint8_t openErr = httpsGetOpen("export.arxiv.org", path, 8000, chunked, contentLength,
                                 "MiniMeBot/1.0 (ESP32 Discord bot)", "Accept-Encoding: identity\r\n");
  if (openErr) {
    setHttpOpenError(outReport, outCap, openErr, "arXiv");
    return false;
  }
  size_t bodyLen = 0;
  if (!readOpenBodyPumped(httpsClient, chunked, contentLength, gFetchBody, sizeof(gFetchBody),
                          bodyLen, 8000UL)) {
    httpsRelease();
    return reportSet(outReport, outCap, "arXiv response empty."), false;
  }
  httpsRelease();
  if (bodyLen > sizeof(gFetchBody) - 1) bodyLen = sizeof(gFetchBody) - 1;
  gFetchBody[bodyLen] = '\0';
  if (bodyLen < 50) {
    return reportSet(outReport, outCap, "arXiv response empty."), false;
  }
  size_t len = 0;
  outReport[0] = '\0';
  reportPrintf(outReport, outCap, len, "⚛️ **Latest arXiv physics papers:**\n");
  const char* from = gFetchBody;
  int n = 0;
  while (n < 3) {
    const char* entry = strstr(from, "<entry>");
    if (!entry) break;
    const char* entryEnd = strstr(entry, "</entry>");
    if (!entryEnd) break;
    char title[160];
    char id[160];
    title[0] = '\0';
    id[0] = '\0';
    const char* t0 = strstr(entry, "<title>");
    const char* t1 = t0 ? strstr(t0, "</title>") : nullptr;
    if (t0 && t1 && t0 < entryEnd && t1 < entryEnd) {
      t0 += 7;
      size_t tn = (size_t)(t1 - t0);
      if (tn >= sizeof(title)) tn = sizeof(title) - 1;
      memcpy(title, t0, tn);
      title[tn] = '\0';
      collapseWhitespaceBuf(title);
      truncateTextBuf(title, 140);
    } else {
      strncpy(title, "Untitled", sizeof(title) - 1);
    }
    const char* i0 = strstr(entry, "<id>");
    const char* i1 = i0 ? strstr(i0, "</id>") : nullptr;
    if (i0 && i1 && i0 < entryEnd && i1 < entryEnd) {
      i0 += 4;
      size_t in = (size_t)(i1 - i0);
      if (in >= sizeof(id)) in = sizeof(id) - 1;
      memcpy(id, i0, in);
      id[in] = '\0';
      collapseWhitespaceBuf(id);
    }
    reportPrintf(outReport, outCap, len, "%d. **%s**", n + 1, title[0] ? title : "Untitled");
    if (id[0]) reportPrintf(outReport, outCap, len, "\n%s", id);
    reportPrintf(outReport, outCap, len, "\n");
    n++;
    from = entryEnd + 8;
  }
  if (n == 0) {
    return reportSet(outReport, outCap, "No physics papers found."), false;
  }
  return true;
}

bool getApod(char* outReport, size_t outCap) {
  if (!outReport || outCap == 0) return false;
  char path[96];
  snprintf(path, sizeof(path), "/planetary/apod?api_key=%s", NASA_API_KEY);
  bool chunked = false;
  int contentLength = -1;
  uint8_t openErr = httpsGetOpen("api.nasa.gov", path, 8000, chunked, contentLength);
  if (openErr) {
    setHttpOpenError(outReport, outCap, openErr, "NASA APOD");
    return false;
  }
  size_t bodyLen = 0;
  if (!readOpenBodyPumped(httpsClient, chunked, contentLength, gFetchBody, sizeof(gFetchBody),
                          bodyLen, 8000UL)) {
    httpsRelease();
    return reportSet(outReport, outCap, "NASA APOD empty response."), false;
  }
  httpsRelease();
  JsonDocument filter;
  filter["title"] = true;
  filter["explanation"] = true;
  filter["date"] = true;
  filter["url"] = true;
  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, gFetchBody, bodyLen, DeserializationOption::Filter(filter));
  if (err) {
    return reportSet(outReport, outCap, "NASA APOD JSON parse error."), false;
  }
  const char* title = doc["title"] | "Astronomy Picture of the Day";
  const char* date = doc["date"] | "";
  char expl[400];
  strncpy(expl, doc["explanation"] | "", sizeof(expl) - 1);
  expl[sizeof(expl) - 1] = '\0';
  collapseWhitespaceBuf(expl);
  truncateTextBuf(expl, 350);
  const char* url = doc["url"] | "";
  size_t len = 0;
  outReport[0] = '\0';
  if (date[0]) {
    reportPrintf(outReport, outCap, len, "🌌 **NASA APOD (%s):**\n- **%s**\n%s", date, title, expl);
  } else {
    reportPrintf(outReport, outCap, len, "🌌 **NASA APOD:**\n- **%s**\n%s", title, expl);
  }
  if (url[0]) reportPrintf(outReport, outCap, len, "\n%s", url);
  return true;
}

bool getIssPosition(char* outReport, size_t outCap) {
  if (!outReport || outCap == 0) return false;
  WiFiClient client;
  bool chunked = false;
  int contentLength = -1;
  uint8_t openErr =
      httpGetOpen(client, "api.open-notify.org", "/iss-now.json", 5000, chunked, contentLength);
  if (openErr) {
    setHttpOpenError(outReport, outCap, openErr, "ISS tracker");
    return false;
  }
  size_t bodyLen = 0;
  if (!readOpenBodyPumped(client, chunked, contentLength, gFetchBody, sizeof(gFetchBody), bodyLen,
                          5000UL)) {
    client.stop();
    return reportSet(outReport, outCap, "ISS tracker empty response."), false;
  }
  client.stop();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, gFetchBody, bodyLen);
  if (err) {
    return reportSet(outReport, outCap, "ISS tracker JSON parse error."), false;
  }
  const char* lat = doc["iss_position"]["latitude"] | "?";
  const char* lon = doc["iss_position"]["longitude"] | "?";
  size_t len = 0;
  outReport[0] = '\0';
  reportPrintf(outReport, outCap, len,
               "🌍 **ISS now:**\n- **Latitude:** %s\n- **Longitude:** %s", lat, lon);
  return true;
}

bool askDeepSeek(const char* question, char* outReport, size_t outCap) {
  if (!outReport || outCap == 0) return false;
  if (strlen(DEEPSEEK_API_KEY) == 0 ||
      strcmp(DEEPSEEK_API_KEY, "DEEPSEEK_API_KEY") == 0) {
    return reportSet(outReport, outCap,
                     "DeepSeek API key not set. Add DEEPSEEK_API_KEY in secrets.h."),
           false;
  }

  char q[ASK_QUESTION_MAX + 1];
  size_t qn = question ? strlen(question) : 0;
  if (qn > ASK_QUESTION_MAX) qn = ASK_QUESTION_MAX;
  if (qn) memcpy(q, question, qn);
  q[qn] = '\0';
  while (qn > 0 && (q[qn - 1] == ' ' || q[qn - 1] == '\t' || q[qn - 1] == '\r' || q[qn - 1] == '\n')) {
    q[--qn] = '\0';
  }
  size_t qs = 0;
  while (q[qs] == ' ' || q[qs] == '\t' || q[qs] == '\r' || q[qs] == '\n') qs++;
  if (qs > 0) {
    size_t rem = qn - qs;
    memmove(q, q + qs, rem + 1);
    qn = rem;
  }
  if (qn == 0) {
    return reportSet(outReport, outCap, "Usage: !ask <question>"), false;
  }

  JsonDocument req;
  req["model"] = "deepseek-chat";
  req["max_tokens"] = DEEPSEEK_MAX_TOKENS;
  req["temperature"] = 0.7;
  req["stream"] = false;
  JsonArray messages = req["messages"].to<JsonArray>();
  JsonObject sys = messages.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] =
      "You are MiniMe on an ESP32 Discord bot. Answer clearly for science, tech, and physics. "
      "Keep the full answer under 2000 characters so it fits one Discord message.";
  JsonObject user = messages.add<JsonObject>();
  user["role"] = "user";
  user["content"] = q;

  static char body[3072];
  static char request[3584];
  static char respBuf[8192];
  size_t bodyLen = serializeJson(req, body, sizeof(body));
  if (bodyLen == 0 || bodyLen >= sizeof(body)) {
    return reportSet(outReport, outCap, "DeepSeek: request too large."), false;
  }

  static WiFiClientSecure deepSeekTls;
  deepSeekTls.stop();
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 12))
  deepSeekTls.useBuiltinCACertBundle();
#else
  extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
  extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
  deepSeekTls.setCACertBundle(rootca_crt_bundle_start,
                              (size_t)(rootca_crt_bundle_end - rootca_crt_bundle_start));
#endif
  deepSeekTls.setTimeout(25000);
  deepSeekTls.setHandshakeTimeout(15);
  if (!deepSeekTls.connect("api.deepseek.com", 443)) {
    deepSeekTls.stop();
    return reportSet(outReport, outCap, "DeepSeek connection failed."), false;
  }
  int reqLen = snprintf(request, sizeof(request),
                        "POST /chat/completions HTTP/1.1\r\n"
                        "Host: api.deepseek.com\r\n"
                        "Authorization: Bearer %s\r\n"
                        "Content-Type: application/json\r\n"
                        "Accept: application/json\r\n"
                        "Accept-Encoding: identity\r\n"
                        "User-Agent: " MINIME_USER_AGENT "\r\n"
                        "Content-Length: %u\r\n"
                        "Connection: close\r\n\r\n"
                        "%s",
                        DEEPSEEK_API_KEY, (unsigned)bodyLen, body);
  if (reqLen < 0 || (size_t)reqLen >= sizeof(request)) {
    deepSeekTls.stop();
    return reportSet(outReport, outCap, "DeepSeek: request buffer overflow."), false;
  }
  deepSeekTls.write((const uint8_t*)request, (size_t)reqLen);
  unsigned long deadline = millis() + 30000UL;
  char statusLine[160];
  statusLine[0] = '\0';
  bool chunked = false;
  int contentLength = -1;
  if (!httpsAwaitHeaders(deepSeekTls, deadline, true, statusLine, sizeof(statusLine), chunked,
                         contentLength)) {
    deepSeekTls.stop();
    return reportSet(outReport, outCap, "DeepSeek timeout waiting for headers."), false;
  }
  size_t respLen = 0;
  if (!readHttpBodyAfterHeaders(deepSeekTls, chunked, contentLength, respBuf, sizeof(respBuf),
                                respLen, deadline)) {
    deepSeekTls.stop();
    size_t len = 0;
    outReport[0] = '\0';
    reportPrintf(outReport, outCap, len, "DeepSeek empty response. %s", statusLine);
    return false;
  }
  deepSeekTls.stop();
  const char* jsonPtr = (const char*)memchr(respBuf, '{', respLen);
  if (!jsonPtr) {
    char st[84];
    strncpy(st, statusLine, sizeof(st) - 1);
    st[sizeof(st) - 1] = '\0';
    truncateTextBuf(st, 80);
    size_t len = 0;
    outReport[0] = '\0';
    reportPrintf(outReport, outCap, len, "DeepSeek: no JSON body. %s", st);
    return false;
  }
  JsonDocument filter;
  filter["choices"][0]["message"]["content"] = true;
  filter["error"]["message"] = true;

  if (!deepSeekDoc) {
    deepSeekDoc = newSpiRamJsonDoc();
  }
  if (!deepSeekDoc) {
    return reportSet(outReport, outCap, "DeepSeek: out of memory (JSON doc)."), false;
  }
  deepSeekDoc->clear();
  DeserializationError err =
      deserializeJson(*deepSeekDoc, jsonPtr, DeserializationOption::Filter(filter));
  if (err) {
    size_t len = 0;
    outReport[0] = '\0';
    reportPrintf(outReport, outCap, len, "DeepSeek JSON parse error (%s).", err.c_str());
    return false;
  }
  if (!(*deepSeekDoc)["error"].isNull()) {
    char emsg[220];
    strncpy(emsg, (*deepSeekDoc)["error"]["message"] | "API error", sizeof(emsg) - 1);
    emsg[sizeof(emsg) - 1] = '\0';
    truncateTextBuf(emsg, 200);
    size_t len = 0;
    outReport[0] = '\0';
    reportPrintf(outReport, outCap, len, "DeepSeek error: %s", emsg);
    return false;
  }
  char answer[DISCORD_CONTENT_MAX + 1];
  strncpy(answer, (*deepSeekDoc)["choices"][0]["message"]["content"] | "", sizeof(answer) - 1);
  answer[sizeof(answer) - 1] = '\0';
  collapseWhitespaceBuf(answer);
  if (answer[0] == '\0') {
    char st[64];
    strncpy(st, statusLine, sizeof(st) - 1);
    st[sizeof(st) - 1] = '\0';
    truncateTextBuf(st, 60);
    size_t len = 0;
    outReport[0] = '\0';
    reportPrintf(outReport, outCap, len, "DeepSeek returned an empty answer. %s", st);
    return false;
  }
  const char* prefix = "🧠 **DeepSeek:**\n";
  int room = DISCORD_CONTENT_MAX - (int)strlen(prefix);
  if (room < 100) room = 100;
  truncateTextBuf(answer, (size_t)room);
  size_t len = 0;
  outReport[0] = '\0';
  reportPrintf(outReport, outCap, len, "%s%s", prefix, answer);
  return true;
}

void runAskFromLoop() {
  if (!askNeedPost) return;
  askNeedPost = false;
  char channelId[DISCORD_SNOWFLAKE_MAX];
  strncpy(channelId, askPendingChannelId, sizeof(channelId) - 1);
  channelId[sizeof(channelId) - 1] = '\0';
  char report[CMD_REPORT_MAX];
  report[0] = '\0';
  bool ok = askDeepSeek(askPendingQuestion, report, sizeof(report));
  askPendingQuestion[0] = '\0';
  askPendingChannelId[0] = '\0';
  if (ok) {
    if (!sendDiscordMessage(channelId, report)) {
      noteCmdErrorReply("Post fail: DeepSeek");
      const char* fallback =
          "DeepSeek answered, but Discord rejected the post (try a shorter question).";
      if (!sendDiscordCmdError(channelId, fallback)) {
        showTransient("DeepSeek", "Post fail");
        return;
      }
    }
    showTransient("DeepSeek", "Sent");
  } else {
    if (!sendDiscordCmdError(channelId, report)) {
      truncateTextBuf(report, (size_t)DISCORD_CONTENT_MAX);
      if (!sendDiscordCmdError(channelId, report)) {
        showTransient("DeepSeek", "Post fail");
        return;
      }
    }
    showTransient("DeepSeek", "Error");
  }
}
