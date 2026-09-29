#ifndef MINIME_CORES_H
#define MINIME_CORES_H

#include <Arduino.h>

// Dual-core: Core 1 = Discord/Wi-Fi/HTTPS/OTA/web; Core 0 = LCD + touch.
// See docs / README dual-core section.

enum { DISCORD_CMD_CONTENT_MAX = 512 };
enum { DISCORD_CMD_QUEUE_DEPTH = 6 };

struct DiscordCmdJob {
  char content[DISCORD_CMD_CONTENT_MAX];
  char channelId[24];
  char authorId[24];
  char authorName[40];
  bool isDM;
};

// Call after setupDisplay + setupTouch. Pins uiTask to Core 0.
void startUiCore();

// Core 1: enqueue from Gateway MESSAGE_CREATE (never call handleCommand there).
bool enqueueDiscordCmd(const String& content, const String& authorId,
                       const String& authorName, const String& channelId, bool isDM);

// Core 1: drain one or more jobs from loop() after pumpGateway.
void drainDiscordCmds();

// Core 1: refresh published DashSnap for Core 0 paint (no DS18B20; temp from Core 0).
void publishDashSnap();

#endif
