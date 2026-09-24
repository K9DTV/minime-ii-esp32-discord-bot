#include "minime.h"
#include "mm_prefs.h"
#include <esp_partition.h>
#include <atomic>

// ESP32-S3 onboard flash prefs for Controls (rules from Vfo memory.cpp; storage is
// chip flash via esp_partition -- not 47L16 I2C EERAM).
// Partition label "prefs": 8 KB = two 4 KB erase sectors (slot A / slot B).

static const uint32_t SETTINGS_SIGNATURE  = 0x4D4D4932u; // "MMI2"
static const uint16_t SETTINGS_VERSION    = 1;
static const uint32_t SETTINGS_TAIL_MAGIC = 0xCAFEBABEu;
static const size_t   SETTINGS_SLOT_SIZE  = 0x1000; // one flash erase sector
static const char*    PREFS_PART_LABEL    = "prefs";

extern std::atomic<bool> dashForceFull;

struct __attribute__((packed)) Settings {
  uint32_t signature;
  uint16_t version;
  uint16_t structSize;
  uint32_t sequence;
  uint8_t  themeLight; // LCD Light/Dark (web theme stays localStorage-only)
  uint8_t  brightPct;  // 0..100
  uint8_t  volPct;     // 0..100
  uint8_t  soundOn;
  uint8_t  ticksOn;
  uint8_t  notifyOn;
  uint16_t reserved;
  uint32_t tailMagic;
  uint32_t crc32;
};

static_assert(sizeof(Settings) <= SETTINGS_SLOT_SIZE, "Settings must fit in one prefs flash sector");

static Settings settings;
static const esp_partition_t* prefsPart = nullptr;

static uint32_t crc32_update(uint32_t crc, uint8_t data) {
  crc ^= data;
  for (int i = 0; i < 8; i++)
    crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
  return crc;
}

static uint32_t crc32_compute(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) crc = crc32_update(crc, data[i]);
  return crc ^ 0xFFFFFFFFu;
}

static void clampSettings(Settings& s) {
  if (s.brightPct > 100) s.brightPct = 100;
  if (s.volPct > 100) s.volPct = 100;
  s.themeLight = s.themeLight ? 1 : 0;
  s.soundOn = s.soundOn ? 1 : 0;
  s.ticksOn = s.ticksOn ? 1 : 0;
  s.notifyOn = s.notifyOn ? 1 : 0;
}

static bool validateSettings(const Settings* s) {
  if (s->signature != SETTINGS_SIGNATURE) return false;
  if (s->version != SETTINGS_VERSION) return false;
  if (s->structSize != sizeof(Settings)) return false;
  if (s->tailMagic != SETTINGS_TAIL_MAGIC) return false;

  const size_t len = sizeof(Settings) - sizeof(uint32_t);
  if (crc32_compute((const uint8_t*)s, len) != s->crc32) return false;

  if (s->brightPct > 100 || s->volPct > 100) return false;
  return true;
}

static const esp_partition_t* findPrefsPart() {
  if (prefsPart) return prefsPart;
  prefsPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40,
                                       PREFS_PART_LABEL);
  if (!prefsPart) {
    MmLog.println(F("[prefs] partition \"prefs\" missing"));
  }
  return prefsPart;
}

static bool readSlot(uint8_t slot, Settings* out) {
  const esp_partition_t* p = findPrefsPart();
  if (!p || slot > 1) return false;
  if (p->size < SETTINGS_SLOT_SIZE * 2) return false;
  const size_t off = (size_t)slot * SETTINGS_SLOT_SIZE;
  return esp_partition_read(p, off, out, sizeof(Settings)) == ESP_OK;
}

static bool writeSlot(uint8_t slot, const Settings* src) {
  const esp_partition_t* p = findPrefsPart();
  if (!p || slot > 1) return false;
  if (p->size < SETTINGS_SLOT_SIZE * 2) return false;
  const size_t off = (size_t)slot * SETTINGS_SLOT_SIZE;
  if (esp_partition_erase_range(p, off, SETTINGS_SLOT_SIZE) != ESP_OK) return false;
  return esp_partition_write(p, off, src, sizeof(Settings)) == ESP_OK;
}

static void fillDefaults(Settings& s) {
  s.signature = SETTINGS_SIGNATURE;
  s.version = SETTINGS_VERSION;
  s.structSize = (uint16_t)sizeof(Settings);
  s.sequence = 0;
  s.themeLight = 0; // Dark
  s.brightPct = 100;
  s.volPct = 100;
  s.soundOn = 1;
  s.ticksOn = 1;
  s.notifyOn = 1;
  s.reserved = 0;
  s.tailMagic = SETTINGS_TAIL_MAGIC;
  const size_t len = sizeof(Settings) - sizeof(uint32_t);
  s.crc32 = crc32_compute((const uint8_t*)&s, len);
}

static void applySettingsToRuntime(const Settings& s) {
  setLcdThemeLight(s.themeLight != 0);
  uiBrightPct.store(s.brightPct);
  uiVolPct.store(s.volPct);
  uiSoundOn.store(s.soundOn != 0);
  uiTicksOn.store(s.ticksOn != 0);
  uiNotifyOn.store(s.notifyOn != 0);
  applyBacklightFromSettings();
  uiControlsGen.fetch_add(1);
  lastDashMillis = 0;
  dashForceFull.store(true);
}

static void captureRuntimeToSettings(Settings& s) {
  s.signature = SETTINGS_SIGNATURE;
  s.version = SETTINGS_VERSION;
  s.structSize = (uint16_t)sizeof(Settings);
  s.themeLight = lcdThemeLight ? 1 : 0;
  s.brightPct = uiBrightPct.load();
  s.volPct = uiVolPct.load();
  s.soundOn = uiSoundOn.load() ? 1 : 0;
  s.ticksOn = uiTicksOn.load() ? 1 : 0;
  s.notifyOn = uiNotifyOn.load() ? 1 : 0;
  s.reserved = 0;
  s.tailMagic = SETTINGS_TAIL_MAGIC;
  clampSettings(s);
  const size_t len = sizeof(Settings) - sizeof(uint32_t);
  s.crc32 = crc32_compute((const uint8_t*)&s, len);
}

bool isSettingsDirty() {
  return (lcdThemeLight != (settings.themeLight != 0) ||
          uiBrightPct.load() != settings.brightPct ||
          uiVolPct.load() != settings.volPct ||
          uiSoundOn.load() != (settings.soundOn != 0) ||
          uiTicksOn.load() != (settings.ticksOn != 0) ||
          uiNotifyOn.load() != (settings.notifyOn != 0));
}

void loadSettings() {
  Settings a, b;
  bool okA = readSlot(0, &a) && validateSettings(&a);
  bool okB = readSlot(1, &b) && validateSettings(&b);

  if (okA && okB) {
    settings = (a.sequence >= b.sequence) ? a : b;
  } else if (okA) {
    settings = a;
  } else if (okB) {
    settings = b;
  } else {
    MmLog.println(F("[prefs] corrupt or empty -- defaults"));
    fillDefaults(settings);
    writeSlot(0, &settings);
  }

  clampSettings(settings);
  applySettingsToRuntime(settings);
  MmLog.print(F("[prefs] loaded seq="));
  MmLog.println(settings.sequence);
}

bool recallSettings() {
  Settings a, b;
  bool okA = readSlot(0, &a) && validateSettings(&a);
  bool okB = readSlot(1, &b) && validateSettings(&b);

  if (!okA && !okB) {
    MmLog.println(F("[prefs] recall failed -- defaults"));
    fillDefaults(settings);
    writeSlot(0, &settings);
    applySettingsToRuntime(settings);
    return false;
  }

  if (okA && okB) settings = (a.sequence >= b.sequence) ? a : b;
  else if (okA) settings = a;
  else settings = b;

  clampSettings(settings);
  applySettingsToRuntime(settings);
  return true;
}

bool factoryResetSettings() {
  Settings a, b;
  const bool okA = readSlot(0, &a) && validateSettings(&a);
  const bool okB = readSlot(1, &b) && validateSettings(&b);
  uint32_t seq = settings.sequence;
  if (okA && a.sequence > seq) seq = a.sequence;
  if (okB && b.sequence > seq) seq = b.sequence;

  fillDefaults(settings);
  settings.sequence = seq + 1;
  {
    const size_t len = sizeof(Settings) - sizeof(uint32_t);
    settings.crc32 = crc32_compute((const uint8_t*)&settings, len);
  }

  uint8_t curSlot = 0;
  if (okA && okB) curSlot = (a.sequence >= b.sequence) ? 0 : 1;
  else if (okB && !okA) curSlot = 1;
  const uint8_t dst = (okA || okB) ? (uint8_t)(curSlot ^ 1) : 0;

  if (!writeSlot(dst, &settings)) {
    MmLog.println(F("[prefs] factory reset write failed"));
    applySettingsToRuntime(settings); // still apply RAM defaults
    return false;
  }

  applySettingsToRuntime(settings);
  MmLog.print(F("[prefs] factory reset seq="));
  MmLog.print(settings.sequence);
  MmLog.print(F(" slot="));
  MmLog.println(dst);
  return true;
}

bool saveSettings() {
  if (!isSettingsDirty()) return false;

  Settings next;
  captureRuntimeToSettings(next);
  next.sequence = settings.sequence + 1;
  clampSettings(next);
  {
    const size_t len = sizeof(Settings) - sizeof(uint32_t);
    next.crc32 = crc32_compute((const uint8_t*)&next, len);
  }

  uint8_t curSlot = 0;
  Settings curA, curB;
  bool okA = readSlot(0, &curA) && validateSettings(&curA);
  bool okB = readSlot(1, &curB) && validateSettings(&curB);
  if (okA && okB) curSlot = (curA.sequence >= curB.sequence) ? 0 : 1;
  else if (okB && !okA) curSlot = 1;
  else curSlot = 0;
  const uint8_t dst = curSlot ^ 1;

  if (!writeSlot(dst, &next)) {
    MmLog.println(F("[prefs] write failed"));
    return false;
  }

  settings = next;
  MmLog.print(F("[prefs] saved seq="));
  MmLog.print(settings.sequence);
  MmLog.print(F(" slot="));
  MmLog.println(dst);
  return true;
}
