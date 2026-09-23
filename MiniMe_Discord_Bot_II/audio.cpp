#include "minime.h"
#include <math.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"

// On-module I2S speaker (NS4168 amp). Core 0 only for UI ticks.
// Touch = short decaying *tick* (not a beep). Alert beep kept for DM/@mention later.

static const uint32_t I2S_RATE = 16000;
static i2s_chan_handle_t i2sTx = nullptr;
static bool audioReady = false;

// Saved alert beep (was the louder sustained tone) — wire to DM/@mention when ready.
static const uint32_t ALERT_HZ = 1000;
static const uint32_t ALERT_MS = 45;
static const int16_t ALERT_PEAK = 22000;

static int16_t scalePeak(int16_t peak) {
  if (peak <= 0) return 0;
  const int vol = (int)uiVolPct.load();
  if (vol <= 0) return 0;
  if (vol >= 100) return peak;
  return (int16_t)(((int)peak * vol) / 100);
}

void setupAudio() {
  audioReady = false;
  i2sTx = nullptr;

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.auto_clear = true;
  esp_err_t err = i2s_new_channel(&chanCfg, &i2sTx, nullptr);
  if (err != ESP_OK || !i2sTx) {
    MmLog.println(F("I2S: new_channel failed"));
    return;
  }

  i2s_std_config_t stdCfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK_PIN,
      .ws = (gpio_num_t)I2S_LRCLK_PIN,
      .dout = (gpio_num_t)I2S_DOUT_PIN,
      .din = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };

  err = i2s_channel_init_std_mode(i2sTx, &stdCfg);
  if (err != ESP_OK) {
    MmLog.println(F("I2S: init_std failed"));
    i2s_del_channel(i2sTx);
    i2sTx = nullptr;
    return;
  }
  err = i2s_channel_enable(i2sTx);
  if (err != ESP_OK) {
    MmLog.println(F("I2S: enable failed"));
    i2s_del_channel(i2sTx);
    i2sTx = nullptr;
    return;
  }
  audioReady = true;
}

static void i2sWriteStereo(const int16_t* interleaved, size_t frames) {
  if (!i2sTx || !interleaved || frames < 1) return;
  size_t written = 0;
  i2s_channel_write(i2sTx, interleaved, frames * sizeof(int16_t) * 2, &written, pdMS_TO_TICKS(100));
}

static void i2sPadSilence() {
  int16_t z[64 * 2];
  memset(z, 0, sizeof(z));
  i2sWriteStereo(z, 64);
}

// Sustained beep with soft edges (alert / notification — not touch UI).
static void playBeepMs(uint32_t hz, uint32_t ms, int16_t peak) {
  if (!audioReady || !i2sTx || peak < 1) return;

  const size_t frames = (size_t)((I2S_RATE * (uint64_t)ms) / 1000UL);
  if (frames < 1) return;

  const size_t edge = (size_t)((I2S_RATE * 2UL) / 1000UL);
  const size_t edgeUse = (edge > 0 && edge * 2 < frames) ? edge : 0;

  int16_t buf[64 * 2];
  size_t done = 0;
  while (done < frames) {
    size_t chunk = frames - done;
    if (chunk > 64) chunk = 64;
    for (size_t i = 0; i < chunk; i++) {
      const size_t n = done + i;
      float env = 1.0f;
      if (edgeUse) {
        if (n < edgeUse) env = (float)n / (float)edgeUse;
        else if (n + edgeUse >= frames) env = (float)(frames - 1 - n) / (float)edgeUse;
      }
      const float ph = 2.0f * (float)M_PI * (float)hz * (float)n / (float)I2S_RATE;
      const int16_t s = (int16_t)(sinf(ph) * (float)peak * env);
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
    }
    i2sWriteStereo(buf, chunk);
    done += chunk;
  }
  i2sPadSilence();
}

// UI tick: very short + exponential decay (reads as a tick, not a beep).
static void playTickClick(uint32_t hz, uint32_t ms, int16_t peak) {
  if (!audioReady || !i2sTx || peak < 1) return;

  const size_t frames = (size_t)((I2S_RATE * (uint64_t)ms) / 1000UL);
  if (frames < 1) return;

  // ~2.5 ms decay constant — energy dies fast so it does not sing.
  const float tau = (float)I2S_RATE * 0.0025f;
  int16_t buf[64 * 2];
  size_t done = 0;
  while (done < frames) {
    size_t chunk = frames - done;
    if (chunk > 64) chunk = 64;
    for (size_t i = 0; i < chunk; i++) {
      const size_t n = done + i;
      const float env = expf(-(float)n / tau);
      const float ph = 2.0f * (float)M_PI * (float)hz * (float)n / (float)I2S_RATE;
      const int16_t s = (int16_t)(sinf(ph) * (float)peak * env);
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
    }
    i2sWriteStereo(buf, chunk);
    done += chunk;
  }
  i2sPadSilence();
}

void audioTickWake() {
  // Soft lower tick — any touch while backlight is asleep.
  if (!uiSoundOn.load() || !uiTicksOn.load()) return;
  playTickClick(1600, 10, scalePeak(7000));
}

void audioTickButton() {
  // Slightly brighter tick — theme / layout / logo / controls while awake.
  if (!uiSoundOn.load() || !uiTicksOn.load()) return;
  playTickClick(2400, 12, scalePeak(8500));
}

void audioAlertBeep() {
  // Saved louder sustained tone (single beep) — available for other cues.
  if (!uiSoundOn.load()) return;
  playBeepMs(ALERT_HZ, ALERT_MS, scalePeak(ALERT_PEAK));
}

void audioAlarmBeep() {
  // Distinct from UI ticks and from audioAlertBeep — two-note alarm chirp.
  if (!uiSoundOn.load() || !uiNotifyOn.load()) return;
  playBeepMs(880, 70, scalePeak(17000));
  delay(35);
  playBeepMs(1320, 90, scalePeak(19000));
}

void pollAudioAlerts() {
  // Core 0 only (I2S). Sticky until owner !clear clears alertDm / alertMention.
  static unsigned long lastAlarmMs = 0;
  if (!uiSoundOn.load() || !uiNotifyOn.load()) {
    lastAlarmMs = 0;
    return;
  }
  const bool active = alertDm.load() || alertMention.load();
  if (!active) {
    lastAlarmMs = 0;
    return;
  }
  const unsigned long now = millis();
  if (lastAlarmMs != 0 && (now - lastAlarmMs) < ALERT_SOUND_PERIOD_MS) return;
  lastAlarmMs = now;
  audioAlarmBeep();
}
