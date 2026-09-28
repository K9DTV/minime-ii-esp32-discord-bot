#include "minime.h"
#include <stdio.h>
#include <string.h>

// Discord command tokenizer + handleCommand dispatch table (kCmds).

bool askNeedPost = false;
char askPendingQuestion[ASK_QUESTION_MAX + 1] = "";
char askPendingChannelId[DISCORD_SNOWFLAKE_MAX] = "";

typedef bool (*FetchReportFn)(char*, size_t);

static void sendFetchResult(const char* channelId, const char* label, bool ok, const char* report,
                            const char* okLine2 = "Sent", const char* okLine3 = "") {
  if (!ok) {
    sendDiscordCmdError(channelId, report);
    setTransient(label, "Error");
    return;
  }
  const bool posted = sendDiscordMessage(channelId, report);
  if (posted) setTransient(label, okLine2, okLine3);
  else {
    char note[48];
    snprintf(note, sizeof(note), "Post fail: %s", label ? label : "?");
    noteCmdErrorReply(note);
    setTransient(label, "Post fail");
  }
}

static void runFetchCommand(const char* channelId, const char* label, const char* fetching,
                            FetchReportFn fetch) {
  char report[CMD_REPORT_MAX];
  report[0] = '\0';
  setTransient(label, fetching);
  sendFetchResult(channelId, label, fetch(report, sizeof(report)), report);
}

// LCD value only if Discord accepted the post (same rule as sendFetchResult / !help).
static void showIfPosted(const char* label, const char* okLine2, bool posted) {
  if (posted) setTransient(label, okLine2);
  else {
    char note[48];
    snprintf(note, sizeof(note), "Post fail: %s", label ? label : "?");
    noteCmdErrorReply(note);
    setTransient(label, "Post fail");
  }
}

// ---- dispatch table (single place for consumes-rest / owner / record-use) ----

struct CmdCtx {
  const String& channelId;
  const String& authorId;
  const String& authorName;
  const String& args;
  bool isDM;
};

typedef void (*CmdHandler)(const CmdCtx& ctx);

enum CmdFlags : uint8_t {
  CMD_NONE = 0,
  CMD_CONSUMES_REST = 1u << 0, // multi-word args (mid-line tokenize keeps full rest)
  CMD_OWNER = 1u << 1,
  CMD_RECORD_USE = 1u << 2,
};

struct CmdEntry {
  const char* name;
  uint8_t flags;
  CmdHandler handler;
};

static void cmdHelp(const CmdCtx& ctx) {
  static const char helpMsg[] =
    "🤖 **MiniMe Bot Commands**\n\n"
    "**👤 Public Commands:**\n"
    "- `!apod` -- NASA Astronomy Picture of the Day.\n"
    "- `!ask <question>` -- Asks DeepSeek (text AI reply in chat).\n"
    "- `!msg <text>` -- Sticky Msg line on the LCD Display panel (max 31 characters; Clear / !clear wipes it).\n"
    "- `!help` -- Shows this command list.\n"
    "- `!iss` -- Current International Space Station position.\n"
    "- `!news` -- Space and high-tech science headlines.\n"
    "- `!physics` -- Latest arXiv physics papers.\n"
    "- `!sys` -- System diagnostics (uptime, internal heap, PSRAM, RSSI, gateway, firmware URL).\n"
    "- `!temp` -- Reads the current indoor temperature sensor.\n"
    "- `!time` -- Displays the current bot time.\n"
    "- `!weather <zip>` -- Fetches the weather report for a US ZIP code.\n\n"
    "**👑 Owner-Only Commands:**\n"
    "- `!ota` -- Wi-Fi firmware update info (IP / hostname).\n"
    "- `!coredump` -- Last panic from flash coredump (`!coredump clear` erases).\n"
    "- `!clear` -- Clears DM / mention alerts and the Msg line on the LCD (stops the alarm sound).\n"
    "- `!resetprefs` -- Factory-reset Controls prefs in flash (bright/vol/toggles/theme).";
  showIfPosted("Help", "Command Sent", sendDiscordMessage(ctx.channelId.c_str(), helpMsg));
}

static void cmdWeather(const CmdCtx& ctx) {
  if (ctx.args.length() == 0) {
    sendDiscordMessage(ctx.channelId.c_str(), "Usage: !weather <zip>");
    return;
  }
  String zip = ctx.args;
  bool validZip = (zip.length() == 5);
  if (validZip) {
    for (unsigned i = 0; i < 5; i++) {
      char c = zip.charAt(i);
      if (c < '0' || c > '9') { validZip = false; break; }
    }
  }
  if (!validZip) {
    sendDiscordMessage(ctx.channelId.c_str(), "Invalid ZIP code.");
    return;
  }
  char report[CMD_REPORT_MAX];
  report[0] = '\0';
  setTransient("Weather", "Fetching...");
  bool ok = getWeather(zip.c_str(), report, sizeof(report));
  sendFetchResult(ctx.channelId.c_str(), "Weather", ok, report, zip.c_str(), "Sent");
}

static void cmdNews(const CmdCtx& ctx) {
  runFetchCommand(ctx.channelId.c_str(), "News", "Fetching...", getScienceNews);
}

static void cmdPhysics(const CmdCtx& ctx) {
  runFetchCommand(ctx.channelId.c_str(), "Physics", "Fetching arXiv...", getPhysicsPapers);
}

static void cmdApod(const CmdCtx& ctx) {
  runFetchCommand(ctx.channelId.c_str(), "APOD", "Fetching NASA...", getApod);
}

static void cmdIss(const CmdCtx& ctx) {
  runFetchCommand(ctx.channelId.c_str(), "ISS", "Fetching...", getIssPosition);
}

static void cmdTemp(const CmdCtx& ctx) {
  float c = 0, f = 0;
  bool had = false, fresh = false;
  dashTempSnapshot(c, f, had, fresh);
  if (fresh) {
    char msg[96];
    char lcd[24];
    snprintf(msg, sizeof(msg), "Current Temp: %.1f degC / %.1f degF", c, f);
    snprintf(lcd, sizeof(lcd), "%.1fF/%.1fC", f, c);
    showIfPosted("Temp", lcd, sendDiscordMessage(ctx.channelId.c_str(), msg));
  } else if (had) {
    showIfPosted("Temp", "Stale",
                 sendDiscordCmdError(ctx.channelId.c_str(),
                                     "Temperature reading is stale (>30s). Sensor may be disconnected."));
  } else {
    showIfPosted("Temp", "Sensor error",
                 sendDiscordCmdError(ctx.channelId.c_str(), "Temperature sensor error."));
  }
}

static void cmdSys(const CmdCtx& ctx) {
  char report[CMD_REPORT_MAX];
  formatSystemInfo(report, sizeof(report));
  showIfPosted("Sys", "Sent", sendDiscordMessage(ctx.channelId.c_str(), report, true));
}

static void cmdOta(const CmdCtx& ctx) {
  String ota = otaStatusText();
  String ip = WiFi.localIP().toString();
  showIfPosted("OTA", ip.c_str(), sendDiscordMessage(ctx.channelId.c_str(), ota.c_str()));
}

static void cmdCoredump(const CmdCtx& ctx) {
  char report[CMD_REPORT_MAX];
  report[0] = '\0';
  if (ctx.args.equalsIgnoreCase("clear") || ctx.args.equalsIgnoreCase("erase")) {
    bool ok = clearCoreDumpImage(report, sizeof(report));
    showIfPosted("Coredump", ok ? "Cleared" : "Error",
                 ok ? sendDiscordMessage(ctx.channelId.c_str(), report, true)
                    : sendDiscordCmdError(ctx.channelId.c_str(), report, true));
    return;
  }
  setTransient("Coredump", "Reading...");
  bool ok = formatCoreDumpReport(report, sizeof(report));
  showIfPosted("Coredump", ok ? "Sent" : "Empty",
               ok ? sendDiscordMessage(ctx.channelId.c_str(), report, true)
                  : sendDiscordCmdError(ctx.channelId.c_str(), report, true));
}

static void cmdTime(const CmdCtx& ctx) {
  updateLocalTime();
  char currentTime[12];
  formatLocalTimeStr(currentTime, sizeof(currentTime));
  char msg[48];
  snprintf(msg, sizeof(msg), "🕒 Current Bot Time: %s", currentTime);
  showIfPosted("Time", currentTime, sendDiscordMessage(ctx.channelId.c_str(), msg));
}

static void cmdAsk(const CmdCtx& ctx) {
  if (ctx.args.length() == 0) {
    sendDiscordMessage(ctx.channelId.c_str(), "Usage: !ask <question>");
    return;
  }
  if (askNeedPost) {
    sendDiscordCmdError(ctx.channelId.c_str(),
                        "DeepSeek is already answering. Try again in a moment.");
    return;
  }
  size_t n = ctx.args.length();
  if (n > ASK_QUESTION_MAX) n = ASK_QUESTION_MAX;
  memcpy(askPendingQuestion, ctx.args.c_str(), n);
  askPendingQuestion[n] = '\0';
  strncpy(askPendingChannelId, ctx.channelId.c_str(), sizeof(askPendingChannelId) - 1);
  askPendingChannelId[sizeof(askPendingChannelId) - 1] = '\0';
  askNeedPost = true;
  setTransient("DeepSeek", "Queued"); // local queue; Discord reply checked in runAskFromLoop
}

static void cmdMessage(const CmdCtx& ctx) {
  if (ctx.args.length() == 0) {
    sendDiscordMessage(ctx.channelId, "Usage: !msg <text> (max 31 characters on LCD Msg line)");
    return;
  }
  String text = ctx.args;
  // UI_MSG_COLS includes NUL; glass fits ~31 chars after "Msg:". Sticky -- no timed clear.
  const int maxChars = UI_MSG_COLS - 1;
  if ((int)text.length() > maxChars) text = text.substring(0, maxChars);
  noteLcdMessage(text);
  char reply[80];
  snprintf(reply, sizeof(reply), "Msg updated (%d/%d chars).", (int)text.length(), maxChars);
  if (!sendDiscordMessage(ctx.channelId, reply)) {
    noteCmdErrorReply("Post fail: Msg");
    setTransient("Msg", "Post fail");
  }
}

static void cmdClear(const CmdCtx& ctx) {
  clearAlertFlags();
  showIfPosted("clear", "alerts+Msg OFF",
               sendDiscordMessage(ctx.channelId, "DM/mention alerts and Msg cleared"));
}

static void cmdResetPrefs(const CmdCtx& ctx) {
  const bool ok = factoryResetSettings();
  setTransient("Prefs", ok ? "factory reset" : "reset fail");
  showIfPosted("resetprefs", ok ? "factory OK" : "write fail",
               sendDiscordMessage(ctx.channelId,
                                  ok ? "Controls prefs reset to factory defaults."
                                     : "Prefs reset applied in RAM; flash write failed."));
}

#ifdef MINIME_TEST_TWDT
// Scratch only: never returns so Core 1 loop() stops feeding TWDT (~90 s panic).
static void cmdHang(const CmdCtx&) {
  setTransient("TWDT", "hang...");
  while (true) {
    delay(1);
  }
}
#endif

// Name match is exact (already lowercased). CMD_CONSUMES_REST is the mid-line tokenize rule.
static const CmdEntry kCmds[] = {
  { "!help",     CMD_NONE,                           cmdHelp },
  { "!weather",  CMD_RECORD_USE,                     cmdWeather },
  { "!news",     CMD_RECORD_USE,                     cmdNews },
  { "!physics",  CMD_RECORD_USE,                     cmdPhysics },
  { "!apod",     CMD_RECORD_USE,                     cmdApod },
  { "!iss",      CMD_RECORD_USE,                     cmdIss },
  { "!temp",     CMD_RECORD_USE,                     cmdTemp },
  { "!sys",      CMD_RECORD_USE,                     cmdSys },
  { "!ota",      CMD_OWNER | CMD_RECORD_USE,         cmdOta },
  { "!coredump", CMD_OWNER | CMD_RECORD_USE,         cmdCoredump },
  { "!time",     CMD_RECORD_USE,                     cmdTime },
  { "!ask",      CMD_CONSUMES_REST | CMD_RECORD_USE, cmdAsk },
  { "!msg",      CMD_CONSUMES_REST | CMD_RECORD_USE, cmdMessage },
  { "!clear",      CMD_OWNER | CMD_RECORD_USE,         cmdClear },
  { "!resetprefs", CMD_OWNER | CMD_RECORD_USE,         cmdResetPrefs },
#ifdef MINIME_TEST_TWDT
  { "!hang",     CMD_OWNER,                          cmdHang },
#endif
};

static const CmdEntry* findCmd(const String& cmdWord) {
  for (size_t i = 0; i < sizeof(kCmds) / sizeof(kCmds[0]); i++) {
    if (cmdWord.equals(kCmds[i].name)) return &kCmds[i];
  }
  return nullptr;
}

static void stripTrailingPunct(String& s) {
  while (s.length() > 0) {
    char last = s.charAt(s.length() - 1);
    if (last == '.' || last == ',' || last == '!' || last == '?' ||
        last == ';' || last == ':') {
      s.remove(s.length() - 1);
    } else {
      break;
    }
  }
}

// Find "!cmd" at line start or after whitespace. Sets cmdWord (lower) + args.
// entryOut is the table row for cmdWord (nullptr if unknown); one findCmd for tokenize + dispatch.
static bool tokenizeCommand(const String& content, const CmdEntry*& entryOut,
                            String& cmdWord, String& args) {
  entryOut = nullptr;
  String raw = content;
  raw.trim();
  if (raw.length() == 0) return false;

  bool midLine = false;
  if (!raw.startsWith("!")) {
    int bang = -1;
    for (int i = 0; i < (int)raw.length(); i++) {
      if (raw.charAt(i) != '!') continue;
      char next = (i + 1 < (int)raw.length()) ? raw.charAt(i + 1) : 0;
      if (!((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z'))) continue;
      if (i > 0) {
        char prev = raw.charAt(i - 1);
        if (prev != ' ' && prev != '\t' && prev != '\n') continue;
      }
      bang = i;
      break;
    }
    if (bang < 0) return false;
    raw = raw.substring(bang);
    midLine = true;
  }

  int spIdx = raw.indexOf(' ');
  cmdWord = (spIdx > 0) ? raw.substring(0, spIdx) : raw;
  args = (spIdx > 0) ? raw.substring(spIdx + 1) : "";
  cmdWord.toLowerCase();
  args.trim();
  stripTrailingPunct(cmdWord);

  if (cmdWord.length() <= 1) return false;
  entryOut = findCmd(cmdWord);

  // Mid-line: one-word args unless CMD_CONSUMES_REST. Strip trailing punct on short args.
  if (midLine && args.length() > 0 &&
      !(entryOut && (entryOut->flags & CMD_CONSUMES_REST))) {
    int argSp = args.indexOf(' ');
    if (argSp > 0) args = args.substring(0, argSp);
    stripTrailingPunct(args);
  }
  return true;
}

void handleCommand(const String& content, const String& authorId, const String& authorName,
                   const String& channelId, bool isDM)
{
  if (!isDM && channelId != TARGET_CHANNEL_ID && channelId != TARGET_CHANNEL_ID1) {
    return;
  }
  String cmdWord, args;
  const CmdEntry* e = nullptr;
  if (!tokenizeCommand(content, e, cmdWord, args)) return;

  noteBotActivity();

  if (!e) {
    sendDiscordMessage(channelId, "That is not a command.");
    setTransient("Unknown", cmdWord);
    return;
  }

  if (e->flags & CMD_OWNER) {
    if (!isOwner(authorId)) {
      if (!isDM) {
        sendDiscordMessage(channelId, "You are not allowed to use this command.");
      }
      return;
    }
  }

  if (e->flags & CMD_RECORD_USE) {
    recordUserUse(authorId, authorName);
  }

  CmdCtx ctx{ channelId, authorId, authorName, args, isDM };
  e->handler(ctx);
}
