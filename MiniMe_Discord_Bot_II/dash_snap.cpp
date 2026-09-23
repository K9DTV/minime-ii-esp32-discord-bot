#include "display_internal.h"
#include <string.h>

// Seqlock: Core 1 writes publishedSnap between odd/even seq; Core 0 retries if torn.
static DashSnap publishedSnap = {};
static volatile uint32_t snapSeq = 0;

DashSnap drawnSnap = {};

static uint32_t hashUsers() {
  uint32_t h = 2166136261u;
  for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) {
    h ^= trackedUsers[i].active ? 1u : 0u;
    h *= 16777619u;
    h ^= (uint32_t)trackedUsers[i].status;
    h *= 16777619u;
    h ^= trackedUsers[i].useCount24h;
    h *= 16777619u;
    const char* id = trackedUsers[i].userId;
    for (size_t c = 0; id[c]; c++) {
      h ^= (uint8_t)id[c];
      h *= 16777619u;
    }
    const char* nm = trackedUsers[i].userName;
    for (size_t c = 0; nm[c]; c++) {
      h ^= (uint8_t)nm[c];
      h *= 16777619u;
    }
  }
  return h;
}

static void captureSnap(DashSnap& s) {
  memset(&s, 0, sizeof(s));
  s.valid = true;
  s.themeLight = lcdThemeLight;
  s.layoutLog = lcdLayoutLog;
  s.layoutControls = lcdLayoutControls;
  s.controlsGen = uiControlsGen.load();
  updateLocalTime();
  formatLocalTimeStr(s.timeStr, sizeof(s.timeStr));
  formatLocalDateStr(s.dateStr, sizeof(s.dateStr));
  formatUptimeStr(s.upStr, sizeof(s.upStr));
  if (!gatewayConnected) s.gw = -1;
  else if (identified) s.gw = 1;
  else s.gw = 0;
  s.bot = (int8_t)botDiscordStatus;
  s.rssi = WiFi.RSSI();
  boardMemTotals(s.memFree, s.memTotal);
  boardPsramTotals(s.psFree, s.psTotal);
  s.servoDeg = lastServoDeg;
  {
    float tc = 0, tf = 0;
    bool had = false, fresh = false;
    dashTempSnapshot(tc, tf, had, fresh);
    s.tempC10 = fresh ? (int)(tc * 10.0f) : -9990;
  }
  s.identified = identified;
  s.nActive = 0;
  for (uint8_t i = 0; i < MAX_TRACKED_USERS; i++) {
    if (trackedUsers[i].active) s.nActive++;
    const char* src = "---";
    if (trackedUsers[i].active) {
      if (trackedUsers[i].userName[0]) src = trackedUsers[i].userName;
      else src = trackedUsers[i].userId;
    }
    size_t n = 0;
    while (src[n] && n + 1 < sizeof(s.users[i].name) && n < 12) {
      s.users[i].name[n] = src[n];
      n++;
    }
    s.users[i].name[n] = '\0';
    strncpy(s.users[i].status,
            statusToWord(trackedUsers[i].active ? trackedUsers[i].status : 0),
            sizeof(s.users[i].status) - 1);
    uint32_t uses = trackedUsers[i].active ? trackedUsers[i].useCount24h : 0;
    snprintf(s.users[i].bot, sizeof(s.users[i].bot), "%lu", (unsigned long)uses);
  }
  s.dm = alertDm.load();
  s.mention = alertMention.load();
  s.httpsBusy = httpsInUse;
  uiOverlayCopyEvent(s.event, sizeof(s.event));
  {
    IPAddress ip = WiFi.localIP();
    snprintf(s.ip, sizeof(s.ip), "%u.%u.%u.%u",
             (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
  }
  s.cpuMhz = getCpuFrequencyMhz();
  s.userHash = hashUsers();
  s.logGen = lcdLogGen();
  s.logRowCount = 0;
  for (uint8_t i = 0; i < DASH_LOG_ROWS; i++) {
    if (!lcdFullLogNewest(i, s.logRows[i], sizeof(s.logRows[i]))) break;
    s.logRowCount++;
  }
  s.serialRowCount = 0;
  for (uint8_t i = 0; i < DASH_LOG_ROWS; i++) {
    if (!lcdSerialNewest(i, s.serialRows[i], sizeof(s.serialRows[i]))) break;
    s.serialRowCount++;
  }
}

void publishDashSnap() {
  // Throttle: full snap copy every DASH_REFRESH_MS (loop can run much faster).
  static unsigned long lastPubMs = 0;
  unsigned long now = millis();
  if (lastPubMs != 0 && (now - lastPubMs) < DASH_REFRESH_MS) return;
  lastPubMs = now;

  static DashSnap tmp; // static: ~4 KB — keep off Core 1 loop stack
  captureSnap(tmp);
  uint32_t s = snapSeq;
  snapSeq = s + 1; // odd = writer in progress
  publishedSnap = tmp;
  snapSeq = s + 2; // even = stable
}

void loadPublishedSnap(DashSnap& out) {
  for (;;) {
    uint32_t s1 = snapSeq;
    if (s1 & 1u) {
      taskYIELD();
      continue; // writer mid-update
    }
    out = publishedSnap;
    uint32_t s2 = snapSeq;
    if (s1 == s2 && !(s2 & 1u)) break;
    taskYIELD();
  }
}

bool snapLeftEqual(const DashSnap& a, const DashSnap& b) {
  if (a.themeLight != b.themeLight || a.layoutLog != b.layoutLog) return false;
  if (a.layoutControls != b.layoutControls) return false;
  if (a.layoutControls) return a.controlsGen == b.controlsGen;
  if (a.layoutLog) return a.logGen == b.logGen; // LOG window
  // Display mode left: status / metrics
  return strcmp(a.timeStr, b.timeStr) == 0
      && strcmp(a.dateStr, b.dateStr) == 0
      && strcmp(a.upStr, b.upStr) == 0
      && a.gw == b.gw && a.bot == b.bot
      && a.rssi == b.rssi
      && a.memFree == b.memFree && a.memTotal == b.memTotal
      && a.psFree == b.psFree && a.psTotal == b.psTotal
      && a.servoDeg == b.servoDeg && a.tempC10 == b.tempC10
      && a.identified == b.identified && a.nActive == b.nActive
      && a.dm == b.dm && a.mention == b.mention && a.httpsBusy == b.httpsBusy
      && strcmp(a.event, b.event) == 0
      && strcmp(a.ip, b.ip) == 0
      && a.cpuMhz == b.cpuMhz;
}

bool snapRightEqual(const DashSnap& a, const DashSnap& b) {
  if (a.themeLight != b.themeLight || a.layoutLog != b.layoutLog) return false;
  if (a.layoutControls != b.layoutControls) return false;
  if (a.layoutControls) return a.controlsGen == b.controlsGen;
  if (a.layoutLog) return a.logGen == b.logGen; // Serial window
  return a.userHash == b.userHash; // Display mode right: users
}
