#include "minime.h"
#include <SD.h>
#include <SPI.h>

// Dedicated SPI3 (HSPI) for the SD slot. Do NOT use the default SPI / FSPI object:
// Arduino_ESP32QSPI for the AXS15231B also uses SPI2_HOST (FSPI). Calling
// SPI.begin() after setupDisplay() remuxes that host and corrupts the panel
// (wrong colors, IP flash, then crash).
static SPIClass sdSpi(HSPI);
static bool sdBusBegun = false;
static std::atomic<bool> sdOk{false};
static std::atomic<uint32_t> sdFreeMb{0};
static std::atomic<uint32_t> sdTotalMb{0};
static unsigned long lastSdRetryMs = 0;
static unsigned long lastSdStatMs = 0;

static void clearSdStats() {
  sdFreeMb.store(0);
  sdTotalMb.store(0);
}

static void refreshSdStats() {
  if (!sdOk.load()) {
    clearSdStats();
    return;
  }
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  if (total == 0) {
    // Card gone or FS confused -- drop cleanly; remount path will retry.
    SD.end();
    sdOk.store(false);
    clearSdStats();
    return;
  }
  uint64_t freeB = (total > used) ? (total - used) : 0;
  sdTotalMb.store((uint32_t)(total / (1024ULL * 1024ULL)));
  sdFreeMb.store((uint32_t)(freeB / (1024ULL * 1024ULL)));
}

static bool tryMountSd() {
  if (!sdBusBegun) {
    sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    sdBusBegun = true;
  }
  // Always end a prior FS session before begin (hot-plug / retry). Do not call
  // default SPI.begin -- that fights the LCD QSPI host.
  SD.end();
  // 10 MHz on HSPI only; keep off FSPI.
  if (!SD.begin(PIN_SD_CS, sdSpi, 10000000)) {
    sdOk.store(false);
    clearSdStats();
    return false;
  }
  uint64_t total = SD.totalBytes();
  if (total == 0) {
    SD.end();
    sdOk.store(false);
    clearSdStats();
    return false;
  }
  sdOk.store(true);
  refreshSdStats();
  return sdOk.load();
}

bool setupSdCard() {
  lastSdRetryMs = millis();
  lastSdStatMs = millis();
  if (tryMountSd()) {
    MmLog.println(F("SD: mounted (HSPI/SPI3)"));
    return true;
  }
  MmLog.println(F("SD: not detected"));
  return false;
}

void pollSdCard() {
  unsigned long now = millis();
  if (!sdOk.load()) {
    // Hot-plug retry ~ every 5 s (was 2 s) -- avoid begin thrash during boot crash loops.
    if (lastSdRetryMs == 0 || (now - lastSdRetryMs) >= 5000UL) {
      lastSdRetryMs = now;
      if (tryMountSd()) {
        MmLog.println(F("SD: mounted (HSPI/SPI3)"));
      }
    }
    return;
  }
  if (lastSdStatMs == 0 || (now - lastSdStatMs) >= 5000UL) {
    lastSdStatMs = now;
    refreshSdStats();
  }
}

bool sdCardPresent() {
  return sdOk.load();
}

void boardSdTotalsMb(uint32_t& freeMb, uint32_t& totalMb) {
  freeMb = sdFreeMb.load();
  totalMb = sdTotalMb.load();
}
