#include "minime.h"
#include <new>

// External API fetchers + DeepSeek (Core 1). Dispatch stays in commands.cpp.

static JsonDocument* deepSeekDoc = nullptr;

// Body read with Gateway HB pumps (HTTP or HTTPS Client& -- both run on Core 1).
// empty body => false (readHttpBodyAfterHeaders); these APIs never return empty on success.
static bool readOpenBodyPumped(Client& client, bool chunked, int contentLength,
                               String& outBody, unsigned long timeoutMs) {
  return readHttpBodyAfterHeaders(client, chunked, contentLength, outBody, millis() + timeoutMs);
}

bool getWeather(const String& zip, String& outReport) {
  WiFiClient client;
  String url = "/data/2.5/weather?zip=" + zip + ",US&units=imperial&appid=" + WEATHER_API_KEY;
  bool chunked = false;
  int contentLength = -1;
  uint8_t openErr = httpGetOpen(client, "api.openweathermap.org", url, 5000, chunked, contentLength);
  if (openErr) {
    setHttpOpenError(outReport, openErr, "Weather service");
    return false;
  }
  String body;
  if (!readOpenBodyPumped(client, chunked, contentLength, body, 5000UL)) {
    client.stop();
    outReport = "Weather empty response.";
    return false;
  }
  client.stop();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    outReport = "Weather JSON parse error.";
    return false;
  }
  String city = doc["name"] | "Unknown";
  float tempF = doc["main"]["temp"] | 0.0f;
  float tempC = (tempF - 32.0f) * 5.0f / 9.0f;
  int humidity = doc["main"]["humidity"] | 0;
  String cond = doc["weather"][0]["description"] | "Unknown";
  outReport = "☁️ **Weather Report (" + city + " - " + zip + "):**\n" +
              "- **Condition:** " + cond + "\n" +
              "- **Temperature:** " + String(tempF, 1) + " degF (" + String(tempC, 1) + " degC)\n" +
              "- **Humidity:** " + String(humidity) + "%";
  return true;
}

bool getScienceNews(String& outReport) {
  bool chunked = false;
  int contentLength = -1;
  uint8_t openErr = httpsGetOpen("api.spaceflightnewsapi.net", "/v4/articles/?limit=3", 8000,
                                 chunked, contentLength);
  if (openErr) {
    setHttpOpenError(outReport, openErr, "Science news");
    return false;
  }
  String body;
  if (!readOpenBodyPumped(httpsClient, chunked, contentLength, body, 8000UL)) {
    httpsRelease();
    outReport = "Science news empty response.";
    return false;
  }
  httpsRelease();
  // SNAPI v4: { "count", "next", "results": [ { title, news_site, url, ... } ] }
  JsonDocument filter;
  filter["results"][0]["title"] = true;
  filter["results"][0]["news_site"] = true;
  filter["results"][0]["url"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    outReport = "Science news JSON parse error.";
    return false;
  }
  JsonArray results = doc["results"].as<JsonArray>();
  if (results.isNull() || results.size() == 0) {
    outReport = "No science headlines right now.";
    return false;
  }
  outReport = "🛰️ **Space & high-tech headlines:**\n";
  int n = 0;
  for (JsonObject item : results) {
    if (n >= 3) break;
    String title = collapseWhitespace(item["title"] | "Untitled");
    String site = item["news_site"] | "Source";
    String url = item["url"] | "";
    outReport += String(n + 1) + ". **" + truncateText(title, 140) + "** (" + site + ")";
    if (url.length()) outReport += "\n" + url;
    outReport += "\n";
    n++;
  }
  return n > 0;
}

bool getPhysicsPapers(String& outReport) {
  const char* path =
    "/api/query?search_query=cat:physics.*&start=0&max_results=3&sortBy=submittedDate&sortOrder=descending";
  bool chunked = false;
  int contentLength = -1;
  uint8_t openErr = httpsGetOpen("export.arxiv.org", path, 8000, chunked, contentLength,
                                 "MiniMeBot/1.0 (ESP32 Discord bot)", "Accept-Encoding: identity\r\n");
  if (openErr) {
    setHttpOpenError(outReport, openErr, "arXiv");
    return false;
  }
  String xml;
  if (!readOpenBodyPumped(httpsClient, chunked, contentLength, xml, 8000UL)) {
    httpsRelease();
    outReport = "arXiv response empty.";
    return false;
  }
  httpsRelease();
  // Soft 24 KB parse cap (readHttpBodyAfterHeaders hard-caps at 48 KB).
  if (xml.length() > 24000) xml = xml.substring(0, 24000);
  if (xml.length() < 50) {
    outReport = "arXiv response empty.";
    return false;
  }
  outReport = "⚛️ **Latest arXiv physics papers:**\n";
  int from = 0;
  int n = 0;
  while (n < 3) {
    int entry = xml.indexOf("<entry>", from);
    if (entry < 0) break;
    int entryEnd = xml.indexOf("</entry>", entry);
    if (entryEnd < 0) break;
    String block = xml.substring(entry, entryEnd);
    int t0 = block.indexOf("<title>");
    int t1 = block.indexOf("</title>");
    String title = "Untitled";
    if (t0 >= 0 && t1 > t0) {
      title = collapseWhitespace(block.substring(t0 + 7, t1));
    }
    int i0 = block.indexOf("<id>");
    int i1 = block.indexOf("</id>");
    String id = "";
    if (i0 >= 0 && i1 > i0) {
      id = collapseWhitespace(block.substring(i0 + 4, i1));
    }
    outReport += String(n + 1) + ". **" + truncateText(title, 140) + "**";
    if (id.length()) outReport += "\n" + id;
    outReport += "\n";
    n++;
    from = entryEnd + 8;
  }
  if (n == 0) {
    outReport = "No physics papers found.";
    return false;
  }
  return true;
}

bool getApod(String& outReport) {
  String path = String("/planetary/apod?api_key=") + NASA_API_KEY;
  bool chunked = false;
  int contentLength = -1;
  uint8_t openErr = httpsGetOpen("api.nasa.gov", path, 8000, chunked, contentLength);
  if (openErr) {
    setHttpOpenError(outReport, openErr, "NASA APOD");
    return false;
  }
  String body;
  if (!readOpenBodyPumped(httpsClient, chunked, contentLength, body, 8000UL)) {
    httpsRelease();
    outReport = "NASA APOD empty response.";
    return false;
  }
  httpsRelease();
  JsonDocument filter;
  filter["title"] = true;
  filter["explanation"] = true;
  filter["date"] = true;
  filter["url"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    outReport = "NASA APOD JSON parse error.";
    return false;
  }
  String title = doc["title"] | "Astronomy Picture of the Day";
  String date = doc["date"] | "";
  String expl = collapseWhitespace(doc["explanation"] | "");
  String url = doc["url"] | "";
  outReport = "🌌 **NASA APOD";
  if (date.length()) outReport += " (" + date + ")";
  outReport += ":**\n- **" + title + "**\n" + truncateText(expl, 350);
  if (url.length()) outReport += "\n" + url;
  return true;
}

bool getIssPosition(String& outReport) {
  WiFiClient client;
  bool chunked = false;
  int contentLength = -1;
  uint8_t openErr = httpGetOpen(client, "api.open-notify.org", "/iss-now.json", 5000,
                                chunked, contentLength);
  if (openErr) {
    setHttpOpenError(outReport, openErr, "ISS tracker");
    return false;
  }
  String body;
  if (!readOpenBodyPumped(client, chunked, contentLength, body, 5000UL)) {
    client.stop();
    outReport = "ISS tracker empty response.";
    return false;
  }
  client.stop();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    outReport = "ISS tracker JSON parse error.";
    return false;
  }
  String lat = doc["iss_position"]["latitude"] | "?";
  String lon = doc["iss_position"]["longitude"] | "?";
  outReport = "🌍 **ISS now:**\n"
              "- **Latitude:** " + lat + "\n"
              "- **Longitude:** " + lon;
  return true;
}

bool askDeepSeek(const String& question, String& outReport) {
  if (strlen(DEEPSEEK_API_KEY) == 0 ||
      strcmp(DEEPSEEK_API_KEY, "DEEPSEEK_API_KEY") == 0) {
    outReport = "DeepSeek API key not set. Add DEEPSEEK_API_KEY in SD /secrets.h.";
    return false;
  }

  // Trim/cap question into a fixed buffer (no String heap churn).
  char q[501];
  size_t qn = question.length();
  if (qn > 500) qn = 500;
  memcpy(q, question.c_str(), qn);
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
    outReport = "Usage: !ask <question>";
    return false;
  }

  JsonDocument req;
  req["model"] = "deepseek-chat";
  req["max_tokens"] = DEEPSEEK_MAX_TOKENS;
  req["temperature"] = 0.7;
  req["stream"] = false;
  JsonArray messages = req["messages"].to<JsonArray>();
  JsonObject sys = messages.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] = "You are MiniMe on an ESP32 Discord bot. Answer clearly for science, tech, and physics. Keep the full answer under 2000 characters so it fits one Discord message.";
  JsonObject user = messages.add<JsonObject>();
  user["role"] = "user";
  user["content"] = q;

  // Fixed .bss buffers -- Core 1 only, not concurrent with another !ask.
  static char body[3072];
  static char request[3584];
  static char respBuf[8192];
  size_t bodyLen = serializeJson(req, body, sizeof(body));
  if (bodyLen == 0 || bodyLen >= sizeof(body)) {
    outReport = "DeepSeek: request too large.";
    return false;
  }

  // Dedicated TLS -- leaves httpsInUse free so Discord REST / !weather can drain during wait.
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
    outReport = "DeepSeek connection failed.";
    return false;
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
    outReport = "DeepSeek: request buffer overflow.";
    return false;
  }
  deepSeekTls.write((const uint8_t*)request, (size_t)reqLen);
  unsigned long deadline = millis() + 30000UL;
  String statusLine;
  bool chunked = false;
  int contentLength = -1;
  if (!httpsAwaitHeaders(deepSeekTls, deadline, true, statusLine, chunked, contentLength)) {
    deepSeekTls.stop();
    outReport = "DeepSeek timeout waiting for headers.";
    return false;
  }
  size_t respLen = 0;
  if (!readHttpBodyAfterHeaders(deepSeekTls, chunked, contentLength, respBuf, sizeof(respBuf),
                                respLen, deadline)) {
    deepSeekTls.stop();
    outReport = "DeepSeek empty response. " + statusLine;
    return false;
  }
  deepSeekTls.stop();
  const char* jsonPtr = (const char*)memchr(respBuf, '{', respLen);
  if (!jsonPtr) {
    outReport = "DeepSeek: no JSON body. " + truncateText(statusLine, 80);
    return false;
  }
  JsonDocument filter;
  filter["choices"][0]["message"]["content"] = true;
  filter["error"]["message"] = true;

  if (!deepSeekDoc) {
    deepSeekDoc = newSpiRamJsonDoc();
  }
  if (!deepSeekDoc) {
    outReport = "DeepSeek: out of memory (JSON doc).";
    return false;
  }
  deepSeekDoc->clear();
  DeserializationError err = deserializeJson(
      *deepSeekDoc, jsonPtr, DeserializationOption::Filter(filter));
  if (err) {
    outReport = "DeepSeek JSON parse error (" + String(err.c_str()) + ").";
    return false;
  }
  if (!(*deepSeekDoc)["error"].isNull()) {
    String emsg = (*deepSeekDoc)["error"]["message"] | "API error";
    outReport = "DeepSeek error: " + truncateText(emsg, 200);
    return false;
  }
  String answer = collapseWhitespace((*deepSeekDoc)["choices"][0]["message"]["content"] | "");
  if (answer.length() == 0) {
    outReport = "DeepSeek returned an empty answer. " + truncateText(statusLine, 60);
    return false;
  }
  const char* prefix = "🧠 **DeepSeek:**\n";
  int room = DISCORD_CONTENT_MAX - (int)strlen(prefix);
  if (room < 100) room = 100;
  outReport = String(prefix) + truncateText(answer, room);
  return true;
}

void runAskFromLoop() {
  if (!askNeedPost) return;
  askNeedPost = false;
  String channelId = askPendingChannelId;
  String report;
  bool ok = askDeepSeek(askPendingQuestion, report);
  askPendingQuestion = "";
  askPendingChannelId = "";
  if (ok) {
    if (!sendDiscordMessage(channelId, report)) {
      noteCmdErrorReply("Post fail: DeepSeek");
      String fallback = "DeepSeek answered, but Discord rejected the post (try a shorter question).";
      if (!sendDiscordCmdError(channelId, fallback)) {
        showTransient("DeepSeek", "Post fail");
        return;
      }
    }
    showTransient("DeepSeek", "Sent");
  } else {
    if (!sendDiscordCmdError(channelId, report)) {
      String fallback = truncateText(report, DISCORD_CONTENT_MAX);
      if (!sendDiscordCmdError(channelId, fallback)) {
        showTransient("DeepSeek", "Post fail");
        return;
      }
    }
    showTransient("DeepSeek", "Error");
  }
}
