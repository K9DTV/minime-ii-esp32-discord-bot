#ifndef SECRETS_BUFS_H
#define SECRETS_BUFS_H

// Runtime credential buffers. Boot fills these from SD /secrets.h only.
// Prefer these names via macros in minime.h (WIFI_SSID -> secWifiSsid, etc.).

#include <stddef.h>
#include <stdint.h>

enum {
  SEC_WIFI_SSID_MAX = 64,
  SEC_WIFI_PASS_MAX = 64,
  SEC_BOT_TOKEN_MAX = 128,
  SEC_API_KEY_MAX = 96,
  SEC_ID_MAX = 24,
  SEC_OTA_HOST_MAX = 32,
  SEC_OTA_PASS_MAX = 64,
  SEC_WEB_PASS_MAX = 64
};

extern char secWifiSsid[SEC_WIFI_SSID_MAX];
extern char secWifiPassword[SEC_WIFI_PASS_MAX];
extern char secBotToken[SEC_BOT_TOKEN_MAX];
extern char secWeatherApiKey[SEC_API_KEY_MAX];
extern char secNasaApiKey[SEC_API_KEY_MAX];
extern char secDeepseekApiKey[SEC_API_KEY_MAX];
extern char secBotGuildId[SEC_ID_MAX];
extern char secOwnerId[SEC_ID_MAX];
extern char secTargetChannelId[SEC_ID_MAX];
extern char secTargetChannelId1[SEC_ID_MAX];
extern char secOtaHostname[SEC_OTA_HOST_MAX];
extern char secOtaPassword[SEC_OTA_PASS_MAX];
extern char secWebUiPassword[SEC_WEB_PASS_MAX];

// true = SD /secrets.h was opened (content may still be unusable).
// false = no card, or the card has no /secrets.h.
extern bool secretsFromSd;

// Call after setupSdCard(). Loads SD /secrets.h only (no compile-time credential fallback).
// Returns true if Wi-Fi SSID + bot token from that file look usable.
bool loadSecrets();

#endif
