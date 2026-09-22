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
// Usage-error replies (Usage: !weather …) are fire-and-forget: no LCD, no post-bool check — cheap chat nacks only.
static void showIfPosted(const char* label, const String& okLine2, bool posted) {
  if (posted) showTransient(label, okLine2);
  else {
    noteCmdErrorReply((String("Post fail: ") + label).c_str());
    showTransient(label, "Post fail");
  }
}

// Commands whose args are the rest of the line (multi-word). Others take one token.
static bool cmdConsumesRest(const String& cmd) {
  return cmd == "!ask" || cmd == "!display";
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
static bool tokenizeCommand(const String& content, String& cmdWord, String& args) {
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

  // Mid-line: one-word args unless cmdConsumesRest. Strip trailing punct on short args.
  if (midLine && args.length() > 0 && !cmdConsumesRest(cmdWord)) {
    int argSp = args.indexOf(' ');
    if (argSp > 0) args = args.substring(0, argSp);
    stripTrailingPunct(args);
  }
  return cmdWord.length() > 1;
}

void handleCommand(const String& content, const String& authorId, const String& authorName,
                   const String& channelId, bool isDM)
{
  if (!isDM && channelId != TARGET_CHANNEL_ID && channelId != TARGET_CHANNEL_ID1) {
    return;
  }
  String cmdWord, args;
  if (!tokenizeCommand(content, cmdWord, args)) return;

  noteBotActivity();
  // Bot:N: recordUserUse only inside known-command branches below (not !help / unknown).

  if (cmdWord == "!help") {
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
      "• `!clear` — Clears DM / mention alert flags on the LCD.";
    showIfPosted("Help", "Command Sent", sendDiscordMessage(channelId, helpMsg));
    return;
  }
  if (cmdWord == "!weather") {
    recordUserUse(authorId, authorName);
    if (args.length() == 0) {
      sendDiscordMessage(channelId, "Usage: !weather <zip>");
      return;
    }
    String zip = args;
    bool validZip = (zip.length() == 5);
    if (validZip) {
      for (unsigned i = 0; i < 5; i++) {
        char c = zip.charAt(i);
        if (c < '0' || c > '9') { validZip = false; break; }
      }
    }
    if (!validZip) {
      sendDiscordMessage(channelId, "Invalid ZIP code.");
      return;
    }
    String report;
    showTransient("Weather", "Fetching...");
    bool ok = getWeather(zip, report);
    // okLine2 = ZIP so LCD shows Weather / <zip> / Sent|Post fail|Error
    sendFetchResult(channelId, "Weather", ok, report, zip, "Sent");
    return;
  }
  if (cmdWord == "!news") {
    recordUserUse(authorId, authorName);
    runFetchCommand(channelId, "News", "Fetching...", getScienceNews);
    return;
  }
  if (cmdWord == "!physics") {
    recordUserUse(authorId, authorName);
    runFetchCommand(channelId, "Physics", "Fetching arXiv...", getPhysicsPapers);
    return;
  }
  if (cmdWord == "!apod") {
    recordUserUse(authorId, authorName);
    runFetchCommand(channelId, "APOD", "Fetching NASA...", getApod);
    return;
  }
  if (cmdWord == "!iss") {
    recordUserUse(authorId, authorName);
    runFetchCommand(channelId, "ISS", "Fetching...", getIssPosition);
    return;
  }
  if (cmdWord == "!temp") {
    recordUserUse(authorId, authorName);
    float c = 0, f = 0;
    bool had = false, fresh = false;
    dashTempSnapshot(c, f, had, fresh);
    if (fresh) {
      String msg = "Current Temp: " + String(c, 1) + "°C / " + String(f, 1) + "°F";
      showIfPosted("Temp", String(f, 1) + "F/" + String(c, 1) + "C",
                   sendDiscordMessage(channelId, msg));
    } else if (had) {
      showIfPosted("Temp", "Stale",
                   sendDiscordCmdError(channelId, "Temperature reading is stale (>30s). Sensor may be disconnected."));
    } else {
      showIfPosted("Temp", "Sensor error",
                   sendDiscordCmdError(channelId, "Temperature sensor error."));
    }
    return;
  }
  if (cmdWord == "!sys") {
    recordUserUse(authorId, authorName);
    showIfPosted("Sys", "Sent", sendDiscordMessage(channelId, getSystemInfo(), true));
    return;
  }
  if (cmdWord == "!ota") {
    if (!isOwner(authorId)) {
      if (!isDM) {
        sendDiscordMessage(channelId, "You are not allowed to use this command.");
      }
      return;
    }
    recordUserUse(authorId, authorName);
    showIfPosted("OTA", WiFi.localIP().toString(), sendDiscordMessage(channelId, otaStatusText()));
    return;
  }
  if (cmdWord == "!coredump") {
    if (!isOwner(authorId)) {
      if (!isDM) {
        sendDiscordMessage(channelId, "You are not allowed to use this command.");
      }
      return;
    }
    recordUserUse(authorId, authorName);
    String report;
    if (args.equalsIgnoreCase("clear") || args.equalsIgnoreCase("erase")) {
      bool ok = clearCoreDumpImage(report);
      showIfPosted("Coredump", ok ? "Cleared" : "Error",
                   ok ? sendDiscordMessage(channelId, report, true)
                      : sendDiscordCmdError(channelId, report, true));
      return;
    }
    showTransient("Coredump", "Reading...");
    bool ok = formatCoreDumpReport(report);
    showIfPosted("Coredump", ok ? "Sent" : "Empty",
                 ok ? sendDiscordMessage(channelId, report, true)
                    : sendDiscordCmdError(channelId, report, true));
    return;
  }
  if (cmdWord == "!time") {
    recordUserUse(authorId, authorName);
    updateLocalTime();
    char currentTime[12];
    formatLocalTimeStr(currentTime, sizeof(currentTime));
    String msg = String("🕒 Current Bot Time: ") + currentTime;
    showIfPosted("Time", currentTime, sendDiscordMessage(channelId, msg));
    return;
  }
  if (cmdWord == "!ask") {
    recordUserUse(authorId, authorName);
    if (args.length() == 0) {
      sendDiscordMessage(channelId, "Usage: !ask <question>");
      return;
    }
    if (askNeedPost) {
      sendDiscordCmdError(channelId, "DeepSeek is already answering. Try again in a moment.");
      return;
    }
    String question = args;
    askPendingQuestion = question;
    askPendingChannelId = channelId;
    askNeedPost = true;
    showTransient("DeepSeek", "Queued"); // local queue; Discord reply checked in runAskFromLoop
    return;
  }
  if (cmdWord == "!display") {
    recordUserUse(authorId, authorName);
    if (args.length() == 0) {
      sendDiscordMessage(channelId, "Usage: !display <text>");
      return;
    }
    String text = args;
    if (text.length() > 50) text = text.substring(0, 50);
    String line15 = text.substring(0, text.length() > 25 ? 25 : text.length());
    String line16 = text.length() > 25 ? text.substring(25) : "";
    showTransient(line15, line16, "", 6000); // local LCD is the feature
    // If Discord ack fails, overwrite with Post fail (local text already shown briefly).
    if (!sendDiscordMessage(channelId, "Display updated.")) {
      noteCmdErrorReply("Post fail: Display");
      showTransient("Display", "Post fail");
    }
    return;
  }
  if (cmdWord == "!servo" || cmdWord == "!clear") {
    if (!isOwner(authorId)) {
      if (!isDM) {
        sendDiscordMessage(channelId, "You are not allowed to use this command.");
      }
      return;
    }
    recordUserUse(authorId, authorName);
    if (cmdWord == "!clear") {
      clearAlertFlags();
      showIfPosted("clear", "alerts OFF",
                   sendDiscordMessage(channelId, "DM/mention alerts cleared"));
      return;
    }
    if (cmdWord == "!servo") {
      if (args.length() == 0) {
        sendDiscordMessage(channelId, "Usage: !servo <0-90>");
        return;
      }
      int angle = args.toInt();
      if (angle < 0 || angle > 90) {
        sendDiscordMessage(channelId, "Angle out of range. Allowed: 0-90 degrees.");
        return;
      }
      setServoAngle(angle);
      String msg = "Servo set to " + String(angle) + " degrees.";
      showIfPosted("Servo", String(angle) + " deg", sendDiscordMessage(channelId, msg));
      return;
    }
  }

  sendDiscordMessage(channelId, "That is not a command.");
  showTransient("Unknown", cmdWord);
}
