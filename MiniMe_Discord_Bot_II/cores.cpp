#include "minime.h"
#include "cores.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static TaskHandle_t uiTaskHandle = nullptr;
static SemaphoreHandle_t cmdMux = nullptr;
static bool drainCmdsBusy = false;

static DiscordCmdJob cmdQ[DISCORD_CMD_QUEUE_DEPTH];
static uint8_t cmdHead = 0;
static uint8_t cmdTail = 0;
static uint8_t cmdCount = 0;

// Clears drainCmdsBusy on every return path (not on panic — noted in CODE_REVIEW_NOTES).
struct DrainBusyGuard {
  bool& flag;
  explicit DrainBusyGuard(bool& f) : flag(f) { flag = true; }
  ~DrainBusyGuard() { flag = false; }
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
  if (drainCmdsBusy) return;
  DrainBusyGuard guard(drainCmdsBusy);
  // At most 2 jobs per call. DeepSeek uses a dedicated TLS client, so httpsInUse stays
  // false during !ask and Discord/other HTTPS cmds can run. While shared httpsClient is
  // held, skip drain here (caller also gates) — avoids mid-fetch "busy" Discord spam.
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
    handleCommand(String(job.content), String(job.authorId), String(job.authorName),
                  String(job.channelId), job.isDM);
  }
}

static void uiTask(void* /*arg*/) {
  for (;;) {
    if (!otaIsBusy()) {
      pollTouchWake();
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
