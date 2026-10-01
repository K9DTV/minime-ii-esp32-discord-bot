# MiniMe II -- HIL soak results

Attested local soaks (GitHub Actions cannot reach the board). Playbook: [`HIL_SOAK.md`](HIL_SOAK.md). Gateway PASS evidence: [`lan-monitor-gateway-pass-20260928.log`](lan-monitor-gateway-pass-20260928.log). Final LAN archive for the post-reboot daytime soak: [`lan-monitor-soak-final-20260928-1423.log`](lan-monitor-soak-final-20260928-1423.log). Live JSON: [`lan-status-snapshot.json`](lan-status-snapshot.json).

## 2026-10-01 -- v1.01.00 afternoon LAN soak (2 s poll) -- **PASS** (operator stop)

| Field | Value |
|---|---|
| Firmware | **v1.01.00** |
| Monitor | `lan-monitor-auth.ps1` pid **28372**, interval **2 s** |
| Start | **2026-10-01T14:19:25** PT |
| Stop | **2026-10-01T15:07:25** PT (operator request) |
| Wall duration | ~**48 m** |
| Board up (last poll) | **0d 0h 54m 48s** at **15:07:22** (`gw=True`, rssi=-41, heapPct=13, presence=idle) |
| Final archive | [`lan-monitor-soak-final-20261001-1507.log`](lan-monitor-soak-final-20261001-1507.log) |
| Board | Guition @ `http://192.168.68.60` |
| Gateway | **Good** at stop; two brief `GW_DOWN` blips recovered in **4 s** and **2 s** (no gw=false >=60 s) |
| Result | **PASS** -- operator stop; no MONITOR_STOP from gw rule; two FETCH_FAIL (LAN only, recovered) |

### Abnormals (all recovered)

| When (PT) | Event | Recovery |
|---|---|---|
| 14:38:50 / 14:38:59 | FETCH_FAIL timeout | next polls gw=True |
| 14:48:31 | FAIL_START GW_DOWN (`WS_DISCONNECTED_WIFI_UP`) | FAIL_CLEARED after **4 s** |
| 14:48:38 | FAIL_START GW_DOWN | FAIL_CLEARED after **2 s** |

### Notes

- Monitor stopped by operator request (not by gw=false >=60 s rule).
- Poll rate was **2 seconds**.
- Full 2 s stream stays local in gitignored `docs/lan-monitor.log`.
- Prior same-day overnight v1.00.01 soak (stop 12:02) remains attested above/below as its own section.

---
## 2026-09-30 to 2026-10-01 -- v1.00.01 overnight/daytime LAN soak (2 s poll) -- **PASS** (operator stop)

| Field | Value |
|---|---|
| Firmware | **v1.00.01** |
| Monitor | `lan-monitor-auth.ps1` pid **25100**, interval **2 s** |
| Start | **2026-09-30T12:45:46** PT |
| Stop | **2026-10-01T12:02:04** PT (operator request) |
| Wall duration | ~**23 h 16 m** |
| Board up (last poll) | **0d 23h 27m 4s** at **12:02:03** (`gw=True`, rssi=-40, heapPct=15, presence=idle) |
| Final archive | [`lan-monitor-soak-final-20261001-1202.log`](lan-monitor-soak-final-20261001-1202.log) |
| Prior partials | interim `...-0607.log`; compact `...-0423.log`, `...-1023.log` |
| Board | Guition @ `http://192.168.68.60` |
| Gateway | **Good** whenever polled (`gw=True`); no lasting `gw=False` |
| Result | **PASS** -- no MONITOR_STOP; no gw=false >=60 s; nine brief LAN FETCH_FAIL only (all recovered) |

### Abnormals (all recovered)

| When (PT) | Event | Recovery |
|---|---|---|
| 2026-09-30 13:02:16 | FETCH_FAIL timeout | next poll gw=True |
| 2026-09-30 15:08:15 | FETCH_FAIL timeout | next poll gw=True |
| 2026-09-30 16:32:15 | FETCH_FAIL timeout | next poll gw=True |
| 2026-09-30 17:02:16 | FETCH_FAIL timeout | next poll gw=True |
| 2026-09-30 19:23:14 | FETCH_FAIL timeout | next poll gw=True |
| 2026-09-30 19:29:14 | FETCH_FAIL timeout | next poll gw=True |
| 2026-10-01 11:22:08 | FETCH_FAIL timeout | (paired with next) |
| 2026-10-01 11:22:15 | FETCH_FAIL timeout | recovered ~11:22:22 |
| 2026-10-01 11:28:57 | FETCH_FAIL timeout | recovered ~11:29:04 |

After the 10:23 PT partial: three new FETCH_FAIL only (11:22-11:28); no lasting gw=False, no GW_REBIND, no uptime reset.

### Notes

- Monitor stopped by operator request at end of soak (not by gw=false rule).
- Poll rate was **2 seconds** throughout.
- Full 2 s status stream stays local in gitignored `docs/lan-monitor.log`.
- GitHub artifacts are compact: poll rate noted; only abnormal events listed.
- `FETCH_FAIL` is LAN HTTP timeout to `/api/status`, not a Discord drop; each recovered on the next poll(s).
- Presence idle vs online is Discord/LCD state, not gateway down.
- No `GW_REBIND` line in this LAN soak (rebind path not exercised or not visible on status poll).

---

## 2026-09-28 -- v1.00.01 daytime soak (08:22 start) -- **PASS** (operator stop)

| Field | Value |
|---|---|
| Firmware | **v1.00.01** |
| Monitor | `lan-monitor-auth.ps1` pid **31712**, interval 30 s |
| Start | **2026-09-28T08:22:16** PT |
| Stop | **14:23:00** PT (operator) |
| Archive | [`lan-monitor-soak-final-20260928-1423.log`](lan-monitor-soak-final-20260928-1423.log) |
| Board up (last good poll) | **0d 4h 5m 42s** at **12:25:17** (`gw=True`, ver=1.00.01) |
| Gateway | **Good** whenever polled; READY stamps **71707**, **213202** (tls_headroom recovers), **7999010**, **14422831** |
| Result | **PASS** -- no MONITOR_STOP; no gw=false >=60 s; tls_headroom drops recovered |

### lan-monitor.log (final)

```
=== MiniMe II monitor start 2026-09-28T08:22:16 ... interval=30s ===
[08:22:16] LOGIN_OK gw=True botOnline=True up=0d 0h 2m 41s ver=1.00.01
[08:22:16] LOG [71707] READY session=yes
[08:23:46] LOG [213202] READY session=yes
[08:28:18] gw=True botOnline=False cpu=160 up=0d 0h 8m 42s
[10:33:09] gw=True botOnline=True cpu=240 up=0d 2h 13m 34s
[10:33:39] LOG [7999010] READY session=yes
[10:38:10] gw=True botOnline=False cpu=160 up=0d 2h 18m 35s lcd=asleep
[11:58:05] FETCH_FAIL timeout (LAN only)
[12:20:16] gw=True botOnline=True cpu=240 up=0d 4h 0m 41s
[12:20:46] LOG [14422831] READY session=yes
[12:25:17] gw=True botOnline=False cpu=160 up=0d 4h 5m 42s lcd=asleep
=== MONITOR_STOP operator 14:23:00 ===
```

Notes: long gaps in the LAN log are hung HTTP polls (process stayed alive). `FETCH_FAIL` is LAN-only. Board LOG earlier same boot documented intentional `tls_headroom` clears that recovered to READY.

---
## 2026-09-28 -- v1.00.01 Gateway soak -- **PASS**

| Field | Value |
|---|---|
| Firmware on board | **v1.00.01** |
| Commit | `06e7358` |
| Board | Guition JC3248W535EN @ `http://192.168.68.60` |
| Canonical evidence | Board **LOG** (Discord gateway) -- [`lan-monitor-gateway-pass-20260928.log`](lan-monitor-gateway-pass-20260928.log) |
| Boot (wall, PT) | **~05:07:50** (monitor RELOGIN 05:08:34, up 44 s) |
| READY #1 | millis **19470** (~**05:08:10** PT) |
| READY #2 (after OP7) | millis **9071755** (~**07:39:02** PT) |
| Attested window | **~2 h 31 m** continuous Gateway uptime (19470 to 9071755) |
| Interactive A-E | **PASS** same day |
| Overnight F (Gateway) | **PASS** |
| Result | **PASS** |

### Board LOG (canonical -- what the soak really did)

```
[17428] BIND_HOST gateway.discord.gg
[17503] DROP_START: WS_DISCONNECTED_WIFI_UP reason=TCP connection cleanup rssi=-52 seq=0 session
[17504] DISCONNECT_AT millis=17504 rssi=-52 heap=70672 psram=8042640
[17504] CLEAR_SESSION disconnect
[17504] RECONNECT_INTERVAL_MS=200
[19155] CONNECT_AT millis=19155 gap_ms=1651
[19155] WS_CONNECTED
[19159] OP10_HELLO hb_ms=41250
[19161] SENT_IDENTIFY
[19470] RECONNECT_INTERVAL_MS=3000
[19470] RECOVERED
[19470] READY session=yes
[9067748] DROP_START: OP7_RECONNECT
[9067748] CLEAR_SESSION op7
[9067748] RECONNECT_INTERVAL_MS=200
[9067752] WS_DISCONNECTED_WIFI_UP rssi=-59 seq=0 session=no
[9067752] DISCONNECT_AT millis=9067752 rssi=-59 heap=82268 psram=8035536
[9067752] RECONNECT_INTERVAL_MS=200
[9069949] CONNECT_AT millis=9069949 gap_ms=2197
[9069950] WS_CONNECTED
[9069953] OP10_HELLO hb_ms=41250
[9069955] SENT_IDENTIFY
[9071755] RECONNECT_INTERVAL_MS=3000
[9071755] RECOVERED
[9071755] READY session=yes
```

### Scoring

| Event | Timeframe | Outcome |
|---|---|---|
| First WS open drop (`TCP connection cleanup`, Wi-Fi up, rssi=-52) | millis 17503-19470 (~**1.7 s** gap) | **PASS** -- RECOVERED / READY |
| Idle Gateway | 19470 to 9067748 (~**2 h 31 m**) | **PASS** -- no drop logged |
| Discord **OP7_RECONNECT** | millis 9067748-9071755 (~**2.2 s** gap) | **PASS** -- RECOVERED / READY |
| Heap / PSRAM at drops | 70k / 8.0M then 82k / 8.0M | **PASS** -- no crash signature |

LAN `FETCH_FAIL` / monitor hung polls / earlier pre-05:08 segments are **out of scope** for this Gateway PASS (LAN-only or prior boots). WEB `botOnline` Idle/Online churn with `gw=True` is expected Idle cpu=160 and is **not** a Gateway fail.

### Verdict

**v1.00.01 Gateway soak: PASS.** Board LOG is authoritative for this attestation.

---
## 2026-09-24 -- v0.7.80 overnight Gateway soak (monitor stopped)



| Field | Value |

|---|---|

| Firmware on board | **v0.7.80** (`Display  -  v0.7.80`) |

| Workspace tree | **v0.7.85** local (SD/secrets/rings) -- **not flashed** this soak |

| Board | Guition JC3248W535EN @ `http://192.168.68.60` |

| Monitor | `docs/lan-monitor.ps1` (Bypass) |

| Start | **2026-09-23T21:45:20** PDT |

| Stop | **2026-09-24T01:57:57** PDT (operator) |

| End snapshot | **2026-09-24T01:57:58** PDT -- [`lan-status-snapshot.json`](lan-status-snapshot.json) |

| End uptime | **0d 4h 57m 57s** |

| Gateway | **Good** throughout (`gw=1`); no `gw=false` >=60 s stop |

| Bot | Idle/Online churn (cpu 160/240); end **Idle** cpu **160** |

| Heap / PSRAM | heapPct **33**; psramPct **96** |

| LCD / lastEvent | awake; lastEvent **GW drop** (recovered) |

| Temp / SD | `tempOk=false` (probe not on GPIO 18 yet); no `sdOk` on this build |

| Mid pull | 00:39:52 PDT @ up **3h 40m 32s** (same ver) |

| Result | **PASS** (Gateway soak) -- not a v0.7.83/0.7.84/0.7.85 feature PASS |

| Next | Flash `Display  -  v0.8.4` then [`soak-0.8.4.md`](soak-0.8.4.md) (covers 0.8.0-0.8.4). Prior SD checklist: [`soak-0.7.85.md`](soak-0.7.85.md) |



### lan-monitor.log (excerpt)



```

=== MiniMe II monitor start 2026-09-23T21:45:20 ... base=http://192.168.68.60 ===

[21:45:20] gw=True botOnline=True cpu=240 up=0d 0h 46m 0s heapPct=34

[21:45:20] LOG READY session=yes

[21:59:05] gw=True botOnline=False cpu=160 up=0d 0h 59m 45s heapPct=32

=== SNAPSHOT 2026-09-24T00:39:52-07:00 ver=0.7.80 up=0d 3h 40m 32s gw=1 idle ===

[01:12:52] gw=True botOnline=True cpu=240 up=0d 4h 13m 32s

[01:13:24] LOG READY session=yes

[01:17:50] gw=True botOnline=False cpu=160 up=0d 4h 18m 30s

[01:20:40] gw=True botOnline=True cpu=240 up=0d 4h 21m 21s

[01:25:41] gw=True botOnline=False cpu=160 up=0d 4h 26m 21s

=== MiniMe II monitor STOP 2026-09-24T01:57:57-07:00 ... ver=0.7.80 up~4h58m gw=1 idle ===

```



### MiniMe LOG (fulllog)



- Boot Identify -> READY

- **OP7_RECONNECT** ~44.6 min: gap **1646 ms** -> `RECOVERED` / READY

- Second **OP7_RECONNECT** ~4h 13m (`DROP_START` @ 15207402): gap **2134 ms** -> READY @ 15209974

- No stuck disconnect; GW stayed Good after each recover



### MiniMe Serial



- Steady `[GW] alive ... gw=1 id=1 drop=0` (~60 s)

- Repeated `[C0] DS18B20 disconnected` (sensor not fitted / not on GPIO 18 -- expected)



---



## 2026-09-23 -- v0.7.80 split + fixed buffers + Controls



| Field | Value |

|---|---|

| Firmware | **v0.7.80** (`Display  -  v0.7.80`) |

| Commit | `57a9b41` -- web/discord file split; README status/OTA/LAN; (CI tokenize/`ci_html` fix still local at attestation) |

| Board | Guition JC3248W535EN @ `http://192.168.68.60` |

| Playbook | [`soak-0.7.80.md`](soak-0.7.80.md) |

| Monitor | `docs/lan-monitor.ps1` (Bypass); start **21:45** PDT |

| Attestation | **21:56** PDT (operator) |

| Result | **PASS** (blocks A-D) |



### What was under test



- Fixed-buffer Discord REST / DeepSeek (`!ask` short + long + hammer)

- `web_ui` / `web_render` / `discord_http` split (LAN Display/Log/Controls)

- Web slider debounce (bright/vol)

- LCD Controls hit boxes; Save / Cancel / factory (long-press + `!resetprefs`)

- Smoke: wake, theme chip, DM/@mention + `!clear`



### Operator report



Blocks **A, B, C, D** completed -- all worked as expected. No fail noted (panic, stuck GW, miss-hits, bad prefs after Save, `!ask` hang).



### Monitor note



Interactive attestation ~**11 min** after monitor start (not a full >=30 min idle-only Gateway watch). Leave `lan-monitor` running longer if you want a separate idle GW stamp for this build.



---



## 2026-09-22 / 2026-09-23 -- v0.7.43 TWDT + `!ask` hammer



| Field | Value |

|---|---|

| Firmware | **v0.7.43** (`Display  -  v0.7.43`) |

| Commit | `a73fa11` -- 90 s loop TWDT, Discord send 60 s budget, `!ask` handshake 15 s / body 30 s |

| Board | Guition JC3248W535EN @ `http://192.168.68.60` |

| Monitor | `docs/lan-monitor.ps1` (default base URL) |

| Monitor wall start | 2026-09-22 **15:15** PDT |

| Attestation time | 2026-09-23 ~**00:19** PDT |

| Result | **PASS** |



### What was under test



- Loop-task TWDT at **90 s** (`enableLoopWDT` only; no `uiTask` subscribe; no mid-HTTPS TWDT reset sprinkle)

- `sendDiscordMessage` **60 s** wall budget

- DeepSeek `!ask` timeouts: handshake **15 s**, body deadline **30 s**

- Dual-core HOL path (dedicated DeepSeek TLS)



### Board continuous uptime



- Intentional **software-update reboot** ~**15:55** PDT (not a panic). Uptime reset; Gateway down **3 s**, then `READY`.

- Continuous post-update uptime reached **~7h 45m+** by **23:42** (still climbing; no further uptime reset in the log through attestation).



### Gateway outages (`GW_DOWN` via lan-monitor)



| When (PDT) | Duration | Cause |

|---|---:|---|

| 15:55:36 -> 15:55:39 | **3 s** | Software update reboot |

| 22:56:23 -> 22:56:28 | **5 s** | Brief drop; recovered; `READY` ~1 min later |



- **Total GW-down:** ~**8 s**

- **No** `GW_DOWN` lasting >=60 s

- **No** `MONITOR_STOP`



### LAN poll timeouts (`FETCH_FAIL`)



Seven LAN `/api/status` timeouts (monitor HTTP timeout). **Not** Discord Gateway outages. One fell during the `!ask` hammer (23:16) -- expected when Core 1 is busy on HTTPS.



### `!ask` stress



| Field | Value |

|---|---|

| Window | **23:10 -> 00:16** PDT (hammered repeatedly) |

| Discord message drops | **None** (operator report) |

| `GW_DOWN` in window | **None** |

| Reboot / TWDT panic | **None** (uptime kept increasing) |

| Heap (samples) | Stable ~**37-38%** |



### Verdict



**v0.7.43 TWDT + ask budgets are board-proven** under multi-hour soak and ~66 minutes of repeated `!ask`. Soft Discord guard (HB ack) + 90 s loop backstop held without false panic. GitHub CI green still does not replace HIL; this file is the HIL attestation for this release.



## TWDT positive proof -- `!hang` (v0.7.48+)



Proves the **90 s loop TWDT fires when Core 1 hangs**, not only that it stays quiet under `!ask`.



### Scratch build steps



1. In `MiniMe_Discord_Bot_II/minime_config.h`, uncomment `#define MINIME_TEST_TWDT`.

2. Flash that build (`Display  -  v0.7.48` or newer with the define).

3. As owner: `!hang` -- LCD may show `TWDT` / `hang...`; Discord goes quiet (loop stuck).

4. Wait **>= 90 s** for Task WDT panic + reboot.

5. After boot: owner `!coredump` -- summary should name **Task WDT** / loop task.

6. Save a screenshot of the `!coredump` reply (or Serial/LOG lines above `ELF file SHA256`) into `docs/` and link it below.

7. **Comment out** `#define MINIME_TEST_TWDT` again and reflash production (never leave `!hang` enabled).



### Result (fill after run)



| Field | Value |

|---|---|

| Firmware | scratch build with `MINIME_TEST_TWDT` (App SHA `d7804ba59...`) |

| Date | 2026-09-23 ~**01:54** PDT |

| Panic reason | **Task watchdog got triggered** -- `loopTask (CPU 1)` did not reset in time |

| Evidence | Discord `!coredump` after reboot (coredump @ `0xFD0000`, PC `0x4037B59F`, IDLE1 / ExcCause as reported) |

| Screenshot | [`TWDT.jpg`](TWDT.jpg) |

| Production build | v0.7.48, `MINIME_TEST_TWDT` **not** defined; `!hang` handler compiled out (`kCmds` does not register it) |

| Result | **PASS** |



**Verdict:** 90 s loop TWDT **fires when Core 1 hangs** (`!hang`), proven via flash coredump -- not only quiet under soak/`!ask`.

