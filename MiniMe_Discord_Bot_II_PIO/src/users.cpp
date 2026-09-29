#include "minime.h"

TrackedUser trackedUsers[MAX_TRACKED_USERS];
char cachedGuildIds[MAX_CACHED_GUILDS][24];
uint8_t cachedGuildCount = 0;
unsigned long usesWindowStartMillis = 0;

static void copyTruncField(char* dst, size_t dstLen, const char* src) {
  if (!dst || dstLen == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

uint8_t statusFromDiscord(const char* s) {
  if (!s) return 0;
  if (!strcmp(s, "online")) return 2;
  if (!strcmp(s, "idle")) return 1;
  if (!strcmp(s, "dnd")) return 3;
  return 0;  // offline / unknown
}

const char* statusToWord(uint8_t s) {
  static const char* w[] = {"Off", "Idle", "On", "DND"};
  return s < 4 ? w[s] : "Off";
}

void clearTrackedSlot(uint8_t i) {
  trackedUsers[i].active = false;
  trackedUsers[i].userId[0] = '\0';
  trackedUsers[i].userName[0] = '\0';
  trackedUsers[i].status = 0;
  trackedUsers[i].useCount24h = 0;
}

void fillTrackedSlot(uint8_t i, const char* userId, const char* userName) {
  trackedUsers[i].active = true;
  copyTruncField(trackedUsers[i].userId, sizeof(trackedUsers[i].userId), userId);
  copyTruncField(trackedUsers[i].userName, sizeof(trackedUsers[i].userName), userName);
  trackedUsers[i].status = 0;
  trackedUsers[i].useCount24h = 0;
}

void fillTrackedSlot(uint8_t i, const String& userId, const String& userName) {
  fillTrackedSlot(i, userId.c_str(), userName.c_str());
}

void initTrackedUsers() {
  for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) clearTrackedSlot(i);
}

void resetUseWindowIfNeeded() {
  unsigned long now = millis();
  if (usesWindowStartMillis == 0) {
    usesWindowStartMillis = now;
    return;
  }
  if (now - usesWindowStartMillis >= USES_WINDOW_MS) {
    usesWindowStartMillis = now;
    for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) {
      trackedUsers[i].useCount24h = 0;
    }
  }
}

int findUserIndex(const char* userId) {
  if (!userId || !userId[0]) return -1;
  for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) {
    if (trackedUsers[i].active && strcmp(trackedUsers[i].userId, userId) == 0) return (int)i;
  }
  return -1;
}

int findUserIndex(const String& userId) {
  return findUserIndex(userId.c_str());
}

int findFreeTrackedSlot() {
  for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) {
    if (!trackedUsers[i].active) return (int)i;
  }
  return -1;
}

int addOrPickUserSlot(const String& userId, const String& userName) {
  int idx = findUserIndex(userId);
  if (idx >= 0) {
    if (userName.length()) {
      copyTruncField(trackedUsers[idx].userName, sizeof(trackedUsers[idx].userName),
                     userName.c_str());
    }
    return idx;
  }

  idx = findFreeTrackedSlot();
  if (idx >= 0) {
    fillTrackedSlot((uint8_t)idx, userId, userName);
    return idx;
  }

  // Full: overwrite lowest 24h-use slot.
  uint8_t worst = 0;
  for (uint8_t i = 1; i < MAX_TRACKED_USERS; i++) {
    if (trackedUsers[i].useCount24h < trackedUsers[worst].useCount24h) worst = i;
  }
  fillTrackedSlot(worst, userId, userName);
  return (int)worst;
}

void recordUserUse(const String& userId, const String& userName) {
  resetUseWindowIfNeeded();
  if (userId.length() == 0) return;
  int idx = findUserIndex(userId);
  if (idx < 0) {
    idx = addOrPickUserSlot(userId, userName);
  }
  if (idx < 0) return;
  if (userName.length()) {
    copyTruncField(trackedUsers[idx].userName, sizeof(trackedUsers[idx].userName),
                   userName.c_str());
  }
  // Do not force status=On -- Discord PRESENCE_UPDATE owns Online/Idle/DND/Off.
  trackedUsers[idx].useCount24h++;
  noteDisplayActivity();
}

void applyPresencesArray(JsonArray presences) {
  if (presences.isNull()) return;
  for (JsonObject p : presences) {
    const char* uid = p["user"]["id"];
    if (!uid) continue;
    int idx = findUserIndex(uid);
    if (idx < 0) continue;
    const char* st = p["status"] | "offline";
    trackedUsers[idx].status = statusFromDiscord(st);
  }
}

String discordDisplayName(JsonVariantConst user) {
  String name = user["global_name"] | "";
  if (name.length() == 0) name = user["username"] | "";
  return name;
}

void handlePresenceUpdate(JsonObject d) {
  const char* uid = d["user"]["id"];
  const char* st  = d["status"] | "offline";
  if (!uid) return;

  String name = discordDisplayName(d["user"]);
  int idx = findUserIndex(uid);
  // Presence must not add unknown users (would evict tracked slots via addOrPickUserSlot).
  if (idx < 0) return;
  if (name.length()) {
    copyTruncField(trackedUsers[idx].userName, sizeof(trackedUsers[idx].userName), name.c_str());
  }
  trackedUsers[idx].status = statusFromDiscord(st);
}

void rememberGuildId(const String& gid) {
  String id = gid;
  id.trim();
  if (!discordIdLooksValid(id)) return;
  for (uint8_t i = 0; i < cachedGuildCount; i++) {
    if (strcmp(cachedGuildIds[i], id.c_str()) == 0) return;
  }
  if (cachedGuildCount >= MAX_CACHED_GUILDS) return;
  copyTruncField(cachedGuildIds[cachedGuildCount], sizeof(cachedGuildIds[0]), id.c_str());
  cachedGuildCount++;
}
