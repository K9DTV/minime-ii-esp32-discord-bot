#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include <ctype.h>
#include "secrets_bufs.h"
#include "minime_config.h"

// Compile-time seed macros -- this translation unit only (not via minime.h).
#include "secrets.h"
#if defined(MINIME_SECRETS_IS_EXAMPLE)
#error "Using secrets.example.h template -- copy to secrets.h, fill values, remove MINIME_SECRETS_IS_EXAMPLE"
#endif
#ifndef WEB_UI_PASSWORD
#define WEB_UI_PASSWORD ""
#endif

char secWifiSsid[SEC_WIFI_SSID_MAX];
char secWifiPassword[SEC_WIFI_PASS_MAX];
char secBotToken[SEC_BOT_TOKEN_MAX];
char secWeatherApiKey[SEC_API_KEY_MAX];
char secNasaApiKey[SEC_API_KEY_MAX];
char secDeepseekApiKey[SEC_API_KEY_MAX];
char secBotGuildId[SEC_ID_MAX];
char secOwnerId[SEC_ID_MAX];
char secTargetChannelId[SEC_ID_MAX];
char secTargetChannelId1[SEC_ID_MAX];
char secOtaHostname[SEC_OTA_HOST_MAX];
char secOtaPassword[SEC_OTA_PASS_MAX];
char secWebUiPassword[SEC_WEB_PASS_MAX];
bool secretsFromSd = false;

static void copyCap(char* dst, size_t dstMax, const char* src) {
  if (!dst || dstMax == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstMax - 1);
  dst[dstMax - 1] = '\0';
}

static void seedFromCompile() {
  copyCap(secWifiSsid, sizeof(secWifiSsid), WIFI_SSID);
  copyCap(secWifiPassword, sizeof(secWifiPassword), WIFI_PASSWORD);
  copyCap(secBotToken, sizeof(secBotToken), BOT_TOKEN);
  copyCap(secWeatherApiKey, sizeof(secWeatherApiKey), WEATHER_API_KEY);
  copyCap(secNasaApiKey, sizeof(secNasaApiKey), NASA_API_KEY);
  copyCap(secDeepseekApiKey, sizeof(secDeepseekApiKey), DEEPSEEK_API_KEY);
  copyCap(secBotGuildId, sizeof(secBotGuildId), BOT_GUILD_ID);
  copyCap(secOwnerId, sizeof(secOwnerId), OWNER_ID_STR);
  copyCap(secTargetChannelId, sizeof(secTargetChannelId), TARGET_CHANNEL_ID);
  copyCap(secTargetChannelId1, sizeof(secTargetChannelId1), TARGET_CHANNEL_ID1);
  copyCap(secOtaHostname, sizeof(secOtaHostname), OTA_HOSTNAME);
  copyCap(secOtaPassword, sizeof(secOtaPassword), OTA_PASSWORD);
  copyCap(secWebUiPassword, sizeof(secWebUiPassword), WEB_UI_PASSWORD);
  secretsFromSd = false;
}

// Drop compile macros so minime.h can alias WIFI_SSID -> secWifiSsid, etc.
#undef WIFI_SSID
#undef WIFI_PASSWORD
#undef BOT_TOKEN
#undef WEATHER_API_KEY
#undef NASA_API_KEY
#undef DEEPSEEK_API_KEY
#undef BOT_GUILD_ID
#undef OWNER_ID_STR
#undef TARGET_CHANNEL_ID
#undef TARGET_CHANNEL_ID1
#undef OTA_HOSTNAME
#undef OTA_PASSWORD
#undef WEB_UI_PASSWORD
#ifdef MINIME_SECRETS_H
#undef MINIME_SECRETS_H
#endif

#include "minime.h"

static bool applyDefine(const char* name, const char* value) {
  if (!name || !value) return false;
  if (strcmp(name, "WIFI_SSID") == 0) {
    copyCap(secWifiSsid, sizeof(secWifiSsid), value);
    return true;
  }
  if (strcmp(name, "WIFI_PASSWORD") == 0) {
    copyCap(secWifiPassword, sizeof(secWifiPassword), value);
    return true;
  }
  if (strcmp(name, "BOT_TOKEN") == 0) {
    copyCap(secBotToken, sizeof(secBotToken), value);
    return true;
  }
  if (strcmp(name, "WEATHER_API_KEY") == 0) {
    copyCap(secWeatherApiKey, sizeof(secWeatherApiKey), value);
    return true;
  }
  if (strcmp(name, "NASA_API_KEY") == 0) {
    copyCap(secNasaApiKey, sizeof(secNasaApiKey), value);
    return true;
  }
  if (strcmp(name, "DEEPSEEK_API_KEY") == 0) {
    copyCap(secDeepseekApiKey, sizeof(secDeepseekApiKey), value);
    return true;
  }
  if (strcmp(name, "BOT_GUILD_ID") == 0) {
    copyCap(secBotGuildId, sizeof(secBotGuildId), value);
    return true;
  }
  if (strcmp(name, "OWNER_ID_STR") == 0) {
    copyCap(secOwnerId, sizeof(secOwnerId), value);
    return true;
  }
  if (strcmp(name, "TARGET_CHANNEL_ID") == 0) {
    copyCap(secTargetChannelId, sizeof(secTargetChannelId), value);
    return true;
  }
  if (strcmp(name, "TARGET_CHANNEL_ID1") == 0) {
    copyCap(secTargetChannelId1, sizeof(secTargetChannelId1), value);
    return true;
  }
  if (strcmp(name, "OTA_HOSTNAME") == 0) {
    copyCap(secOtaHostname, sizeof(secOtaHostname), value);
    return true;
  }
  if (strcmp(name, "OTA_PASSWORD") == 0) {
    copyCap(secOtaPassword, sizeof(secOtaPassword), value);
    return true;
  }
  if (strcmp(name, "WEB_UI_PASSWORD") == 0) {
    copyCap(secWebUiPassword, sizeof(secWebUiPassword), value);
    return true;
  }
  return false;
}

static bool parseDefineLine(const char* line) {
  const char* p = line;
  while (*p && isspace((unsigned char)*p)) p++;
  if (*p == '\0' || (*p == '/' && p[1] == '/')) return false;
  if (*p != '#') return false;
  if (strncmp(p, "#define", 7) != 0) return false;
  p += 7;
  if (!isspace((unsigned char)*p)) return false;
  while (*p && isspace((unsigned char)*p)) p++;

  char name[40];
  size_t ni = 0;
  while (*p && (isalnum((unsigned char)*p) || *p == '_')) {
    if (ni + 1 < sizeof(name)) name[ni++] = *p;
    p++;
  }
  name[ni] = '\0';
  if (ni == 0) return false;
  while (*p && isspace((unsigned char)*p)) p++;

  char value[SEC_BOT_TOKEN_MAX];
  size_t vi = 0;
  if (*p == '"') {
    p++;
    while (*p && *p != '"') {
      if (*p == '\\' && p[1]) {
        p++;
        if (vi + 1 < sizeof(value)) value[vi++] = *p;
        p++;
        continue;
      }
      if (vi + 1 < sizeof(value)) value[vi++] = *p;
      p++;
    }
    value[vi] = '\0';
  } else {
    while (*p && !isspace((unsigned char)*p) && !(*p == '/' && p[1] == '/')) {
      if (vi + 1 < sizeof(value)) value[vi++] = *p;
      p++;
    }
    value[vi] = '\0';
  }
  if (strcmp(name, "MINIME_SECRETS_IS_EXAMPLE") == 0) return false;
  return applyDefine(name, value);
}

static bool loadFromSdFile() {
  if (!sdCardPresent()) return false;
  File f = SD.open("/secrets.h", FILE_READ);
  if (!f) {
    MmLog.println(F("Secrets: SD has no /secrets.h"));
    return false;
  }
  char line[192];
  size_t li = 0;
  int applied = 0;
  while (f.available()) {
    char c = (char)f.read();
    if (c == '\r') continue;
    if (c == '\n' || li + 1 >= sizeof(line)) {
      line[li] = '\0';
      if (li > 0 && parseDefineLine(line)) applied++;
      li = 0;
      if (c != '\n') {
        while (f.available()) {
          char d = (char)f.read();
          if (d == '\n') break;
        }
      }
      continue;
    }
    line[li++] = c;
  }
  if (li > 0) {
    line[li] = '\0';
    if (parseDefineLine(line)) applied++;
  }
  f.close();
  if (applied <= 0) {
    MmLog.println(F("Secrets: /secrets.h had no usable #define lines"));
    return false;
  }
  secretsFromSd = true;
  MmLog.print(F("Secrets: loaded "));
  MmLog.print(applied);
  MmLog.println(F(" keys from SD /secrets.h"));
  return true;
}

static bool secretsLookUsable() {
  if (secWifiSsid[0] == '\0') return false;
  if (secBotToken[0] == '\0') return false;
  if (strcmp(secBotToken, "bot token") == 0) return false;
  if (strcmp(secWifiSsid, "ssid") == 0) return false;
  return true;
}

bool loadSecrets() {
  seedFromCompile();
  if (!loadFromSdFile()) {
    MmLog.println(F("Secrets: using compile-time secrets.h (no SD overlay)"));
  }
  if (!secretsLookUsable()) {
    MmLog.println(F("Secrets: Wi-Fi SSID or BOT_TOKEN missing/placeholder"));
    return false;
  }
  return true;
}
