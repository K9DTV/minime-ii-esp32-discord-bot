#include "minime.h"
#include "cores.h"
#include <stdlib.h>
#include <string.h>

// HTTP header/body helpers (split from discord_rest.cpp). Core 1 only.
// Line and status scratch live on the stack (pumpNetWait may re-enter HTTPS on the other client).

// Gateway HB while its socket is up; drain cmds only when shared HTTPS is free (DeepSeek has its
// own TLS). A down Gateway is left for loop() to reconnect after the command returns: a reconnect
// here runs its TLS handshake beside the HTTPS session being waited on, and blocks that wait.
void pumpNetWait() {
  if (gatewayWS.socketOpen()) pumpGateway();
  if (!httpsInUse) drainDiscordCmds();
}

static const size_t HTTP_LINE_MAX = 512;

static void copyCapped(char* dst, size_t cap, const char* src) {
  if (!dst || cap == 0) return;
  strncpy(dst, src ? src : "", cap - 1);
  dst[cap - 1] = '\0';
}

static void asciiToLowerInPlace(char* s) {
  if (!s) return;
  for (; *s; ++s) {
    if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
  }
}

static bool startsWith(const char* s, const char* prefix) {
  if (!s || !prefix) return false;
  while (*prefix) {
    if (*s != *prefix) return false;
    ++s;
    ++prefix;
  }
  return true;
}

// Read one CRLF-terminated line. Stores at most HTTP_LINE_MAX chars (and cap-1).
// Bytes past the cap are discarded until '\n'. False on deadline or peer close mid-line;
// a partial line is left in out.
static bool readHttpLineCapped(Client& client, char* out, size_t cap, unsigned long deadlineMs) {
  if (!out || cap == 0) return false;
  size_t len = 0;
  out[0] = '\0';
  size_t storeMax = HTTP_LINE_MAX;
  if (cap - 1 < storeMax) storeMax = cap - 1;
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
    if (len < storeMax) {
      out[len++] = c;
      out[len] = '\0';
    }
  }
  return false;
}

// line is lowercased in place. Same checks as the old String::toLowerCase path:
// startsWith + indexOf("chunked") + toInt/toFloat after ':'.
static void applyHeaderLine(char* line, bool& chunked, int& contentLength, float* retryAfterSec) {
  asciiToLowerInPlace(line);
  if (startsWith(line, "transfer-encoding:") && strstr(line, "chunked") != nullptr) {
    chunked = true;
  }
  if (startsWith(line, "content-length:")) {
    const char* colon = strchr(line, ':');
    if (colon) contentLength = (int)atol(colon + 1);
  }
  if (retryAfterSec && startsWith(line, "retry-after:")) {
    // Discord sends seconds (int/float). Ignore HTTP-date forms (atof == 0).
    const char* colon = strchr(line, ':');
    if (colon) {
      float v = (float)atof(colon + 1);
      if (v > 0.f) *retryAfterSec = v;
    }
  }
}

// Skip status + headers; fill Transfer-Encoding / Content-Length for the body reader.
bool httpSkipHeaders(Client& client, unsigned long timeoutMs,
                     bool& outChunked, int& outContentLength) {
  outChunked = false;
  outContentLength = -1;
  unsigned long deadline = millis() + timeoutMs;
  while (client.available() == 0) {
    if (millis() > deadline) return false;
    delay(1);
  }
  char line[HTTP_LINE_MAX + 1];
  if (!readHttpLineCapped(client, line, sizeof(line), deadline)) return false;
  while (millis() <= deadline && (client.connected() || client.available())) {
    if (!readHttpLineCapped(client, line, sizeof(line), deadline)) return false;
    if (line[0] == '\0') return true;
    applyHeaderLine(line, outChunked, outContentLength, nullptr);
  }
  return false;
}

bool httpsAwaitHeaders(Client& client, unsigned long deadlineMs, bool pump, char* outStatus,
                       size_t statusCap, bool& chunked, int& contentLength,
                       float* outRetryAfterSec) {
  if (outRetryAfterSec) *outRetryAfterSec = -1.f;
  // Match the old String overload: a provided status buffer is replaced, including
  // with "" when the status line is never read (timeout before the first byte).
  if (outStatus && statusCap > 0) outStatus[0] = '\0';
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
  char line[HTTP_LINE_MAX + 1];
  if (!readHttpLineCapped(client, line, sizeof(line), deadlineMs)) {
    copyCapped(outStatus, statusCap, line);
    client.stop();
    return false;
  }
  copyCapped(outStatus, statusCap, line);
  chunked = false;
  contentLength = -1;
  while (millis() <= deadlineMs) {
    if (!readHttpLineCapped(client, line, sizeof(line), deadlineMs)) {
      client.stop();
      return false;
    }
    if (line[0] == '\0') break;
    applyHeaderLine(line, chunked, contentLength, outRetryAfterSec);
  }
  return true;
}

bool readHttpBodyAfterHeaders(Client& client, bool chunked, int contentLength,
                              char* outBuf, size_t outCap, size_t& outLen,
                              unsigned long deadlineMs) {
  outLen = 0;
  if (!outBuf || outCap < 2) return false;
  outBuf[0] = '\0';
  const size_t maxBody = outCap - 1; // leave room for NUL
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
      char line[HTTP_LINE_MAX + 1];
      if (!readHttpLineCapped(client, line, sizeof(line), deadlineMs)) {
        client.stop();
        return false;
      }
      // Rare bare CRLF between chunks (malformed / CDN quirk). Bound so endless CRLFs
      // cannot spin past deadlineMs without failing. Trailer already ate the post-chunk CRLF.
      if (line[0] == '\0') {
        if (++emptySizeLines > 8) {
          client.stop();
          return false;
        }
        continue;
      }
      emptySizeLines = 0;
      char* semi = strchr(line, ';');
      if (semi) *semi = '\0';
      long chunkSize = strtol(line, nullptr, 16);
      if (chunkSize <= 0) {
        // Final 0-size chunk: complete message (empty body still false for callers).
        return outLen > 0;
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
            if (outLen + (size_t)n > maxBody) {
              hitCap = true;
            } else {
              memcpy(outBuf + outLen, blk, (size_t)n);
              outLen += (size_t)n;
              outBuf[outLen] = '\0';
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
      if (!readHttpLineCapped(client, line, sizeof(line), deadlineMs)) {
        client.stop();
        return false; // partial body is not success (same class as 0.7.8 CL truncate)
      }
      if (hitCap) {
        client.stop();
        return false; // capped body is incomplete -- not success
      }
    }
    client.stop();
    return false; // deadline without final 0-chunk
  }
  if (contentLength > 0) {
    while ((int)outLen < contentLength && millis() < deadlineMs) {
      while (client.available()) {
        size_t remain = (size_t)contentLength - outLen;
        if (remain > sizeof(blk)) remain = sizeof(blk);
        int n = client.read((uint8_t*)blk, remain);
        if (n <= 0) break;
        if (outLen + (size_t)n > maxBody) {
          client.stop();
          return false; // truncated
        }
        memcpy(outBuf + outLen, blk, (size_t)n);
        outLen += (size_t)n;
        outBuf[outLen] = '\0';
        if ((int)outLen >= contentLength) break;
      }
      if (!client.connected() && !client.available()) break;
      pumpNetWait();
      delay(5);
    }
    if ((int)outLen < contentLength) {
      client.stop();
      return false; // incomplete Content-Length body
    }
    return true;
  }
  // Until-close fallback (no chunked, no Content-Length). Best-effort only: peer close
  // with a partial body still returns length>0. Callers must validate JSON / shape.
  while (millis() < deadlineMs) {
    while (client.available()) {
      size_t room = maxBody - outLen;
      if (room == 0) {
        client.stop();
        return false; // truncated
      }
      size_t want = room;
      if (want > sizeof(blk)) want = sizeof(blk);
      int n = client.read((uint8_t*)blk, want);
      if (n <= 0) break;
      memcpy(outBuf + outLen, blk, (size_t)n);
      outLen += (size_t)n;
      outBuf[outLen] = '\0';
    }
    if (!client.connected() && !client.available()) break;
    pumpNetWait();
    delay(10);
  }
  return outLen > 0;
}
