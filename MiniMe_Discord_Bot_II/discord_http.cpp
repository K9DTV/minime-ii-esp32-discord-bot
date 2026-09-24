#include "minime.h"
#include "cores.h"

// HTTP header/body helpers (split from discord_rest.cpp). Core 1 only.

// Gateway HB always; drain cmds only when shared HTTPS is free (DeepSeek has its own TLS).
void pumpNetWait() {
  pumpGateway();
  if (!httpsInUse) drainDiscordCmds();
}

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
bool httpSkipHeaders(Client& client, unsigned long timeoutMs,
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
        return false; // capped body is incomplete -- not success
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
        client.stop();
        return false;
      }
      String sizeLine;
      if (!readHttpLineCapped(client, sizeLine, deadlineMs)) {
        client.stop();
        return false;
      }
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
          return false;
        } else {
          pumpNetWait();
          delay(1);
        }
      }
      if (got < chunkSize) {
        client.stop();
        return false;
      }
      String trailer;
      if (!readHttpLineCapped(client, trailer, deadlineMs)) {
        client.stop();
        return false;
      }
      if (hitCap) {
        client.stop();
        return false;
      }
    }
    client.stop();
    return false;
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
          return false;
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
      return false;
    }
    return true;
  }
  while (millis() < deadlineMs) {
    while (client.available()) {
      size_t room = maxBody - outLen;
      if (room == 0) {
        client.stop();
        return false;
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
