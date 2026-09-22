# Reference checkpoint: lcd-layout-ok

**Status:** WORKING — James confirmed 2026-09-22  
**Firmware at save:** v0.6.3

If James says **revert**, copy these files over `MiniMe_Discord_Bot_II/` (same names).

## Layout contract

**Display** (right IC chip):
- Left: metrics (MiniMe | GW | time; Bot | date; Sig+RSSI; Up/Temp; Heap; Srv; Id/Users; DM/Mention; HTTPS; Event; IP/OTA/CPU/Write/Period)
- Right: users (9px pitch, MAX_TRACKED_USERS=24)

**Log** (right IC chip):
- Left: LOG
- Right: Serial

**Left IC chip:** Light/Dark theme

Do not move users into the left window or turn the right window into SysInfo.

## Files in this folder

| File | Restores to |
|---|---|
| `display.cpp` | `MiniMe_Discord_Bot_II/display.cpp` |
| `touch.cpp` | `MiniMe_Discord_Bot_II/touch.cpp` |
