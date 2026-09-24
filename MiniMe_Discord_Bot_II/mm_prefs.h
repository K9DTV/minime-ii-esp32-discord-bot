#ifndef MM_PREFS_H
#define MM_PREFS_H

#include <Arduino.h>

// Controls prefs in ESP32-S3 onboard flash (partition "prefs" in partitions.csv).
// Same rules as VFO settings: dirty check, signature/version/size/tailMagic/CRC32,
// dual A/B sectors, corrupt/missing -> max defaults then seed flash.
// Not 47L16 / external EERAM.

bool isSettingsDirty();
void loadSettings();   // boot: read best valid slot or defaults
bool saveSettings();   // Save: write only if dirty
bool recallSettings(); // Cancel: flash recall; early-out if not dirty
bool factoryResetSettings(); // defaults + write flash (long-press Cancel / !resetprefs)

#endif
