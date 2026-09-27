#include "minime.h"
#include "cores.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static TaskHandle_t uiTaskHandle = nullptr;
static SemaphoreHandle_t cmdMux = nullptr;
static bool drainCmdsBusy = false;
// millis() when drainCmdsBusy was last armed. Unsigned subtract wraps safely.
static uint32_t drainCmdsBusyAt = 0;
// Bumped each time a guard arms. The destructor clears the flag only for its
// own epoch, so a late outer destructor cannot drop a newer drain after reclaim.
static uint32_t drainBusyEpoch = 0;

static DiscordCmdJob cmdQ[DISCORD_CMD_QUEUE_DEPTH];
static uint8_t cmdHead = 0;
static uint8_t cmdTail = 0;
static uint8_t cmdCount = 0;

// loopTask TWDT is TWDT_TIMEOUT_MS (90s) and is fed only when loop() returns.
// Keep this strictly under that. Panic/abort on ESP32 Arduino does not unwind
// ~DrainBusyGuard; drainDiscordCmds() reclaims a stale flag instead.
static const uint32_t DRAIN_BUSY_STALE_MS = 75000UL;
static_assert(DRAIN_BUSY_STALE_MS >= 60000UL, "drain stale window too short");
static_assert(DRAIN_BUSY_STALE_MS < TWDT_TIMEOUT_MS, "drain reclaim must beat loop TWDT");

// Sets drainCmdsBusy and stamps drainCmdsBusyAt. Clears the flag on every normal
// return. Stale reclaim covers the no-unwind path (CODE_REVIEW_NOTES 0.7.38 / 0.8.5).
struct DrainBusyGuard {
  bool& flag;
  uint32_t epoch;
  explicit DrainBusyGuard(bool& f) : flag(f), epoch(++drainBusyEpoch) {
    flag = true;
    drainCmdsBusyAt = millis();
  }
  ~DrainBusyGuard() {
    if (epoch == drainBusyEpoch) flag = false;
  }
  DrainBusyGuard(const DrainBusyGuard&) = delete;
  DrainBusyGuard& operator=(const DrainBusyGuard&) = delete;
};

static void copyTrunc(char* dst, size_t dstLen, const String& src) {
  if (!dst || dstLen == 0) return;
  size_t n = src.length();
  if (n >= dstLen) n = dstLen - 1;
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

bool enqueueDiscordCmd(const String& content, const String& authorId,
                       const String& authorName, const String& channelId, bool isDM) {
  if (!cmdMux) return false;
  if (xSemaphoreTake(cmdMux, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  bool ok = false;
  if (cmdCount < DISCORD_CMD_QUEUE_DEPTH) {
    DiscordCmdJob& j = cmdQ[cmdTail];
    copyTrunc(j.content, sizeof(j.content), content);
    copyTrunc(j.channelId, sizeof(j.channelId), channelId);
    copyTrunc(j.authorId, sizeof(j.authorId), authorId);
    copyTrunc(j.authorName, sizeof(j.authorName), authorName);
    j.isDM = isDM;
    cmdTail = (uint8_t)((cmdTail + 1) % DISCORD_CMD_QUEUE_DEPTH);
    cmdCount++;
    ok = true;
  }
  xSemaphoreGive(cmdMux);
  return ok;
}

void drainDiscordCmds() {
  if (!cmdMux) return;
  if (drainCmdsBusy) {
    uint32_t age = (uint32_t)millis() - drainCmdsBusyAt;
    if (age <= DRAIN_BUSY_STALE_MS) return;
    // No-unwind safety net. Nested pumpNetWait() returns above while this job's
    // stamp is still inside DRAIN_BUSY_STALE_MS.
    drainCmdsBusy = false;
    MmLog.print(F("[CMD] drainCmdsBusy stale ms="));
    MmLog.println(age);
  }
  DrainBusyGuard guard(drainCmdsBusy);
  // At most 2 jobs per call. DeepSeek uses a dedicated TLS client, so httpsInUse stays
  // false during !ask and Discord/other HTTPS cmds can run. While shared httpsClient is
  // held, skip drain here (caller also gates) -- avoids mid-fetch "busy" Discord spam.
  for (uint8_t n = 0; n < 2; n++) {
    if (httpsInUse) break;
    DiscordCmdJob job;
    bool have = false;
    if (xSemaphoreTake(cmdMux, pdMS_TO_TICKS(5)) == pdTRUE) {
      if (cmdCount > 0) {
        job = cmdQ[cmdHead];
        cmdHead = (uint8_t)((cmdHead + 1) % DISCORD_CMD_QUEUE_DEPTH);
        cmdCount--;
        have = true;
      }
      xSemaphoreGive(cmdMux);
    }
    if (!have) break;
    // Each job gets its own stale window. Aging from the first job would let a nested
    // pumpNetWait() reclaim during a second healthy command (httpsInUse is false for !ask
    // and for Discord 429 waits).
    drainCmdsBusyAt = millis();
    handleCommand(String(job.content), String(job.authorId), String(job.authorName),
                  String(job.channelId), job.isDM);
  }
}

static void uiTask(void* /*arg*/) {
  for (;;) {
    if (!otaIsBusy()) {
      pollTouchWake();
      pollAudioAlerts();
      updateDisplay();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void startUiCore() {
  if (uiTaskHandle) return;
  cmdMux = xSemaphoreCreateMutex();
  BaseType_t ok = xTaskCreatePinnedToCore(uiTask, "ui", 12288, nullptr, 1, &uiTaskHandle, 0);
  if (ok != pdPASS) {
    uiTaskHandle = nullptr;
    MmLog.println(F("uiTask create failed"));
  }
}
