#ifndef MINIME_SECRETS_H
#define MINIME_SECRETS_H

// Copy this file to secrets.h (same folder), fill in real values, then DELETE the next line.
// Firmware seeds from secrets.h at boot, then overlays SD card /secrets.h when present.
// On the board: put the same filled file at the root of the SD card as secrets.h
// (preferred -- change credentials without reflashing).
#define MINIME_SECRETS_IS_EXAMPLE 1

// secrets.h is gitignored -- never commit real tokens or passwords.
// Use #define NAME "value" lines (same form on SD). Runtime parser reads those lines.

#define WIFI_SSID            "ssid"
#define WIFI_PASSWORD        "password"
#define BOT_TOKEN            "bot token"
#define WEATHER_API_KEY      "WEATHER_API_KEY"
#define NASA_API_KEY         "NASA_API_KEY"
#define DEEPSEEK_API_KEY     "DEEPSEEK_API_KEY"

#define BOT_GUILD_ID         "GUILD_ID"  // startup member fetch

#define OWNER_ID_STR         "OWNER_ID_STR"         // owner commands + mention alert
#define TARGET_CHANNEL_ID    "TARGET_CHANNEL_ID"    // commands only (no boot/auto posts)
#define TARGET_CHANNEL_ID1   "TARGET_CHANNEL_ID1"   // second command channel

// Wi-Fi firmware update (ArduinoOTA). Pick a real password in secrets.h.
#define OTA_HOSTNAME         "minime2"
#define OTA_PASSWORD         "change-me-ota"

// LAN web UI password. Empty "" = no auth (open on the LAN). Non-empty = gate /api/status + /api/controls.
#define WEB_UI_PASSWORD      ""

#endif
