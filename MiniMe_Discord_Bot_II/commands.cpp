#include "minime.h"

bool askNeedPost = false;
String askPendingQuestion;
String askPendingChannelId;

String collapseWhitespace(String s) {
  s.replace("\n", " ");
  s.replace("\r", " ");
  s.replace("\t", " ");
  while (s.indexOf("  ") >= 0) {
    s.replace("  ", " ");
  }
  s.trim();
  return s;
}

String truncateText(const String& s, int maxLen) {
  if (s.length() <= maxLen) return s;
  return s.substring(0, maxLen - 3) + "...";
}

typedef bool (*FetchReportFn)(String&);

static void sendFetchResult(const String& channelId, const char* label, bool ok, const String& report,
                            const String& okLine2 = "Sent", const String& okLine3 = "") {
  if (!ok) {
    sendDiscordCmdError(channelId, report);
    showTransient(label, "Error");
    return;
  }
  const bool posted = sendDiscordMessage(channelId, report);
  if (posted) showTransient(label, okLine2, okLine3);
  else {
    noteCmdErrorReply((String("Post fail: ") + label).c_str());
    showTransient(label, "Post fail");
  }
}

static void runFetchCommand(const String& channelId, const char* label, const char* fetching,
                            FetchReportFn fetch) {
  String report;
  showTransient(label, fetching);
  sendFetchResult(channelId, label, fetch(report), report);
}

// LCD value only if Discord accepted the post (same rule as sendFetchResult / !help).
// Usage-error replies (Usage: !weather ...) are fire-and-forget: no LCD, no post-bool check.
static void showIfPosted(const char* label, const String& okLine2, bool posted) {
  if (posted) showTransient(label, okLine2);
  else {
    noteCmdErrorReply((String("Post fail: ") + label).c_str());
    showTransient(label, "Post fail");
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
  String helpMsg =
    "🤖 **MiniMe Bot Commands**\n\n"
    "**👤 Public Commands:**\n"
    "• `!apod` — NASA Astronomy Picture of the Day.\n"
    "• `!ask <question>` — Asks DeepSeek (text AI reply in chat).\n"
    "• `!display <text>` — Writes custom text to the LCD screen.\n"
    "• `!help` — Shows this command list.\n"
    "• `!iss` — Current International Space Station position.\n"
    "• `!news` — Space and high-tech science headlines.\n"
    "• `!physics` — Latest arXiv physics papers.\n"
    "• `!sys` — System diagnostics (uptime, internal heap, PSRAM, RSSI, gateway, firmware URL).\n"
    "• `!temp` — Reads the current indoor temperature sensor.\n"
    "• `!time` — Displays the current bot time.\n"
    "• `!weather <zip>` — Fetches the weather report for a US ZIP code.\n\n"
    "**👑 Owner-Only Commands:**\n"
    "• `!ota` — Wi-Fi firmware update info (IP / hostname).\n"
    "• `!coredump` — Last panic from flash coredump (`!coredump clear` erases).\n"
    "• `!servo <0-90>` — Moves the servo motor to a specific angle.\n"
    "• `!clear` — Clears DM / mention alert flags on the LCD (stops the alarm sound).";
  showIfPosted("Help", "Command Sent", sendDiscordMessage(ctx.channelId, helpMsg));
}

static void cmdWeather(const CmdCtx& ctx) {
  if (ctx.args.length() == 0) {
    sendDiscordMessage(ctx.channelId, "Usage: !weather <zip>");
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
    sendDiscordMessage(ctx.channelId, "Invalid ZIP code.");
    return;
  }
  String report;
  showTransient("Weather", "Fetching...");
  bool ok = getWeather(zip, report);
  sendFetchResult(ctx.channelId, "Weather", ok, report, zip, "Sent");
}

static void cmdNews(const CmdCtx& ctx) {
  runFetchCommand(ctx.channelId, "News", "Fetching...", getScienceNews);
}

static void cmdPhysics(const CmdCtx& ctx) {
  runFetchCommand(ctx.channelId, "Physics", "Fetching arXiv...", getPhysicsPapers);
}

static void cmdApod(const CmdCtx& ctx) {
  runFetchCommand(ctx.channelId, "APOD", "Fetching NASA...", getApod);
}

static void cmdIss(const CmdCtx& ctx) {
  runFetchCommand(ctx.channelId, "ISS", "Fetching...", getIssPosition);
}

static void cmdTemp(const CmdCtx& ctx) {
  float c = 0, f = 0;
  bool had = false, fresh = false;
  dashTempSnapshot(c, f, had, fresh);
  if (fresh) {
    String msg = "Current Temp: " + String(c, 1) + "°C / " + String(f, 1) + "°F";
    showIfPosted("Temp", String(f, 1) + "F/" + String(c, 1) + "C",
                 sendDiscordMessage(ctx.channelId, msg));
  } else if (had) {
    showIfPosted("Temp", "Stale",
                 sendDiscordCmdError(ctx.channelId, "Temperature reading is stale (>30s). Sensor may be disconnected."));
  } else {
    showIfPosted("Temp", "Sensor error",
                 sendDiscordCmdError(ctx.channelId, "Temperature sensor error."));
  }
}

static void cmdSys(const CmdCtx& ctx) {
  showIfPosted("Sys", "Sent", sendDiscordMessage(ctx.channelId, getSystemInfo(), true));
}

static void cmdOta(const CmdCtx& ctx) {
  showIfPosted("OTA", WiFi.localIP().toString(), sendDiscordMessage(ctx.channelId, otaStatusText()));
}

static void cmdCoredump(const CmdCtx& ctx) {
  String report;
  if (ctx.args.equalsIgnoreCase("clear") || ctx.args.equalsIgnoreCase("erase")) {
    bool ok = clearCoreDumpImage(report);
    showIfPosted("Coredump", ok ? "Cleared" : "Error",
                 ok ? sendDiscordMessage(ctx.channelId, report, true)
                    : sendDiscordCmdError(ctx.channelId, report, true));
    return;
  }
  showTransient("Coredump", "Reading...");
  bool ok = formatCoreDumpReport(report);
  showIfPosted("Coredump", ok ? "Sent" : "Empty",
               ok ? sendDiscordMessage(ctx.channelId, report, true)
                  : sendDiscordCmdError(ctx.channelId, report, true));
}

static void cmdTime(const CmdCtx& ctx) {
  updateLocalTime();
  char currentTime[12];
  formatLocalTimeStr(currentTime, sizeof(currentTime));
  String msg = String("🕒 Current Bot Time: ") + currentTime;
  showIfPosted("Time", currentTime, sendDiscordMessage(ctx.channelId, msg));
}

static void cmdAsk(const CmdCtx& ctx) {
  if (ctx.args.length() == 0) {
    sendDiscordMessage(ctx.channelId, "Usage: !ask <question>");
    return;
  }
  if (askNeedPost) {
    sendDiscordCmdError(ctx.channelId, "DeepSeek is already answering. Try again in a moment.");
    return;
  }
  askPendingQuestion = ctx.args;
  askPendingChannelId = ctx.channelId;
  askNeedPost = true;
  showTransient("DeepSeek", "Queued"); // local queue; Discord reply checked in runAskFromLoop
}

static void cmdDisplay(const CmdCtx& ctx) {
  if (ctx.args.length() == 0) {
    sendDiscordMessage(ctx.channelId, "Usage: !display <text>");
    return;
  }
  String text = ctx.args;
  if (text.length() > 50) text = text.substring(0, 50);
  String line15 = text.substring(0, text.length() > 25 ? 25 : text.length());
  String line16 = text.length() > 25 ? text.substring(25) : "";
  showTransient(line15, line16, "", 6000); // local LCD is the feature
  if (!sendDiscordMessage(ctx.channelId, "Display updated.")) {
    noteCmdErrorReply("Post fail: Display");
    showTransient("Display", "Post fail");
  }
}

static void cmdClear(const CmdCtx& ctx) {
  clearAlertFlags();
  showIfPosted("clear", "alerts OFF",
               sendDiscordMessage(ctx.channelId, "DM/mention alerts cleared"));
}

static void cmdServo(const CmdCtx& ctx) {
  if (ctx.args.length() == 0) {
    sendDiscordMessage(ctx.channelId, "Usage: !servo <0-90>");
    return;
  }
  int angle = ctx.args.toInt();
  if (angle < 0 || angle > 90) {
    sendDiscordMessage(ctx.channelId, "Angle out of range. Allowed: 0-90 degrees.");
    return;
  }
  setServoAngle(angle);
  String msg = "Servo set to " + String(angle) + " degrees.";
  showIfPosted("Servo", String(angle) + " deg", sendDiscordMessage(ctx.channelId, msg));
}

#ifdef MINIME_TEST_TWDT
// Scratch only: never returns so Core 1 loop() stops feeding TWDT (~90 s panic).
static void cmdHang(const CmdCtx&) {
  showTransient("TWDT", "hang...");
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
  { "!display",  CMD_CONSUMES_REST | CMD_RECORD_USE, cmdDisplay },
  { "!clear",    CMD_OWNER | CMD_RECORD_USE,         cmdClear },
  { "!servo",    CMD_OWNER | CMD_RECORD_USE,         cmdServo },
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
    showTransient("Unknown", cmdWord);
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
